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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <semaphore.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "fleet_posix/common.hpp"

using namespace std::chrono_literals;

namespace
{

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
    pthread_mutex_init(&pose_mutex_, nullptr);
    pthread_cond_init(&pose_changed_, nullptr);
    pthread_mutex_init(&command_buffer_.mutex, nullptr);
    sem_init(&command_buffer_.free_slots, 0, fleet_posix::kCommandQueueCapacity);
    sem_init(&command_buffer_.used_slots, 0, 0);

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
  }

  bool spawn_turtle()
  {
    while (rclcpp::ok() && !spawn_client_->wait_for_service(500ms)) {
      log("aguardando servico /spawn do Turtlesim");
    }
    if (!rclcpp::ok()) {
      return false;
    }

    if (id_ == 1 && kill_client_->wait_for_service(2s)) {
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

    pthread_mutex_lock(&fleet_->mutex);
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
    pthread_t ros_thread{};
    pthread_t command_thread{};
    pthread_t control_thread{};
    pthread_create(&ros_thread, nullptr, ros_thread_entry, this);
    pthread_create(&command_thread, nullptr, command_thread_entry, this);
    pthread_create(&control_thread, nullptr, control_thread_entry, this);

    pthread_join(command_thread, nullptr);
    pthread_join(control_thread, nullptr);
    rclcpp::shutdown();
    pthread_join(ros_thread, nullptr);

    publish_stop();
    set_mode(fleet_posix::RobotMode::kStopped);
    log("threads finalizadas com pthread_join");
    return EXIT_SUCCESS;
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
    pthread_mutex_t mutex;
  };

  static void * ros_thread_entry(void * argument)
  {
    auto * self = static_cast<AmrAgent *>(argument);
    rclcpp::spin(self->shared_from_this());
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

    pthread_mutex_lock(&fleet_->mutex);
    auto & robot = fleet_->robots[id_ - 1];
    robot.x = pose.x;
    robot.y = pose.y;
    robot.theta = pose.theta;
    pthread_mutex_unlock(&fleet_->mutex);
  }

  void receive_commands()
  {
    while (running_.load()) {
      fleet_posix::Command command{};
      const ssize_t received = mq_receive(
        command_queue_, reinterpret_cast<char *>(&command), sizeof(command), nullptr);
      if (received < 0 && errno == EINTR) {
        continue;
      }
      if (received < 0) {
        log("erro em mq_receive");
        break;
      }

      if (command.type == fleet_posix::CommandType::kPause) {
        paused_.store(true);
        set_mode(fleet_posix::RobotMode::kPaused);
        publish_stop();
        log("pausado pelo operador");
        continue;
      }
      if (command.type == fleet_posix::CommandType::kResume) {
        paused_.store(false);
        log("retomado pelo operador");
        continue;
      }

      enqueue(command);
      if (command.type == fleet_posix::CommandType::kShutdown) {
        break;
      }
    }
  }

  void enqueue(const fleet_posix::Command & command)
  {
    while (sem_wait(&command_buffer_.free_slots) == -1 && errno == EINTR) {}
    pthread_mutex_lock(&command_buffer_.mutex);
    command_buffer_.commands[command_buffer_.tail] = command;
    command_buffer_.tail =
      (command_buffer_.tail + 1) % fleet_posix::kCommandQueueCapacity;
    pthread_mutex_unlock(&command_buffer_.mutex);
    sem_post(&command_buffer_.used_slots);
  }

  fleet_posix::Command dequeue()
  {
    while (sem_wait(&command_buffer_.used_slots) == -1 && errno == EINTR) {}
    pthread_mutex_lock(&command_buffer_.mutex);
    const auto command = command_buffer_.commands[command_buffer_.head];
    command_buffer_.head =
      (command_buffer_.head + 1) % fleet_posix::kCommandQueueCapacity;
    pthread_mutex_unlock(&command_buffer_.mutex);
    sem_post(&command_buffer_.free_slots);
    return command;
  }

  void control_loop()
  {
    while (running_.load()) {
      const auto command = dequeue();
      if (command.type == fleet_posix::CommandType::kShutdown) {
        running_.store(false);
        pthread_cond_broadcast(&pose_changed_);
        break;
      }
      if (command.type == fleet_posix::CommandType::kMission) {
        execute_mission(command);
      }
    }
  }

  void execute_mission(const fleet_posix::Command & command)
  {
    pthread_mutex_lock(&fleet_->mutex);
    fleet_->robots[id_ - 1].current_mission = command.mission_id;
    pthread_mutex_unlock(&fleet_->mutex);
    set_mode(fleet_posix::RobotMode::kMoving);
    log("missao recebida pela fila POSIX");

    bool corridor_held = false;
    bool entered_core = false;
    std::uint64_t seen_generation = 0;
    while (running_.load()) {
      if (paused_.load()) {
        publish_stop();
        std::this_thread::sleep_for(100ms);
        continue;
      }

      const PoseSnapshot pose = wait_for_pose(seen_generation);
      if (!pose.valid) {
        continue;
      }
      seen_generation = pose.generation;
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
        while (sem_wait(&fleet_->corridor) == -1 && errno == EINTR) {}
        if (!running_.load()) {
          break;
        }
        corridor_held = true;
        pthread_mutex_lock(&fleet_->mutex);
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
        pthread_mutex_lock(&fleet_->mutex);
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
      velocity_publisher_->publish(velocity);
    }

    publish_stop();
    if (corridor_held) {
      release_corridor();
    }
  }

  PoseSnapshot wait_for_pose(std::uint64_t previous_generation)
  {
    pthread_mutex_lock(&pose_mutex_);
    while (running_.load() && pose_.generation == previous_generation) {
      timespec deadline{};
      clock_gettime(CLOCK_REALTIME, &deadline);
      deadline.tv_nsec += 200000000;
      if (deadline.tv_nsec >= 1000000000) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000;
      }
      pthread_cond_timedwait(&pose_changed_, &pose_mutex_, &deadline);
    }
    const PoseSnapshot result = pose_;
    pthread_mutex_unlock(&pose_mutex_);
    return result;
  }

  void release_corridor()
  {
    pthread_mutex_lock(&fleet_->mutex);
    if (fleet_->corridor_owner == id_) {
      fleet_->corridor_owner = 0;
    }
    pthread_mutex_unlock(&fleet_->mutex);
    sem_post(&fleet_->corridor);
    log("liberou o corredor com sem_post");
  }

  void set_mode(fleet_posix::RobotMode mode)
  {
    pthread_mutex_lock(&fleet_->mutex);
    fleet_->robots[id_ - 1].mode = mode;
    pthread_mutex_unlock(&fleet_->mutex);
  }

  void publish_stop()
  {
    velocity_publisher_->publish(geometry_msgs::msg::Twist());
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
  pthread_mutex_t pose_mutex_;
  pthread_cond_t pose_changed_;
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

  const mqd_t queue = mq_open(queue_name.c_str(), O_RDONLY);
  if (queue == static_cast<mqd_t>(-1)) {
    std::perror("agent mq_open");
    munmap(fleet, sizeof(*fleet));
    return EXIT_FAILURE;
  }

  rclcpp::init(argc, argv);
  auto agent = std::make_shared<AmrAgent>(id, fleet, queue, log_fd);
  const int result = agent->spawn_turtle() ? agent->run() : EXIT_FAILURE;
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  agent.reset();
  mq_close(queue);
  close(log_fd);
  munmap(fleet, sizeof(*fleet));
  std::exit(result);
}
