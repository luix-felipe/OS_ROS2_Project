#include <geometry_msgs/msg/twist.hpp>
#include <rclcpp/rclcpp.hpp>
#include <turtlesim/msg/pose.hpp>
#include <turtlesim/srv/kill.hpp>
#include <turtlesim/srv/spawn.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fleet_posix/common.hpp"

using namespace std::chrono_literals;

namespace
{

volatile sig_atomic_t g_stop_requested = 0;

void signal_handler(int)
{
  g_stop_requested = 1;
}

timespec deadline_after(long nanoseconds)
{
  timespec deadline{};
  clock_gettime(CLOCK_REALTIME, &deadline);
  deadline.tv_nsec += nanoseconds;
  if (deadline.tv_nsec >= 1000000000) {
    ++deadline.tv_sec;
    deadline.tv_nsec -= 1000000000;
  }
  return deadline;
}

double normalize_angle(double angle)
{
  constexpr double pi = 3.14159265358979323846;
  while (angle > pi) {
    angle -= 2.0 * pi;
  }
  while (angle < -pi) {
    angle += 2.0 * pi;
  }
  return angle;
}

class AmrAgent : public rclcpp::Node
{
public:
  AmrAgent(int id, fleet_posix::FleetState * fleet, mqd_t command_queue, int log_fd)
  : Node("amr_agent_" + std::to_string(id)),
    id_(id), fleet_(fleet), command_queue_(command_queue), log_fd_(log_fd),
    turtle_name_("amr" + std::to_string(id))
  {
    if (sem_init(&command_buffer_.free_slots, 0, fleet_posix::kCommandQueueCapacity) == -1) {
      throw std::runtime_error("sem_init free_slots");
    }
    if (sem_init(&command_buffer_.used_slots, 0, 0) == -1) {
      sem_destroy(&command_buffer_.free_slots);
      throw std::runtime_error("sem_init used_slots");
    }

    velocity_publisher_ = create_publisher<geometry_msgs::msg::Twist>(
      "/" + turtle_name_ + "/cmd_vel", 10);
    pose_subscription_ = create_subscription<turtlesim::msg::Pose>(
      "/" + turtle_name_ + "/pose", 10,
      [this](const turtlesim::msg::Pose & pose) {pose_callback(pose);});
    spawn_client_ = create_client<turtlesim::srv::Spawn>("/spawn");
    kill_client_ = create_client<turtlesim::srv::Kill>("/kill");
  }

  ~AmrAgent() override
  {
    sem_destroy(&command_buffer_.free_slots);
    sem_destroy(&command_buffer_.used_slots);
    pthread_mutex_destroy(&command_buffer_.mutex);
    pthread_cond_destroy(&pose_changed_);
    pthread_mutex_destroy(&pose_mutex_);
    pthread_mutex_destroy(&motion_mutex_);
  }

  bool spawn_turtle()
  {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!shutdown_requested() && !spawn_client_->wait_for_service(100ms)) {
      if (std::chrono::steady_clock::now() >= deadline) {
        log("prazo excedido ao aguardar servico /spawn");
        return false;
      }
    }
    if (shutdown_requested()) {
      return false;
    }

    if (id_ == 1 && kill_client_->wait_for_service(100ms)) {
      auto kill_request = std::make_shared<turtlesim::srv::Kill::Request>();
      kill_request->name = "turtle1";
      const auto future = kill_client_->async_send_request(kill_request);
      rclcpp::spin_until_future_complete(shared_from_this(), future, 2s);
    }

    constexpr float positions[3][3] = {
      {2.0F, 5.15F, 0.0F},
      {5.5F, 2.0F, 1.5708F},
      {9.0F, 5.85F, 3.1416F},
    };
    auto request = std::make_shared<turtlesim::srv::Spawn::Request>();
    request->x = positions[id_ - 1][0];
    request->y = positions[id_ - 1][1];
    request->theta = positions[id_ - 1][2];
    request->name = turtle_name_;
    const auto future = spawn_client_->async_send_request(request);
    const auto result = rclcpp::spin_until_future_complete(shared_from_this(), future, 5s);
    if (result != rclcpp::FutureReturnCode::SUCCESS) {
      log("falha ao criar tartaruga");
      return false;
    }

    if (shutdown_requested()) {
      return false;
    }
    fleet_posix::lock_fleet_state(fleet_);
    auto & robot = fleet_->robots[id_ - 1];
    robot.pid = getpid();
    robot.online = 1;
    robot.x = positions[id_ - 1][0];
    robot.y = positions[id_ - 1][1];
    robot.theta = positions[id_ - 1][2];
    robot.mode = fleet_posix::RobotMode::kIdle;
    pthread_mutex_unlock(&fleet_->mutex);
    log("online e pronto para receber missoes");
    return true;
  }

  int run()
  {
    pthread_t threads[3]{};
    void * (*entries[3])(void *) = {
      ros_thread_entry, command_thread_entry, control_thread_entry};
    int started = 0;
    for (; started < 3; ++started) {
      const int result = pthread_create(&threads[started], nullptr, entries[started], this);
      if (result != 0) {
        errno = result;
        std::perror("pthread_create agente");
        failed_.store(true);
        request_stop();
        break;
      }
    }
    if (started >= 2) {
      pthread_join(threads[1], nullptr);
    }
    request_stop();
    if (started >= 3) {
      pthread_join(threads[2], nullptr);
    }
    publish_stop();
    fleet_posix::lock_fleet_state(fleet_);
    auto & robot = fleet_->robots[id_ - 1];
    robot.online = 0;
    robot.current_mission = 0;
    robot.mode = fleet_posix::RobotMode::kStopped;
    pthread_mutex_unlock(&fleet_->mutex);
    rclcpp::shutdown();
    if (started >= 1) {
      pthread_join(threads[0], nullptr);
    }
    log("threads finalizadas com pthread_join");
    return failed_.load() ? EXIT_FAILURE : EXIT_SUCCESS;
  }

private:
  struct PoseSnapshot
  {
    float x;
    float y;
    float theta;
    std::uint64_t generation;
    bool valid;
  };

  struct CommandBuffer
  {
    fleet_posix::Command commands[fleet_posix::kCommandQueueCapacity];
    std::size_t head{0};
    std::size_t tail{0};
    sem_t free_slots;
    sem_t used_slots;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  };

  static void * ros_thread_entry(void * argument)
  {
    auto * self = static_cast<AmrAgent *>(argument);
    try {
      rclcpp::spin(self->shared_from_this());
    } catch (const std::exception & error) {
      self->log(error.what());
      self->failed_.store(true);
      self->request_stop();
    }
    return nullptr;
  }

  static void * command_thread_entry(void * argument)
  {
    static_cast<AmrAgent *>(argument)->receive_commands();
    return nullptr;
  }

  static void * control_thread_entry(void * argument)
  {
    static_cast<AmrAgent *>(argument)->control_loop();
    return nullptr;
  }

  void pose_callback(const turtlesim::msg::Pose & pose)
  {
    pthread_mutex_lock(&pose_mutex_);
    pose_.x = pose.x;
    pose_.y = pose.y;
    pose_.theta = pose.theta;
    pose_.valid = true;
    ++pose_.generation;
    pthread_cond_broadcast(&pose_changed_);
    pthread_mutex_unlock(&pose_mutex_);

    fleet_posix::lock_fleet_state(fleet_);
    auto & robot = fleet_->robots[id_ - 1];
    robot.x = pose.x;
    robot.y = pose.y;
    robot.theta = pose.theta;
    pthread_mutex_unlock(&fleet_->mutex);
  }

  bool is_running() const
  {
    if (!running_.load() || g_stop_requested || !rclcpp::ok()) {
      return false;
    }
    fleet_posix::lock_fleet_state(fleet_);
    const bool shutdown = fleet_->shutdown_requested != 0;
    pthread_mutex_unlock(&fleet_->mutex);
    return !shutdown;
  }

  bool shutdown_requested() const
  {
    return !is_running();
  }

  void request_stop()
  {
    if (running_.exchange(false)) {
      // Acorda o consumidor ocioso; shutdown nao depende de espaco no buffer.
      sem_post(&command_buffer_.used_slots);
      pthread_mutex_lock(&pose_mutex_);
      pthread_cond_broadcast(&pose_changed_);
      pthread_mutex_unlock(&pose_mutex_);
    }
  }

  void receive_commands()
  {
    while (!shutdown_requested()) {
      pollfd descriptor{static_cast<int>(command_queue_), POLLIN, 0};
      const int ready = poll(&descriptor, 1, 100);
      if (ready == -1 && errno == EINTR) {
        continue;
      }
      if (ready < 0 || (descriptor.revents & (POLLERR | POLLNVAL))) {
        log("erro ao aguardar fila POSIX");
        failed_.store(true);
        break;
      }
      if (ready == 0) {
        continue;
      }
      fleet_posix::Command command{};
      const ssize_t received = mq_receive(
        command_queue_, reinterpret_cast<char *>(&command), sizeof(command), nullptr);
      if (received < 0 && (errno == EINTR || errno == EAGAIN)) {
        continue;
      }
      if (received != static_cast<ssize_t>(sizeof(command))) {
        log("erro ou tamanho invalido em mq_receive");
        failed_.store(true);
        break;
      }
      if (command.robot_id != static_cast<std::uint32_t>(id_)) {
        log("comando destinado a outro AMR recusado");
        continue;
      }
      if (command.type == fleet_posix::CommandType::kShutdown) {
        break;
      }
      if (command.type == fleet_posix::CommandType::kPause ||
        command.type == fleet_posix::CommandType::kResume)
      {
        const bool pause = command.type == fleet_posix::CommandType::kPause;
        pthread_mutex_lock(&motion_mutex_);
        paused_.store(pause);
        if (pause && rclcpp::ok()) {
          velocity_publisher_->publish(geometry_msgs::msg::Twist());
        }
        pthread_mutex_unlock(&motion_mutex_);
        fleet_posix::lock_fleet_state(fleet_);
        auto & robot = fleet_->robots[id_ - 1];
        robot.mode = pause ? fleet_posix::RobotMode::kPaused :
          (robot.current_mission == 0 ? fleet_posix::RobotMode::kIdle :
          (fleet_->corridor_owner == id_ ? fleet_posix::RobotMode::kInCorridor :
          fleet_posix::RobotMode::kMoving));
        pthread_mutex_unlock(&fleet_->mutex);
        log(pause ? "pausado pelo operador" : "retomado pelo operador");
        continue;
      }
      if (command.type == fleet_posix::CommandType::kMission) {
        enqueue(command);
      }
    }
    request_stop();
  }

  void enqueue(const fleet_posix::Command & command)
  {
    int result;
    do {
      result = sem_trywait(&command_buffer_.free_slots);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
      if (errno == EAGAIN) {
        log("buffer de missoes cheio: missao recusada");
      } else {
        failed_.store(true);
        request_stop();
      }
      return;
    }
    pthread_mutex_lock(&command_buffer_.mutex);
    command_buffer_.commands[command_buffer_.tail] = command;
    command_buffer_.tail =
      (command_buffer_.tail + 1) % fleet_posix::kCommandQueueCapacity;
    pthread_mutex_unlock(&command_buffer_.mutex);
    sem_post(&command_buffer_.used_slots);
  }

  bool dequeue(fleet_posix::Command & command)
  {
    int result;
    do {
      result = sem_wait(&command_buffer_.used_slots);
    } while (result == -1 && errno == EINTR && is_running());
    if (!is_running()) {
      return false;
    }
    if (result == -1) {
      failed_.store(true);
      request_stop();
      return false;
    }
    pthread_mutex_lock(&command_buffer_.mutex);
    command = command_buffer_.commands[command_buffer_.head];
    command_buffer_.head =
      (command_buffer_.head + 1) % fleet_posix::kCommandQueueCapacity;
    pthread_mutex_unlock(&command_buffer_.mutex);
    sem_post(&command_buffer_.free_slots);
    return true;
  }

  void control_loop()
  {
    fleet_posix::Command command{};
    while (dequeue(command)) {
      if (is_running() && command.type == fleet_posix::CommandType::kMission) {
        execute_mission(command);
      }
    }
  }

  void execute_mission(const fleet_posix::Command & command)
  {
    fleet_posix::lock_fleet_state(fleet_);
    fleet_->robots[id_ - 1].current_mission = command.mission_id;
    pthread_mutex_unlock(&fleet_->mutex);
    set_mode(fleet_posix::RobotMode::kMoving);
    log("missao recebida pela fila POSIX");

    bool corridor_held = false;
    bool entered_core = false;
    std::uint64_t seen_generation = 0;
    while (is_running()) {
      const PoseSnapshot pose = wait_for_pose(seen_generation);
      if (!pose.valid) {
        continue;
      }
      seen_generation = pose.generation;
      if (paused_.load()) {
        publish_stop();
        continue;
      }
      const double goal_distance = std::hypot(command.target_x - pose.x, command.target_y - pose.y);
      const double center_distance = std::hypot(
        fleet_posix::kCorridorCenterX - pose.x,
        fleet_posix::kCorridorCenterY - pose.y);

      if (fleet_->safe_mode && !corridor_held &&
        center_distance < fleet_posix::kCorridorAcquireRadius)
      {
        publish_stop();
        set_mode(fleet_posix::RobotMode::kWaitingCorridor);
        log("aguardando sem_wait do corredor");
        bool acquired = false;
        while (is_running() && !paused_.load()) {
          const timespec deadline = deadline_after(100000000);
          if (sem_timedwait(&fleet_->corridor, &deadline) == 0) {
            acquired = true;
            break;
          }
          if (errno != EINTR && errno != ETIMEDOUT) {
            failed_.store(true);
            request_stop();
            break;
          }
        }
        if (!acquired) {
          continue;
        }
        if (!is_running() || paused_.load()) {
          sem_post(&fleet_->corridor);
          continue;
        }
        corridor_held = true;
        fleet_posix::lock_fleet_state(fleet_);
        fleet_->corridor_owner = id_;
        pthread_mutex_unlock(&fleet_->mutex);
        set_mode(fleet_posix::RobotMode::kInCorridor);
        log("adquiriu o semaforo do corredor");
      }

      if (corridor_held && center_distance < fleet_posix::kCorridorCoreRadius) {
        entered_core = true;
      }
      if (corridor_held && entered_core &&
        center_distance > fleet_posix::kCorridorAcquireRadius)
      {
        release_corridor();
        corridor_held = false;
        set_mode(fleet_posix::RobotMode::kMoving);
      }

      if (goal_distance < 0.22) {
        publish_stop();
        if (corridor_held) {
          release_corridor();
        }
        fleet_posix::lock_fleet_state(fleet_);
        auto & robot = fleet_->robots[id_ - 1];
        ++robot.completed_missions;
        robot.current_mission = 0;
        robot.mode = fleet_posix::RobotMode::kIdle;
        pthread_mutex_unlock(&fleet_->mutex);
        log("missao concluida");
        return;
      }

      const double desired_heading = std::atan2(command.target_y - pose.y,
          command.target_x - pose.x);
      const double heading_error = normalize_angle(desired_heading - pose.theta);
      geometry_msgs::msg::Twist velocity;
      velocity.linear.x = std::abs(heading_error) < 0.55 ? 1.8 : 0.0;
      velocity.angular.z = std::clamp(4.0 * heading_error, -3.0, 3.0);
      pthread_mutex_lock(&motion_mutex_);
      if (is_running() && !paused_.load()) {
        velocity_publisher_->publish(velocity);
      }
      pthread_mutex_unlock(&motion_mutex_);
    }

    publish_stop();
    if (corridor_held) {
      release_corridor();
    }
  }

  PoseSnapshot wait_for_pose(std::uint64_t previous_generation)
  {
    pthread_mutex_lock(&pose_mutex_);
    while (is_running() && pose_.generation == previous_generation) {
      const timespec deadline = deadline_after(200000000);
      const int result = pthread_cond_timedwait(&pose_changed_, &pose_mutex_, &deadline);
      if (result == ETIMEDOUT) {
        pthread_mutex_unlock(&pose_mutex_);
        return PoseSnapshot{};
      }
    }
    const PoseSnapshot result = pose_;
    pthread_mutex_unlock(&pose_mutex_);
    return result;
  }

  void release_corridor()
  {
    fleet_posix::lock_fleet_state(fleet_);
    if (fleet_->corridor_owner == id_) {
      fleet_->corridor_owner = 0;
    }
    pthread_mutex_unlock(&fleet_->mutex);
    sem_post(&fleet_->corridor);
    log("liberou o corredor com sem_post");
  }

  void set_mode(fleet_posix::RobotMode mode)
  {
    fleet_posix::lock_fleet_state(fleet_);
    fleet_->robots[id_ - 1].mode = paused_.load() ? fleet_posix::RobotMode::kPaused : mode;
    pthread_mutex_unlock(&fleet_->mutex);
  }

  void publish_stop()
  {
    pthread_mutex_lock(&motion_mutex_);
    if (rclcpp::ok()) {
      velocity_publisher_->publish(geometry_msgs::msg::Twist());
    }
    pthread_mutex_unlock(&motion_mutex_);
  }

  void log(const char * text)
  {
    fleet_posix::LogEvent event{};
    event.pid = getpid();
    event.robot_id = id_;
    std::snprintf(event.text, sizeof(event.text), "%s", text);
    const auto * bytes = reinterpret_cast<const unsigned char *>(&event);
    std::size_t remaining = sizeof(event);
    while (remaining > 0) {
      const ssize_t written = write(log_fd_, bytes + sizeof(event) - remaining, remaining);
      if (written < 0 && errno == EINTR) {
        continue;
      }
      if (written <= 0) {
        break;
      }
      remaining -= static_cast<std::size_t>(written);
    }
  }

  int id_;
  fleet_posix::FleetState * fleet_;
  mqd_t command_queue_;
  int log_fd_;
  std::string turtle_name_;
  std::atomic<bool> running_{true};
  std::atomic<bool> paused_{false};
  std::atomic<bool> failed_{false};
  pthread_mutex_t pose_mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_t motion_mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t pose_changed_ = PTHREAD_COND_INITIALIZER;
  PoseSnapshot pose_{};
  CommandBuffer command_buffer_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr velocity_publisher_;
  rclcpp::Subscription<turtlesim::msg::Pose>::SharedPtr pose_subscription_;
  rclcpp::Client<turtlesim::srv::Spawn>::SharedPtr spawn_client_;
  rclcpp::Client<turtlesim::srv::Kill>::SharedPtr kill_client_;
};

bool parse_arguments(
  int argc, char * argv[], int * id, std::string * shm_name,
  std::string * queue_name, int * log_fd)
{
  if (argc % 2 == 0) {
    return false;
  }
  try {
    for (int index = 1; index + 1 < argc; index += 2) {
      const std::string option = argv[index];
      if (option == "--id") {
        *id = std::stoi(argv[index + 1]);
      } else if (option == "--shm") {
        *shm_name = argv[index + 1];
      } else if (option == "--queue") {
        *queue_name = argv[index + 1];
      } else if (option == "--log-fd") {
        *log_fd = std::stoi(argv[index + 1]);
      } else {
        return false;
      }
    }
  } catch (const std::exception &) {
    return false;
  }
  return *id >= 1 && *id <= 3 && !shm_name->empty() && !queue_name->empty() && *log_fd >= 0;
}

}  // namespace

int main(int argc, char * argv[])
{
  int id = 0;
  int log_fd = -1;
  std::string shm_name;
  std::string queue_name;
  if (!parse_arguments(argc, argv, &id, &shm_name, &queue_name, &log_fd)) {
    std::fprintf(stderr, "Argumentos internos do agente invalidos.\n");
    return EXIT_FAILURE;
  }

  const int shm_fd = shm_open(shm_name.c_str(), O_RDWR, 0);
  if (shm_fd == -1) {
    std::perror("agent shm_open");
    return EXIT_FAILURE;
  }
  auto * fleet = static_cast<fleet_posix::FleetState *>(mmap(
      nullptr, sizeof(fleet_posix::FleetState), PROT_READ | PROT_WRITE,
      MAP_SHARED, shm_fd, 0));
  close(shm_fd);
  if (fleet == MAP_FAILED) {
    std::perror("agent mmap");
    return EXIT_FAILURE;
  }

  const mqd_t queue = mq_open(queue_name.c_str(), O_RDONLY | O_NONBLOCK);
  if (queue == static_cast<mqd_t>(-1)) {
    std::perror("agent mq_open");
    munmap(fleet, sizeof(*fleet));
    return EXIT_FAILURE;
  }

  struct sigaction action{};
  action.sa_handler = signal_handler;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  std::signal(SIGPIPE, SIG_IGN);

  int result = EXIT_FAILURE;
  std::shared_ptr<AmrAgent> agent;
  try {
    rclcpp::init(argc, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
    agent = std::make_shared<AmrAgent>(id, fleet, queue, log_fd);
    result = agent->spawn_turtle() ? agent->run() : EXIT_FAILURE;
  } catch (const std::exception & error) {
    std::fprintf(stderr, "Falha no AMR %d: %s\n", id, error.what());
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  agent.reset();
  mq_close(queue);
  close(log_fd);
  munmap(fleet, sizeof(*fleet));
  std::exit(result);
}
