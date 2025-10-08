#pragma once

#include "barock/compositor.hpp"
#include "barock/core/surface.hpp"
#include "barock/resource.hpp"

#include <cstdint>
#include <map>
#include <wayland-server-core.h>

#define DECLARE_SURFACE_ROLE(name, ...)

namespace barock {
  struct xdg_shell_t;
  struct xdg_toplevel_t;

  enum class xdg_role_t { eToplevel, ePopup, eNone };

  struct xdg_surface_t : public surface_role_t<xdg_surface_t> {
    xdg_shell_t                    &shell;
    shared_t<resource_t<surface_t>> surface;
    xdg_role_t                      role;

    int32_t x, y, width, height;

    struct {
      union {
        xdg_toplevel_t *toplevel;
        void           *raw;
      };
    } as;

    xdg_surface_t(xdg_shell_t &parent, shared_t<resource_t<surface_t>> base);
  };

  class xdg_shell_t {
    private:
    compositor_t &compositor;

    public:
    wl_global *global;

    xdg_shell_t(compositor_t &);
    ~xdg_shell_t();

    static void
    handle_xdg_base_destroy(wl_client *, wl_resource *);
    static void
    handle_xdg_base_get_surface(wl_client *, wl_resource *, uint32_t, wl_resource *);
    static void
    xdg_surface_destroy(wl_resource *);

    private:
    static void
    bind(wl_client *, void *, uint32_t, uint32_t);
  };
}
