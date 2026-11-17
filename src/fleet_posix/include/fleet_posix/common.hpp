#ifndef FLEET_POSIX__COMMON_HPP_
#define FLEET_POSIX__COMMON_HPP_

#include <cstdint>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>

namespace fleet_posix
{

constexpr int kRobotCount = 3;
constexpr int kCommandQueueCapacity = 4;
constexpr double kCorridorCenterX = 5.5;
constexpr double kCorridorCenterY = 5.5;
constexpr double kCorridorAcquireRadius = 2.35;
constexpr double kCorridorCoreRadius = 1.35;

enum class CommandType : std::uint32_t
{
  kMission = 1,
  kPause = 2,
  kResume = 3,
  kShutdown = 4,
};

enum class RobotMode : std::uint32_t
{
  kStarting = 0,
  kIdle = 1,
  kMoving = 2,
  kWaitingCorridor = 3,
  kInCorridor = 4,
  kPaused = 5,
  kStopped = 6,
};

struct Command
{
  CommandType type;
  std::uint32_t robot_id;
  float target_x;
  float target_y;
  std::uint32_t mission_id;
};

struct RobotState
{
  pid_t pid;
  int online;
  float x;
  float y;
  float theta;
  RobotMode mode;
  std::uint32_t current_mission;
  std::uint32_t completed_missions;
};

struct FleetState
{
  pthread_mutex_t mutex;
  sem_t corridor;
  int safe_mode;
  int shutdown_requested;
  int corridor_owner;
  std::uint32_t collision_count;
  RobotState robots[kRobotCount];
};

struct LogEvent
{
  pid_t pid;
  int robot_id;
  char text[192];
};

inline const char * mode_name(RobotMode mode)
{
  switch (mode) {
    case RobotMode::kStarting:
      return "iniciando";
    case RobotMode::kIdle:
      return "disponivel";
    case RobotMode::kMoving:
      return "em movimento";
    case RobotMode::kWaitingCorridor:
      return "aguardando corredor";
    case RobotMode::kInCorridor:
      return "no corredor";
    case RobotMode::kPaused:
      return "pausado";
    case RobotMode::kStopped:
      return "encerrado";
  }
  return "desconhecido";
}

}  // namespace fleet_posix

#endif  // FLEET_POSIX__COMMON_HPP_
