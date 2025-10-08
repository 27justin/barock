#include "barock/shell/xdg_surface.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

using namespace barock;

namespace barock {
  xdg_surface_t::xdg_surface_t(xdg_shell_t &parent, shared_t<resource_t<surface_t>> base)
    : shell(parent)
    , surface(base)
    , role(xdg_role_t::eNone) {
    as.raw = nullptr;
  }

  xdg_surface_t::~xdg_surface_t() {
    switch (role) {
      case xdg_role_t::eToplevel: {
        delete as.toplevel;
        break;
      }
      default: {
        assert(false && "Unhandled role in xdg_surface_t dtor");
      }
    }
  }
}

/*
assign the xdg_toplevel surface role

This creates an xdg_toplevel object for the given xdg_surface and gives the associated wl_surface
the xdg_toplevel role.

See the documentation of xdg_toplevel for more details about what an xdg_toplevel is and how it is
used.
*/
void
get_toplevel(wl_client *client, wl_resource *xdg_surface, uint32_t id) {
  // Retrieve the surface_t we got from our compositor.
  auto surface = from_wl_resource<xdg_surface_t>(xdg_surface);

  if (surface->get()->role != xdg_role_t::eNone) {
    wl_resource_post_error(surface->resource(), WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface role was already assigned.");
    return;
  }

  auto toplevel =
    make_resource<xdg_toplevel_t>(client, xdg_toplevel_interface, xdg_toplevel_impl,
                                  wl_resource_get_version(surface->resource()), id, surface->get(),
                                  xdg_toplevel_data_t{
                                    .title  = "",
                                    .app_id = "",
                                    .x      = 0,
                                    .y      = 0,
                                    .width  = -1, // Use
                                    // what
                                    // the
                                    // client
                                    // prefers.
                                    .height = -1,
                                  });

  surface->get()->as.toplevel = new shared_t<resource_t<xdg_toplevel_t>>(toplevel);
  surface->get()->role        = xdg_role_t::eToplevel;

  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel->resource(), 0, 0, &states);
  wl_array_release(&states);
}

void
ack_configure(wl_client *, wl_resource *surface, uint32_t id) {
  INFO("xdg_surface::ack_configure");
}

void
xdg_surface_set_window_geometry(wl_client   *client,
                                wl_resource *xdg_surface,
                                int32_t      x,
                                int32_t      y,
                                int32_t      w,
                                int32_t      h) {
  // The window geometry of a surface is its "visible bounds" from the
  // user's perspective. Client-side decorations often have invisible
  // portions like drop-shadows which should be ignored for the purposes
  // of aligning, placing and constraining windows.
  auto surface = from_wl_resource<xdg_surface_t>(xdg_surface);

  surface->get()->x      = x;
  surface->get()->y      = y;
  surface->get()->width  = w;
  surface->get()->height = h;

  INFO("xdg_surface#set_window_geometry(x = {}, y = {}, width = {}, height = {})", x, y, w, h);
}

void
xdg_surface_destroy(wl_client *, wl_resource *);

struct xdg_surface_interface xdg_surface_impl = { .destroy      = xdg_surface_destroy,
                                                  .get_toplevel = get_toplevel,
                                                  .get_popup    = nullptr,
                                                  .set_window_geometry =
                                                    xdg_surface_set_window_geometry,
                                                  .ack_configure = ack_configure };

void
xdg_surface_destroy(wl_client *, wl_resource *wl_xdg_surface) {
  wl_resource_destroy(wl_xdg_surface);
}
