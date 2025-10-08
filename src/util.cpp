#include "barock/util.hpp"
#include <ctime>

uint32_t
barock::current_time_msec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
