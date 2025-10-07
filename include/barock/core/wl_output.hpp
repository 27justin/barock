#pragma once

#include "wl/wayland-protocol.h"

extern struct wl_output_interface wl_output_impl;

namespace barock {
  struct compositor_t;

  struct wl_output_t {
    public:
    static constexpr int VERSION = 4;
    wl_global           *wl_output_global;
    compositor_t        &compositor;

    wl_output_t(compositor_t &);

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
