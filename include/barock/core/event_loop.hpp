#pragma once

#include <memory>
#include <vector>

#include <cstdint>
#include <wayland-server-core.h>

namespace barock {

  // Class managing the `wl_event_loop` instance, and tracking the
  // resources allocated by this.
  class event_loop_t {
    wl_event_loop                                                                   *event_loop_;
    std::vector<std::unique_ptr<wl_event_source, decltype(&wl_event_source_remove)>> sources_;

    public:
    event_loop_t(wl_event_loop *ev);

    void
    add_fd(int fd, uint32_t mask, int (*func)(int32_t, uint32_t, void *), void *ud);
  };

}
