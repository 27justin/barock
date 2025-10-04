#pragma once

#include "../compositor.hpp"
#include <wayland-server-core.h>

namespace barock {
  class xdg_shell_t {
  private:
    compositor_t &compositor;
  public:
    wl_global *global;

    xdg_shell_t(compositor_t &);
    ~xdg_shell_t();

    static void handle_xdg_base_destroy(wl_client *, wl_resource *);
    static void handle_xdg_base_get_surface(wl_client *, wl_resource *, uint32_t, wl_resource *);
    static void xdg_surface_destroy(wl_resource *);
  private:
    static void bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
