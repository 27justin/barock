#pragma once

#include "barock/core/wl_compositor.hpp"
#include "barock/resource.hpp"

#include "wl/wayland-protocol.h"
#include <cstdint>
#include <vector>

extern struct wl_subcompositor_interface wl_subcompositor_impl;
extern struct wl_subsurface_interface    wl_subsurface_impl;

namespace barock {
  struct compositor_t;
  struct surface_t;
  struct service_registry_t;

  struct subsurface_t {
    ipoint_t          position;
    weak_t<surface_t> surface, parent;
  };

  struct wl_subcompositor_t {
    public:
    wl_global           *wl_subcompositor_global;
    wl_display          *display;
    service_registry_t  &registry;
    static constexpr int VERSION = 1;

    wl_subcompositor_t(wl_display *, service_registry_t &registry);

    static void
    bind(wl_client *, void *, uint32_t version, uint32_t id);
  };
}
