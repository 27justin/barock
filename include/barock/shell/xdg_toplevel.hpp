#pragma once

#include "wl/xdg-shell-protocol.h"

#include "barock/core/surface.hpp"
#include <string>

extern struct xdg_toplevel_interface xdg_toplevel_impl;

namespace barock {
  struct xdg_surface_t;

  struct xdg_toplevel_data_t {
    std::string title, app_id;
    int         x, y, width, height;
  };

  struct xdg_toplevel_t : public surface_role_t<xdg_toplevel_t> {
    public:
    xdg_toplevel_data_t data;
    xdg_surface_t      &surface;

    signal_token_t on_buffer_attached;

    xdg_toplevel_t(xdg_surface_t &, const xdg_toplevel_data_t &data);
    ~xdg_toplevel_t();
  };

}
