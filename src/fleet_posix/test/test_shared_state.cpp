#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "fleet_posix/common.hpp"

int main()
{
  void * mapping = mmap(
    nullptr, sizeof(fleet_posix::FleetState), PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (mapping == MAP_FAILED) {
    return EXIT_FAILURE;
  }
  auto * state = static_cast<fleet_posix::FleetState *>(mapping);
  pthread_mutexattr_t attributes;
  if (pthread_mutexattr_init(&attributes) != 0) {
    return EXIT_FAILURE;
  }
  if (pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED) != 0 ||
    pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST) != 0 ||
    pthread_mutex_init(&state->mutex, &attributes) != 0)
  {
    return EXIT_FAILURE;
  }
  pthread_mutexattr_destroy(&attributes);
  const pid_t child = fork();
  if (child == 0) {
    fleet_posix::lock_fleet_state(state);
    state->shutdown_requested = 1;
    // Simula morte no meio da secao critica, sem pthread_mutex_unlock.
    _exit(EXIT_SUCCESS);
  }
  if (child < 0) {
    return EXIT_FAILURE;
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    return EXIT_FAILURE;
  }
  fleet_posix::lock_fleet_state(state);
  const bool recovered = state->shutdown_requested == 1;
  state->shutdown_requested = 0;
  pthread_mutex_unlock(&state->mutex);
  // Deve continuar utilizavel depois de pthread_mutex_consistent.
  fleet_posix::lock_fleet_state(state);
  const bool reusable = state->shutdown_requested == 0;
  pthread_mutex_unlock(&state->mutex);
  pthread_mutex_destroy(&state->mutex);
  munmap(state, sizeof(*state));
  std::printf("Mutex compartilhado recuperado apos morte do proprietario.\n");
  return recovered && reusable ? EXIT_SUCCESS : EXIT_FAILURE;
}
