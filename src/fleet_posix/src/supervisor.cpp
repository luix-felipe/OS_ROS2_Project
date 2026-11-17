#include <ament_index_cpp/get_package_prefix.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <mqueue.h>
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
using fleet_posix::RobotMode;

volatile sig_atomic_t g_stop_requested = 0;

void signal_handler(int)
{
  g_stop_requested = 1;
}

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

void * logger_thread(void * argument)
{
  const int fd = *static_cast<int *>(argument);
  LogEvent event{};
  while (read_exact(fd, &event, sizeof(event))) {
    std::printf("[AMR %d | pid=%d] %s\n", event.robot_id, event.pid, event.text);
  }
  return nullptr;
}

bool send_command(mqd_t queue, const Command & command)
{
  if (mq_send(
      queue, reinterpret_cast<const char *>(&command), sizeof(command), 0) == -1)
  {
    std::perror("mq_send");
    return false;
  }
  return true;
}

void print_status(FleetState * state)
{
  pthread_mutex_lock(&state->mutex);
  std::printf("\n%-5s %-8s %-8s %-22s %-8s %-9s\n",
    "AMR", "X", "Y", "ESTADO", "MISSAO", "CONCLUIDAS");
  for (int index = 0; index < fleet_posix::kRobotCount; ++index) {
    const auto & robot = state->robots[index];
    std::printf(
      "%-5d %-8.2f %-8.2f %-22s %-8u %-9u\n",
      index + 1, robot.x, robot.y, fleet_posix::mode_name(robot.mode),
      robot.current_mission, robot.completed_missions);
  }
  std::printf(
    "corredor=%s | colisoes=%u | modo=%s\n\n",
    state->corridor_owner == 0 ? "livre" : std::to_string(state->corridor_owner).c_str(),
    state->collision_count, state->safe_mode ? "SEGURO" : "INSEGURO");
  pthread_mutex_unlock(&state->mutex);
}

void detect_collisions(FleetState * state, bool active_pairs[3][3])
{
  pthread_mutex_lock(&state->mutex);
  for (int first = 0; first < fleet_posix::kRobotCount; ++first) {
    for (int second = first + 1; second < fleet_posix::kRobotCount; ++second) {
      const auto & a = state->robots[first];
      const auto & b = state->robots[second];
      if (!a.online || !b.online) {
        continue;
      }
      const double distance = std::hypot(a.x - b.x, a.y - b.y);
      const bool colliding = distance < 0.55;
      if (colliding && !active_pairs[first][second]) {
        ++state->collision_count;
        std::printf(
          "[DETECTOR] colisao logica entre AMR %d e AMR %d em (%.2f, %.2f)\n",
          first + 1, second + 1, (a.x + b.x) / 2.0, (a.y + b.y) / 2.0);
      }
      active_pairs[first][second] = colliding;
    }
  }
  pthread_mutex_unlock(&state->mutex);
}

bool all_robots_online(FleetState * state)
{
  bool result = true;
  pthread_mutex_lock(&state->mutex);
  for (const auto & robot : state->robots) {
    result = result && robot.online;
  }
  pthread_mutex_unlock(&state->mutex);
  return result;
}

bool all_missions_complete(FleetState * state)
{
  bool result = true;
  pthread_mutex_lock(&state->mutex);
  for (const auto & robot : state->robots) {
    result = result && robot.completed_missions > 0;
  }
  pthread_mutex_unlock(&state->mutex);
  return result;
}

void run_demo(mqd_t queues[3], FleetState * state)
{
  std::printf("Aguardando os tres agentes criarem suas tartarugas...\n");
  for (int attempt = 0; attempt < 100 && !all_robots_online(state); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  if (!all_robots_online(state)) {
    std::fprintf(stderr, "Nem todos os AMRs ficaram online; encerrando demonstracao.\n");
    g_stop_requested = 1;
    return;
  }

  const float targets[3][2] = {{9.0F, 5.15F}, {5.5F, 9.0F}, {2.0F, 5.85F}};
  for (int index = 0; index < 3; ++index) {
    Command command{};
    command.type = CommandType::kMission;
    command.robot_id = static_cast<std::uint32_t>(index + 1);
    command.target_x = targets[index][0];
    command.target_y = targets[index][1];
    command.mission_id = static_cast<std::uint32_t>(index + 1);
    send_command(queues[index], command);
  }

  std::printf("Tres missoes simultaneas enviadas pelas filas POSIX.\n");
  bool collision_pairs[3][3]{};
  for (int tick = 0; tick < 240 && !g_stop_requested; ++tick) {
    detect_collisions(state, collision_pairs);
    if (tick % 25 == 0) {
      print_status(state);
    }
    if (tick > 20 && all_missions_complete(state)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  print_status(state);
}

void run_console(mqd_t queues[3], FleetState * state)
{
  std::printf(
    "Comandos: enviar <amr> <x> <y> | pausar <amr> | retomar <amr> | "
    "status | sair\n");
  std::uint32_t next_mission = 1;
  std::string line;
  while (!g_stop_requested && std::printf("fleet> ") && std::getline(std::cin, line)) {
    std::istringstream input(line);
    std::string action;
    input >> action;
    if (action == "status") {
      print_status(state);
      continue;
    }
    if (action == "sair") {
      break;
    }

    int robot = 0;
    input >> robot;
    if (robot < 1 || robot > 3) {
      std::printf("AMR deve estar entre 1 e 3.\n");
      continue;
    }

    Command command{};
    command.robot_id = static_cast<std::uint32_t>(robot);
    if (action == "enviar") {
      input >> command.target_x >> command.target_y;
      if (!input || command.target_x < 0.5F || command.target_x > 10.5F ||
        command.target_y < 0.5F || command.target_y > 10.5F)
      {
        std::printf("Destino invalido. Use coordenadas entre 0.5 e 10.5.\n");
        continue;
      }
      command.type = CommandType::kMission;
      command.mission_id = next_mission++;
    } else if (action == "pausar") {
      command.type = CommandType::kPause;
    } else if (action == "retomar") {
      command.type = CommandType::kResume;
    } else {
      std::printf("Comando desconhecido.\n");
      continue;
    }
    send_command(queues[robot - 1], command);
  }
}

}  // namespace

int main(int argc, char * argv[])
{
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  const bool demo_mode = argc > 1 && std::strcmp(argv[1], "--demo") == 0;
  const bool unsafe_mode =
    (argc > 1 && std::strcmp(argv[1], "--unsafe") == 0) ||
    (argc > 2 && std::strcmp(argv[2], "--unsafe") == 0);

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  const pid_t supervisor_pid = getpid();
  const std::string shm_name = "/fleet_posix_state_" + std::to_string(supervisor_pid);
  std::string queue_names[3];
  for (int index = 0; index < 3; ++index) {
    queue_names[index] =
      "/fleet_posix_cmd_" + std::to_string(supervisor_pid) + "_" + std::to_string(index + 1);
  }

  const int shm_fd = shm_open(shm_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (shm_fd == -1 || ftruncate(shm_fd, sizeof(FleetState)) == -1) {
    std::perror("shm_open/ftruncate");
    return EXIT_FAILURE;
  }
  auto * state = static_cast<FleetState *>(mmap(
      nullptr, sizeof(FleetState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
  close(shm_fd);
  if (state == MAP_FAILED) {
    std::perror("mmap");
    shm_unlink(shm_name.c_str());
    return EXIT_FAILURE;
  }
  std::memset(state, 0, sizeof(*state));

  pthread_mutexattr_t mutex_attributes;
  pthread_mutexattr_init(&mutex_attributes);
  pthread_mutexattr_setpshared(&mutex_attributes, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&state->mutex, &mutex_attributes);
  pthread_mutexattr_destroy(&mutex_attributes);
  sem_init(&state->corridor, 1, 1);
  state->safe_mode = unsafe_mode ? 0 : 1;

  mq_attr queue_attributes{};
  queue_attributes.mq_maxmsg = 10;
  queue_attributes.mq_msgsize = sizeof(Command);
  mqd_t queues[3];
  for (int index = 0; index < 3; ++index) {
    queues[index] = mq_open(
      queue_names[index].c_str(), O_CREAT | O_EXCL | O_RDWR, 0600, &queue_attributes);
    if (queues[index] == static_cast<mqd_t>(-1)) {
      std::perror("mq_open");
      return EXIT_FAILURE;
    }
  }

  int log_pipe[2];
  if (pipe(log_pipe) == -1) {
    std::perror("pipe");
    return EXIT_FAILURE;
  }

  const std::filesystem::path executable_dir =
    std::filesystem::canonical("/proc/self/exe").parent_path();
  const std::string agent_executable = (executable_dir / "amr_agent").string();
  const std::string turtlesim_executable =
    ament_index_cpp::get_package_prefix("turtlesim") + "/lib/turtlesim/turtlesim_node";

  std::printf(
    "FleetPOSIX supervisor pid=%d | modo=%s\n",
    supervisor_pid, state->safe_mode ? "SEGURO" : "INSEGURO");

  const pid_t turtlesim_pid = fork();
  if (turtlesim_pid == 0) {
    close(log_pipe[0]);
    close(log_pipe[1]);
    execl(turtlesim_executable.c_str(), turtlesim_executable.c_str(), nullptr);
    std::perror("exec turtlesim_node");
    _exit(127);
  }
  if (turtlesim_pid < 0) {
    std::perror("fork turtlesim");
    return EXIT_FAILURE;
  }

  pid_t agent_pids[3]{};
  for (int index = 0; index < 3; ++index) {
    agent_pids[index] = fork();
    if (agent_pids[index] == 0) {
      close(log_pipe[0]);
      const std::string id = std::to_string(index + 1);
      const std::string log_fd = std::to_string(log_pipe[1]);
      execl(
        agent_executable.c_str(), agent_executable.c_str(), "--id", id.c_str(),
        "--shm", shm_name.c_str(), "--queue", queue_names[index].c_str(),
        "--log-fd", log_fd.c_str(), nullptr);
      std::perror("exec amr_agent");
      _exit(127);
    }
    if (agent_pids[index] < 0) {
      std::perror("fork amr_agent");
      g_stop_requested = 1;
      break;
    }
  }

  close(log_pipe[1]);
  pthread_t logger{};
  pthread_create(&logger, nullptr, logger_thread, &log_pipe[0]);

  if (demo_mode) {
    run_demo(queues, state);
  } else {
    run_console(queues, state);
  }

  pthread_mutex_lock(&state->mutex);
  state->shutdown_requested = 1;
  pthread_mutex_unlock(&state->mutex);
  for (int index = 0; index < 3; ++index) {
    Command shutdown{};
    shutdown.type = CommandType::kShutdown;
    shutdown.robot_id = static_cast<std::uint32_t>(index + 1);
    send_command(queues[index], shutdown);
    sem_post(&state->corridor);
  }

  bool agents_ok = true;
  for (const pid_t pid : agent_pids) {
    if (pid <= 0) {
      agents_ok = false;
      continue;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      agents_ok = false;
    }
  }

  kill(turtlesim_pid, SIGTERM);
  waitpid(turtlesim_pid, nullptr, 0);
  close(log_pipe[0]);
  pthread_join(logger, nullptr);

  std::printf("Supervisor recolheu todos os processos com waitpid().\n");
  print_status(state);

  for (int index = 0; index < 3; ++index) {
    mq_close(queues[index]);
    mq_unlink(queue_names[index].c_str());
  }
  sem_destroy(&state->corridor);
  pthread_mutex_destroy(&state->mutex);
  munmap(state, sizeof(FleetState));
  shm_unlink(shm_name.c_str());

  std::exit(agents_ok ? EXIT_SUCCESS : EXIT_FAILURE);
}
