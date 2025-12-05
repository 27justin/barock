#pragma once

#include "barock/core/output_manager.hpp"
#include "wl/wayland-protocol.h"

extern struct wl_output_interface wl_output_impl;

namespace barock {
  struct compositor_t;

  struct wl_output_t {
    public:
    static constexpr int VERSION = 4;
    wl_global           *wl_output_global;
    wl_display          *display;
    output_manager_t    &output_manager;

    wl_output_t(wl_display *, output_manager_t &);

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
