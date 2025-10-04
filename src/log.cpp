#include "log.hpp"

void
set_log_filter(log_level level) {
  filter = level;
}

log_level
get_log_filter() {
  return filter;
}
