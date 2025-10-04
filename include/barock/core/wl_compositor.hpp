#pragma once

#include <wayland-server-core.h>
#include <vector>

namespace barock {
  struct surface_t;

  class wl_compositor_t {
  public:
    wl_global *global;
    static constexpr uint32_t VERSION = 6;
    std::vector<barock::surface_t *> surfaces;

    wl_compositor_t(wl_display *);
    ~wl_compositor_t();

    static void
    bind(wl_client *, void *, uint32_t, uint32_t);

    static void
    handle_create_surface(wl_client *, wl_resource *, uint32_t);

    static void
    handle_create_region(wl_client *, wl_resource *, uint32_t);
  };
}
