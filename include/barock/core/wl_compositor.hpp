#pragma once

#include "barock/core/surface.hpp"
#include <vector>
#include <wayland-server-core.h>

namespace barock {
  struct compositor_t;

  class wl_compositor_t {
    public:
    wl_global    *global;
    compositor_t &compositor;

    static constexpr uint32_t VERSION = 6;

    wl_compositor_t(compositor_t &);
    ~wl_compositor_t();

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
