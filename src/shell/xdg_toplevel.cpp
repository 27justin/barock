#include "barock/shell/xdg_toplevel.hpp"
#include "../log.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/shell/xdg_wm_base.hpp"

namespace barock {
  xdg_toplevel_t::xdg_toplevel_t(xdg_surface_t *base, const xdg_toplevel_data_t &prop_data)
    : surface(base)
    , data(prop_data) {
    base->role        = xdg_role_t::eToplevel;
    base->as.toplevel = this;

    // Attach on_buffer_attach listener to resize the window
    on_buffer_attached =
      base->surface->get()->on_buffer_attached.connect([&](const shm_buffer_t &buf) {
        // TODO: We should only do this once, right now it auto resizes
        // to always match the buffer contents.
        data.width  = buf.width;
        data.height = buf.height;
      });
  }

  xdg_toplevel_t::~xdg_toplevel_t() {
    if (surface != nullptr) {
      surface->surface->get()->on_buffer_attached.disconnect(on_buffer_attached);
    }
  }
}

void
set_title(wl_client *client, wl_resource *res, const char *title) {
  INFO("set_title \"{}\"", title);
  barock::xdg_toplevel_t *surface = (barock::xdg_toplevel_t *)wl_resource_get_user_data(res);
  surface->data.title             = title;
}

void
set_app_id(wl_client *client, wl_resource *res, const char *app_id) {
  INFO("set_app_id \"{}\"", app_id);
  barock::xdg_toplevel_t *surface = (barock::xdg_toplevel_t *)wl_resource_get_user_data(res);
  surface->data.app_id            = app_id;
}

void
set_max_size(wl_client *client, wl_resource *toplevel_res, int32_t width, int32_t height) {
  INFO("set max size: {}x{}", width, height);
};

void
set_min_size(wl_client *client, wl_resource *toplevel_res, int32_t width, int32_t height) {
  INFO("set min size: {}x{}", width, height);
};

struct xdg_toplevel_interface xdg_toplevel_impl = { .destroy          = nullptr,
                                                    .set_parent       = nullptr,
                                                    .set_title        = set_title,
                                                    .set_app_id       = set_app_id,
                                                    .show_window_menu = nullptr,
                                                    .move             = nullptr,
                                                    .resize           = nullptr,
                                                    .set_max_size     = set_max_size,
                                                    .set_min_size     = set_min_size,
                                                    .set_maximized    = nullptr,
                                                    .unset_maximized  = nullptr,
                                                    .set_fullscreen   = nullptr,
                                                    .unset_fullscreen = nullptr,
                                                    .set_minimized    = nullptr };
