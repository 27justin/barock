#pragma once

#include "wl/xdg-shell-protocol.h"
#include <string>

#include "barock/core/signal.hpp"
#include "barock/core/surface.hpp"
#include "barock/shell/xdg_wm_base.hpp"

extern struct xdg_toplevel_interface xdg_toplevel_impl;

namespace barock {
  struct xdg_surface_t;

  struct xdg_toplevel_data_t {
    std::string title, app_id;
    int         x, y, width, height;
  };

  struct xdg_toplevel_t : public xdg_base_role_t {
    public:
    xdg_toplevel_data_t               data;
    weak_t<resource_t<xdg_surface_t>> xdg_surface;

    signal_token_t on_buffer_attach;

    xdg_toplevel_t(shared_t<resource_t<xdg_surface_t>>, const xdg_toplevel_data_t &data);
    ~xdg_toplevel_t();
  };

}
