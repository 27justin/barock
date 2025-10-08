#include "barock/core/shm_pool.hpp"
#include "barock/resource.hpp"

#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"

using namespace barock;

namespace barock {
  xdg_toplevel_t::xdg_toplevel_t(xdg_surface_t *base, const xdg_toplevel_data_t &prop_data)
    : surface(base)
    , data(prop_data) {

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
xdg_toplevel_set_title(wl_client *client, wl_resource *xdg_toplevel, const char *title) {
  INFO("xdg_toplevel_set_title \"{}\"", title);
  auto surface               = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
  surface->get()->data.title = title;
}

void
xdg_toplevel_set_app_id(wl_client *client, wl_resource *xdg_toplevel, const char *app_id) {
  INFO("xdg_toplevel_set_app_id \"{}\"", app_id);
  auto surface                = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
  surface->get()->data.app_id = app_id;
}

void
xdg_toplevel_set_max_size(wl_client   *client,
                          wl_resource *xdg_toplevel,
                          int32_t      width,
                          int32_t      height) {
  INFO("set max size: {}x{}", width, height);
  auto surface = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
};

void
xdg_toplevel_set_min_size(wl_client   *client,
                          wl_resource *xdg_toplevel,
                          int32_t      width,
                          int32_t      height) {
  INFO("set min size: {}x{}", width, height);
  auto surface = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
};

void
xdg_toplevel_destroy(wl_client *client, wl_resource *wl_xdg_toplevel);

struct xdg_toplevel_interface xdg_toplevel_impl = { .destroy          = xdg_toplevel_destroy,
                                                    .set_parent       = nullptr,
                                                    .set_title        = xdg_toplevel_set_title,
                                                    .set_app_id       = xdg_toplevel_set_app_id,
                                                    .show_window_menu = nullptr,
                                                    .move             = nullptr,
                                                    .resize           = nullptr,
                                                    .set_max_size     = xdg_toplevel_set_max_size,
                                                    .set_min_size     = xdg_toplevel_set_min_size,
                                                    .set_maximized    = nullptr,
                                                    .unset_maximized  = nullptr,
                                                    .set_fullscreen   = nullptr,
                                                    .unset_fullscreen = nullptr,
                                                    .set_minimized    = nullptr };

void
xdg_toplevel_destroy(wl_client *client, wl_resource *wl_xdg_toplevel) {
  wl_resource_destroy(wl_xdg_toplevel);
}
