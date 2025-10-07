#pragma once

#include "wl/wayland-protocol.h"

extern struct wl_data_device_manager_interface wl_data_device_manager_impl;
extern struct wl_data_device_interface         wl_data_device_impl;

namespace barock {
  struct compositor_t;

  struct wl_data_device_manager_t {
    public:
    static constexpr int VERSION = 3;
    wl_global           *wl_data_device_manager_global;
    compositor_t        &compositor;

    wl_data_device_manager_t(compositor_t &);

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
