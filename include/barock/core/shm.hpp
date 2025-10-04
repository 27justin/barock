#pragma once

#include <wayland-server-core.h>

extern struct wl_shm_pool_interface wl_shm_pool_impl;

namespace barock {
  class shm_t {
  public:
    wl_global *global;
    static constexpr uint32_t VERSION = 2;

    shm_t(wl_display *);
    ~shm_t();

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);

    static void
    handle_create_pool(wl_client *, wl_resource *, uint32_t, int32_t, int32_t);

    static void
    handle_release(wl_client *, wl_resource *);
  };
}
