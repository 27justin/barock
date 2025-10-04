#include "wl/wayland-protocol.h"
#include <atomic>

extern struct wl_surface_interface wl_surface_impl;

namespace barock {
  struct surface_t {
    wl_resource *compositor, *buffer, *callback;
    std::atomic<bool> is_dirty;
  };
};
