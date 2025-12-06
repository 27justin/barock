#pragma once

#include "barock/core/output_manager.hpp"
#include "wl/wayland-protocol.h"

extern struct wl_output_interface wl_output_impl;

namespace barock {
  struct compositor_t;
  struct service_registry_t;

  struct wl_output_t {
    public:
    static constexpr int VERSION = 4;
    wl_global           *wl_output_global;
    wl_display          *display;
    service_registry_t  &registry;

    wl_output_t(wl_display *, service_registry_t &registry);

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
