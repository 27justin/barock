#pragma once

#include <wayland-server-core.h>

extern struct wl_shm_interface wl_shm_impl;

namespace barock {
  struct compositor_t;

  class shm_t {
    public:
    wl_global                *global;
    wl_display               *display;
    static constexpr uint32_t VERSION = 2;

    shm_t(wl_display *);
    ~shm_t();

    private:
    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
