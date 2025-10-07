#pragma once

#include "wl/wayland-protocol.h"
#include <cstdint>
#include <vector>

extern struct wl_subcompositor_interface wl_subcompositor_impl;
extern struct wl_subsurface_interface    wl_subsurface_impl;

namespace barock {
  struct compositor_t;
  struct surface_t;

  struct subsurface_t {
    surface_t *parent;
    surface_t *surface;
    int32_t    x, y;
  };

  struct wl_subcompositor_t {
    public:
    wl_global           *wl_subcompositor_global;
    compositor_t        &compositor;
    static constexpr int VERSION = 1;

    wl_subcompositor_t(compositor_t &);

    static void
    bind(wl_client *, void *, uint32_t version, uint32_t id);
  };
}
