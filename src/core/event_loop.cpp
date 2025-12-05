#include "barock/core/event_loop.hpp"

using namespace barock;

event_loop_t::event_loop_t(wl_event_loop *ev)
  : event_loop_(ev) {}

void
event_loop_t::add_fd(int fd, uint32_t mask, int (*func)(int32_t, uint32_t, void *), void *ud) {
  sources_.emplace_back(wl_event_loop_add_fd(event_loop_, fd, mask, func, ud),
                        wl_event_source_remove);
}
