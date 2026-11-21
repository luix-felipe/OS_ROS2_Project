#include <ament_index_cpp/get_package_prefix.hpp>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "fleet_posix/common.hpp"

namespace
{

using fleet_posix::Command;
using fleet_posix::CommandType;
using fleet_posix::FleetState;
using fleet_posix::LogEvent;
using fleet_posix::lock_fleet_state;

volatile sig_atomic_t g_stop_requested = 0;

// Marca pedido de parada ao receber SIGINT/SIGTERM.
void signal_handler(int)
{
  g_stop_requested = 1;
}

// Reporta o erro de uma chamada pthread.
bool check_pthread(int result, const char * operation)
{
  if (result == 0) {
    return true;
  }
  errno = result;
  std::perror(operation);
  return false;
}

// Cria e remove os recursos de IPC da frota.
class IpcResources
{
public:
  // Define nomes unicos por PID para os objetos de IPC.
  IpcResources()
  : shm_name("/fleet_posix_state_" + std::to_string(getpid())),
    fifo_name("/tmp/fleet_posix_ctl_" + std::to_string(getpid()))
  {
    for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
      queue_names[index] =
        "/fleet_posix_cmd_" + std::to_string(getpid()) + "_" + std::to_string(index + 1);
      queues[index] = static_cast<mqd_t>(-1);
    }
  }

  // Fecha e remove filas, pipe, FIFO e memoria compartilhada.
  ~IpcResources()
  {
    for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
      if (queues[index] != static_cast<mqd_t>(-1)) {
        mq_close(queues[index]);
        mq_unlink(queue_names[index].c_str());
      }
    }
    for (const int fd : log_pipe) {
      if (fd >= 0) {
        close(fd);
      }
    }
    for (const int fd : {fifo_fd, fifo_keepalive_fd_}) {
      if (fd >= 0) {
        close(fd);
      }
    }
    if (fifo_created_) {
      unlink(fifo_name.c_str());
    }
    if (state != nullptr) {
      if (corridor_initialized_) {
        sem_destroy(&state->corridor);
      }
      if (mutex_initialized_) {
        pthread_mutex_destroy(&state->mutex);
      }
      munmap(state, sizeof(FleetState));
    }
    if (shm_created_) {
      shm_unlink(shm_name.c_str());
    }
  }

  // Cria memoria compartilhada, mutex, semaforo, filas e pipe.
  bool initialize(bool unsafe_mode)
  {
    const int shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (shm_fd == -1) {
      std::perror("shm_open");
      return false;
    }
    shm_created_ = true;
    if (ftruncate(shm_fd, sizeof(FleetState)) == -1) {
      std::perror("ftruncate");
      close(shm_fd);
      return false;
    }
    void * mapping = mmap(
      nullptr, sizeof(FleetState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (mapping == MAP_FAILED) {
      std::perror("mmap");
      return false;
    }
    state = static_cast<FleetState *>(mapping);
    std::memset(state, 0, sizeof(*state));

    pthread_mutexattr_t attributes;
    if (!check_pthread(pthread_mutexattr_init(&attributes), "pthread_mutexattr_init")) {
      return false;
    }
    const bool attributes_ok =
      check_pthread(
      pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED),
      "pthread_mutexattr_setpshared") &&
      check_pthread(
      pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST),
      "pthread_mutexattr_setrobust");
    if (attributes_ok) {
      mutex_initialized_ = check_pthread(
        pthread_mutex_init(&state->mutex, &attributes), "pthread_mutex_init");
    }
    pthread_mutexattr_destroy(&attributes);
    if (!mutex_initialized_) {
      return false;
    }
    if (sem_init(&state->corridor, 1, 1) == -1) {
      std::perror("sem_init corredor");
      return false;
    }
    corridor_initialized_ = true;
    state->safe_mode = unsafe_mode ? 0 : 1;

    mq_attr queue_attributes{};
    queue_attributes.mq_maxmsg = 10;
    queue_attributes.mq_msgsize = sizeof(Command);
    for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
      queues[index] = mq_open(
        queue_names[index].c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NONBLOCK,
        0600, &queue_attributes);
      if (queues[index] == static_cast<mqd_t>(-1)) {
        std::perror("mq_open");
        return false;
      }
    }
    if (pipe(log_pipe) == -1) {
      std::perror("pipe");
      return false;
    }
    return true;
  }

  // Named pipe (FIFO) de controle: permite enviar comandos de outro terminal,
  // por exemplo: echo "enviar 1 9.0 5.15" > /tmp/fleet_posix_ctl_<pid>
  bool create_control_fifo()
  {
    if (mkfifo(fifo_name.c_str(), 0600) == -1) {
      std::perror("mkfifo");
      return false;
    }
    fifo_created_ = true;
    // O_NONBLOCK: abrir para leitura nao bloqueia sem escritores. O_CLOEXEC: os
    // filhos (exec) nao herdam o canal do operador.
    fifo_fd = open(fifo_name.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fifo_fd == -1) {
      std::perror("open FIFO leitura");
      return false;
    }
    // Escritor interno mantido aberto: quando um "echo" externo fecha o FIFO,
    // o leitor nao recebe EOF/POLLHUP continuo e o poll nao entra em espera ocupada.
    fifo_keepalive_fd_ = open(fifo_name.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fifo_keepalive_fd_ == -1) {
      std::perror("open FIFO escritor interno");
      return false;
    }
    return true;
  }

  IpcResources(const IpcResources &) = delete;
  IpcResources & operator=(const IpcResources &) = delete;

  std::string shm_name;
  std::string fifo_name;
  int fifo_fd{-1};
  std::string queue_names[fleet_posix::kRobotCount];
  mqd_t queues[fleet_posix::kRobotCount];
  FleetState * state{nullptr};
  int log_pipe[2]{-1, -1};

private:
  bool shm_created_{false};
  bool mutex_initialized_{false};
  bool corridor_initialized_{false};
  bool fifo_created_{false};
  int fifo_keepalive_fd_{-1};
};

// PID e status de um processo filho.
struct ChildProcess
{
  pid_t pid{-1};
  int status{0};
  bool reaped{false};
};

// Recolhe o filho com waitpid sem bloquear.
bool reap_if_finished(ChildProcess & child)
{
  if (child.pid <= 0 || child.reaped) {
    return child.reaped;
  }
  const pid_t result = waitpid(child.pid, &child.status, WNOHANG);
  if (result == child.pid || (result == -1 && errno == ECHILD)) {
    child.reaped = true;
  }
  return child.reaped;
}

// Verifica se todos os filhos continuam em execucao.
bool children_running(ChildProcess & turtlesim, ChildProcess agents[3])
{
  bool running = !reap_if_finished(turtlesim);
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    running = !reap_if_finished(agents[index]) && running;
  }
  if (!running) {
    std::fprintf(stderr, "Um processo filho terminou antes do encerramento da frota.\n");
  }
  return running;
}

// Aguarda o termino do filho ate o prazo.
bool wait_for_child(ChildProcess & child, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!reap_if_finished(child) && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return reap_if_finished(child);
}

// Encerra o filho, escalando de SIGTERM para SIGKILL.
bool finish_child(ChildProcess & child, bool terminate)
{
  if (child.pid <= 0) {
    return false;
  }
  if (terminate && !reap_if_finished(child)) {
    kill(child.pid, SIGTERM);
  }
  if (!wait_for_child(child, std::chrono::seconds(3))) {
    std::fprintf(stderr, "Filho pid=%d excedeu prazo de shutdown; enviando SIGTERM.\n", child.pid);
    kill(child.pid, SIGTERM);
    if (!wait_for_child(child, std::chrono::seconds(2))) {
      std::fprintf(stderr, "Filho pid=%d nao respondeu; enviando SIGKILL.\n", child.pid);
      kill(child.pid, SIGKILL);
      pid_t result;
      do {
        result = waitpid(child.pid, &child.status, 0);
      } while (result == -1 && errno == EINTR);
      child.reaped = result == child.pid;
      return false;
    }
  }
  return child.reaped && WIFEXITED(child.status) && WEXITSTATUS(child.status) == 0;
}

// No filho, antes do exec: o terminal pertence somente ao supervisor. Sem isso,
// Turtlesim e agentes herdariam o stdin do operador.
void detach_stdin()
{
  const int null_fd = open("/dev/null", O_RDONLY);
  if (null_fd >= 0) {
    dup2(null_fd, STDIN_FILENO);
    if (null_fd != STDIN_FILENO) {
      close(null_fd);
    }
  }
}

// Le exatamente size bytes do descritor.
bool read_exact(int fd, void * destination, std::size_t size)
{
  auto * cursor = static_cast<unsigned char *>(destination);
  std::size_t remaining = size;
  while (remaining > 0) {
    const ssize_t result = read(fd, cursor, remaining);
    if (result == 0) {
      return false;
    }
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result < 0) {
      return false;
    }
    cursor += result;
    remaining -= static_cast<std::size_t>(result);
  }
  return true;
}

// Thread que imprime os eventos recebidos pelo pipe.
void * logger_thread(void * argument)
{
  const int fd = *static_cast<int *>(argument);
  LogEvent event{};
  while (read_exact(fd, &event, sizeof(event))) {
    std::printf(
      "[AMR %d | pid=%d] %.*s\n", event.robot_id, event.pid,
      static_cast<int>(sizeof(event.text)), event.text);
  }
  return nullptr;
}

// Envia um comando pela fila POSIX do AMR.
bool send_command(mqd_t queue, const Command & command)
{
  const unsigned int priority = command.type == CommandType::kMission ? 0U : 1U;
  int result;
  do {
    result = mq_send(queue, reinterpret_cast<const char *>(&command), sizeof(command), priority);
  } while (result == -1 && errno == EINTR && !g_stop_requested);
  if (result == -1) {
    if (errno == EAGAIN) {
      std::fprintf(stderr, "Fila POSIX cheia: comando para AMR %u recusado.\n", command.robot_id);
    } else {
      std::perror("mq_send");
    }
    return false;
  }
  return true;
}

// Copia do estado da frota para impressao.
struct StatusSnapshot
{
  fleet_posix::RobotState robots[fleet_posix::kRobotCount];
  int corridor_owner;
  std::uint32_t collision_count;
  int safe_mode;
};

// Imprime a tabela de estado da frota.
void print_status(FleetState * state)
{
  // Secao critica minima: copia o estado sob o mutex e imprime fora dele. Um
  // stdout lento (pipe/tee cheio) nao bloqueia os callbacks de pose dos agentes.
  StatusSnapshot snapshot{};
  lock_fleet_state(state);
  std::memcpy(snapshot.robots, state->robots, sizeof(snapshot.robots));
  snapshot.corridor_owner = state->corridor_owner;
  snapshot.collision_count = state->collision_count;
  snapshot.safe_mode = state->safe_mode;
  pthread_mutex_unlock(&state->mutex);

  std::printf(
    "\n%-5s %-8s %-8s %-22s %-8s %-9s\n",
    "AMR", "X", "Y", "ESTADO", "MISSAO", "CONCLUIDAS");
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    const auto & robot = snapshot.robots[index];
    std::printf(
      "%-5d %-8.2f %-8.2f %-22s %-8u %-9u\n",
      index + 1, robot.x, robot.y, fleet_posix::mode_name(robot.mode),
      robot.current_mission, robot.completed_missions);
  }
  const std::string owner =
    snapshot.corridor_owner == 0 ? "livre" : std::to_string(snapshot.corridor_owner);
  std::printf(
    "corredor=%s | colisoes=%u | modo=%s\n\n",
    owner.c_str(), snapshot.collision_count, snapshot.safe_mode ? "SEGURO" : "INSEGURO");
}

// Conta e reporta colisoes logicas entre AMRs.
void detect_collisions(FleetState * state, bool active_pairs[3][3])
{
  // Colisao detectada, impressa apos liberar o mutex.
  struct CollisionEvent
  {
    int first;
    int second;
    double x;
    double y;
  };
  CollisionEvent events[fleet_posix::kRobotCount * fleet_posix::kRobotCount]{};
  int event_count = 0;

  lock_fleet_state(state);
  for (int first = 0; first < fleet_posix::kRobotCount; ++first) {
    for (int second = first + 1; second < fleet_posix::kRobotCount; ++second) {
      const auto & a = state->robots[first];
      const auto & b = state->robots[second];
      const bool colliding = a.online && b.online && std::hypot(a.x - b.x, a.y - b.y) < 0.55;
      if (colliding && !active_pairs[first][second]) {
        ++state->collision_count;
        events[event_count++] =
          CollisionEvent{first + 1, second + 1, (a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
      }
      active_pairs[first][second] = colliding;
    }
  }
  pthread_mutex_unlock(&state->mutex);

  // I/O fora da secao critica compartilhada entre processos.
  for (int index = 0; index < event_count; ++index) {
    std::printf(
      "[DETECTOR] colisao logica entre AMR %d e AMR %d em (%.2f, %.2f)\n",
      events[index].first, events[index].second, events[index].x, events[index].y);
  }
}

// Indica se os tres AMRs estao online.
bool all_robots_online(FleetState * state)
{
  bool result = true;
  lock_fleet_state(state);
  for (const auto & robot : state->robots) {
    result = result && robot.online;
  }
  pthread_mutex_unlock(&state->mutex);
  return result;
}

// Indica se todos os AMRs concluiram uma missao.
bool all_missions_complete(FleetState * state)
{
  bool result = true;
  lock_fleet_state(state);
  for (const auto & robot : state->robots) {
    result = result && robot.completed_missions > 0;
  }
  pthread_mutex_unlock(&state->mutex);
  return result;
}

// Executa a demonstracao automatica com tres missoes.
bool run_demo(IpcResources & ipc, ChildProcess & turtlesim, ChildProcess agents[3])
{
  std::printf("Aguardando os tres agentes criarem suas tartarugas...\n");
  for (int attempt = 0; attempt < 100 && !all_robots_online(ipc.state); ++attempt) {
    if (g_stop_requested || !children_running(turtlesim, agents)) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!all_robots_online(ipc.state)) {
    std::fprintf(stderr, "Nem todos os AMRs ficaram online; encerrando demonstracao.\n");
    return false;
  }

  const float targets[3][2] = {{9.0F, 5.15F}, {5.5F, 9.0F}, {2.0F, 5.85F}};
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    Command command{};
    command.type = CommandType::kMission;
    command.robot_id = static_cast<std::uint32_t>(index + 1);
    command.target_x = targets[index][0];
    command.target_y = targets[index][1];
    command.mission_id = static_cast<std::uint32_t>(index + 1);
    if (!send_command(ipc.queues[index], command)) {
      return false;
    }
  }

  std::printf("Tres missoes simultaneas enviadas pelas filas POSIX.\n");
  bool collision_pairs[3][3]{};
  for (int tick = 0; tick < 240 && !g_stop_requested; ++tick) {
    if (!children_running(turtlesim, agents)) {
      return false;
    }
    detect_collisions(ipc.state, collision_pairs);
    if (tick % 25 == 0) {
      print_status(ipc.state);
    }
    if (all_missions_complete(ipc.state)) {
      lock_fleet_state(ipc.state);
      const bool expected_collisions = ipc.state->safe_mode ?
        ipc.state->collision_count == 0 : ipc.state->collision_count > 0;
      pthread_mutex_unlock(&ipc.state->mutex);
      print_status(ipc.state);
      if (!expected_collisions) {
        std::fprintf(stderr, "Demonstracao apresentou contagem de colisoes inesperada.\n");
      }
      return expected_collisions;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  std::fprintf(stderr, "Demonstracao interrompida ou prazo das missoes excedido.\n");
  print_status(ipc.state);
  return false;
}

// Interpreta e executa um comando do operador.
bool handle_console_line(
  const std::string & line, IpcResources & ipc, std::uint32_t & next_mission)
{
  std::istringstream input(line);
  std::string action;
  input >> action;
  if (action.empty()) {
    return true;
  }
  if (action == "status") {
    print_status(ipc.state);
    return true;
  }
  if (action == "sair") {
    return false;
  }

  int robot = 0;
  if (!(input >> robot) || robot < 1 || robot > fleet_posix::kRobotCount) {
    std::printf("AMR deve estar entre 1 e 3.\n");
    return true;
  }
  Command command{};
  command.robot_id = static_cast<std::uint32_t>(robot);
  if (action == "enviar") {
    input >> command.target_x >> command.target_y;
    if (!input || !std::isfinite(command.target_x) || !std::isfinite(command.target_y) ||
      command.target_x < 0.5F || command.target_x > 10.5F ||
      command.target_y < 0.5F || command.target_y > 10.5F)
    {
      std::printf("Destino invalido. Use coordenadas entre 0.5 e 10.5.\n");
      return true;
    }
    // Um AMR estacionado dentro da regiao critica nao possui o semaforo e
    // quebraria a exclusao mutua: destinos ali sao recusados.
    if (std::hypot(
        command.target_x - fleet_posix::kCorridorCenterX,
        command.target_y - fleet_posix::kCorridorCenterY) < fleet_posix::kCorridorAcquireRadius)
    {
      std::printf(
        "Destino dentro da regiao critica do corredor (raio %.2f em torno de "
        "(%.1f, %.1f)); escolha um ponto fora dela.\n",
        fleet_posix::kCorridorAcquireRadius, fleet_posix::kCorridorCenterX,
        fleet_posix::kCorridorCenterY);
      return true;
    }
    command.type = CommandType::kMission;
    command.mission_id = next_mission++;
  } else if (action == "pausar") {
    command.type = CommandType::kPause;
  } else if (action == "retomar") {
    command.type = CommandType::kResume;
  } else {
    std::printf("Comando desconhecido.\n");
    return true;
  }
  send_command(ipc.queues[robot - 1], command);
  return true;
}

// Le o que estiver disponivel em fd e processa linhas completas. Retorna false
// quando o operador pediu "sair"; eof indica fim do descritor.
bool drain_lines(
  int fd, std::string & pending, IpcResources & ipc, std::uint32_t & next_mission,
  const char * origin, bool & eof)
{
  char buffer[512];
  const ssize_t received = read(fd, buffer, sizeof(buffer));
  if (received == -1 && (errno == EINTR || errno == EAGAIN)) {
    return true;
  }
  if (received < 0) {
    std::perror("read comandos do operador");
    eof = true;
    return true;
  }
  if (received == 0) {
    eof = true;
    if (!pending.empty()) {
      const std::string line = pending;
      pending.clear();
      return handle_console_line(line, ipc, next_mission);
    }
    return true;
  }
  pending.append(buffer, static_cast<std::size_t>(received));
  std::size_t newline;
  while ((newline = pending.find('\n')) != std::string::npos) {
    const std::string line = pending.substr(0, newline);
    pending.erase(0, newline + 1);
    if (origin != nullptr && !line.empty()) {
      std::printf("[%s] %s\n", origin, line.c_str());
    }
    if (!handle_console_line(line, ipc, next_mission)) {
      return false;
    }
    std::printf("fleet> ");
    std::fflush(stdout);
  }
  if (pending.size() > 4096) {
    std::fprintf(stderr, "Linha de comando muito longa; entrada descartada.\n");
    pending.clear();
  }
  return true;
}

// Operacao online: multiplexa stdin e FIFO com poll.
bool run_console(IpcResources & ipc, ChildProcess & turtlesim, ChildProcess agents[3])
{
  std::printf(
    "Comandos: enviar <amr> <x> <y> | pausar <amr> | retomar <amr> | "
    "status | sair\n");
  std::printf("Canal FIFO (outro terminal): echo \"status\" > %s\n", ipc.fifo_name.c_str());
  std::printf("fleet> ");
  std::fflush(stdout);
  std::uint32_t next_mission = 1;
  std::string stdin_pending;
  std::string fifo_pending;
  bool collision_pairs[3][3]{};
  while (!g_stop_requested) {
    if (!children_running(turtlesim, agents)) {
      return false;
    }
    detect_collisions(ipc.state, collision_pairs);
    // poll() multiplexa duas fontes de comandos: terminal (stdin) e FIFO.
    pollfd sources[2] = {{STDIN_FILENO, POLLIN, 0}, {ipc.fifo_fd, POLLIN, 0}};
    const int ready = poll(sources, 2, 50);
    if (ready == -1 && errno == EINTR) {
      continue;
    }
    if (ready == -1 || ((sources[0].revents | sources[1].revents) & (POLLERR | POLLNVAL))) {
      std::fprintf(stderr, "Erro ao aguardar entrada do operador.\n");
      return false;
    }
    if (ready == 0) {
      continue;
    }
    if (sources[0].revents & (POLLIN | POLLHUP)) {
      bool eof = false;
      if (!drain_lines(STDIN_FILENO, stdin_pending, ipc, next_mission, nullptr, eof) || eof) {
        // "sair" ou fim do terminal (Ctrl+D) encerram a frota.
        return true;
      }
    }
    if (sources[1].revents & POLLIN) {
      bool eof = false;
      if (!drain_lines(ipc.fifo_fd, fifo_pending, ipc, next_mission, "FIFO", eof)) {
        return true;
      }
    }
  }
  return true;
}

// Cria IPC e processos, executa o modo escolhido e encerra a frota.
int run_supervisor(bool demo_mode, bool unsafe_mode)
{
  // Descoberta potencialmente falha precede a criacao de IPC e filhos.
  const std::filesystem::path executable_dir =
    std::filesystem::canonical("/proc/self/exe").parent_path();
  const std::string agent_executable = (executable_dir / "amr_agent").string();
  const std::string turtlesim_executable =
    ament_index_cpp::get_package_prefix("turtlesim") + "/lib/turtlesim/turtlesim_node";
  IpcResources ipc;
  if (!ipc.initialize(unsafe_mode)) {
    return EXIT_FAILURE;
  }
  if (!demo_mode && !ipc.create_control_fifo()) {
    return EXIT_FAILURE;
  }
  std::string agent_ids[fleet_posix::kRobotCount];
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    agent_ids[index] = std::to_string(index + 1);
  }
  const std::string log_fd = std::to_string(ipc.log_pipe[1]);

  std::printf(
    "FleetPOSIX supervisor pid=%d | modo=%s\n",
    getpid(), unsafe_mode ? "INSEGURO" : "SEGURO");
  ChildProcess turtlesim;
  ChildProcess agents[fleet_posix::kRobotCount];
  turtlesim.pid = fork();
  if (turtlesim.pid == 0) {
    detach_stdin();
    close(ipc.log_pipe[0]);
    close(ipc.log_pipe[1]);
    execl(turtlesim_executable.c_str(), turtlesim_executable.c_str(), nullptr);
    std::perror("exec turtlesim_node");
    _exit(127);
  }
  if (turtlesim.pid < 0) {
    std::perror("fork turtlesim");
    return EXIT_FAILURE;
  }

  bool startup_ok = true;
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    agents[index].pid = fork();
    if (agents[index].pid == 0) {
      detach_stdin();
      close(ipc.log_pipe[0]);
      execl(
        agent_executable.c_str(), agent_executable.c_str(), "--id", agent_ids[index].c_str(),
        "--shm", ipc.shm_name.c_str(), "--queue", ipc.queue_names[index].c_str(),
        "--log-fd", log_fd.c_str(), nullptr);
      std::perror("exec amr_agent");
      _exit(127);
    }
    if (agents[index].pid < 0) {
      std::perror("fork amr_agent");
      startup_ok = false;
      break;
    }
  }
  close(ipc.log_pipe[1]);
  ipc.log_pipe[1] = -1;
  pthread_t logger{};
  const bool logger_started = check_pthread(
    pthread_create(&logger, nullptr, logger_thread, &ipc.log_pipe[0]), "pthread_create logger");
  bool run_ok = false;
  try {
    if (startup_ok && logger_started) {
      run_ok = demo_mode ? run_demo(ipc, turtlesim, agents) : run_console(ipc, turtlesim, agents);
    }
  } catch (const std::exception & error) {
    std::fprintf(stderr, "Falha durante operacao: %s\n", error.what());
  }

  lock_fleet_state(ipc.state);
  ipc.state->shutdown_requested = 1;
  pthread_mutex_unlock(&ipc.state->mutex);
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    if (agents[index].pid > 0 && !agents[index].reaped) {
      Command shutdown{};
      shutdown.type = CommandType::kShutdown;
      shutdown.robot_id = static_cast<std::uint32_t>(index + 1);
      send_command(ipc.queues[index], shutdown);
    }
    sem_post(&ipc.state->corridor);
  }

  bool agents_ok = true;
  for (auto & agent : agents) {
    agents_ok = finish_child(agent, false) && agents_ok;
  }
  finish_child(turtlesim, true);
  if (logger_started) {
    pthread_join(logger, nullptr);
  }
  // O leitor so e fechado depois de todos os escritores e da thread de log.
  close(ipc.log_pipe[0]);
  ipc.log_pipe[0] = -1;
  lock_fleet_state(ipc.state);
  ipc.state->corridor_owner = 0;
  pthread_mutex_unlock(&ipc.state->mutex);
  std::printf("Supervisor recolheu todos os processos com waitpid().\n");
  print_status(ipc.state);
  return run_ok && agents_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

// Le as opcoes, instala os sinais e inicia o supervisor.
int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  bool demo_mode = false;
  bool unsafe_mode = false;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--demo") == 0) {
      demo_mode = true;
    } else if (std::strcmp(argv[index], "--unsafe") == 0) {
      unsafe_mode = true;
    } else if (std::strcmp(argv[index], "--help") == 0) {
      std::printf("Uso: fleet_supervisor [--demo] [--unsafe]\n");
      return EXIT_SUCCESS;
    } else {
      std::fprintf(stderr, "Opcao desconhecida: %s\n", argv[index]);
      return EXIT_FAILURE;
    }
  }
  struct sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_handler = signal_handler;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);

  int result = EXIT_FAILURE;
  try {
    result = run_supervisor(demo_mode, unsafe_mode);
  } catch (const std::exception & error) {
    std::fprintf(stderr, "Falha ao iniciar supervisor: %s\n", error.what());
  }
  std::exit(result);
}
