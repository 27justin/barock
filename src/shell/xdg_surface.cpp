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
}

/*
assign the xdg_toplevel surface role

This creates an xdg_toplevel object for the given xdg_surface and gives the associated wl_surface
the xdg_toplevel role.

See the documentation of xdg_toplevel for more details about what an xdg_toplevel is and how it is
used.
*/
void
get_toplevel(wl_client *client, wl_resource *resource, uint32_t id) {
  // Retrieve the surface_t we got from our compositor.
  xdg_surface_t *surface = (xdg_surface_t *)wl_resource_get_user_data(resource);

  if (surface->role != xdg_role_t::eNone) {
    wl_resource_post_error(resource, WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface role was already assigned.");
    return;
  }

  wl_resource *toplevel_res =
    wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
  if (!toplevel_res) {
    wl_client_post_no_memory(client);
    return;
  }

  // Create the xdg_toplevel_t surface, this takes a reference to the
  // surface_t
  xdg_toplevel_t *toplevel = new xdg_toplevel_t(surface, xdg_toplevel_data_t{
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

  surface->as.toplevel = toplevel;
  surface->role        = xdg_role_t::eToplevel;

  WARN("Created xdg_toplevel: {}", (void *)toplevel);
  wl_resource_set_implementation(toplevel_res, &xdg_toplevel_impl, toplevel, [](wl_resource *res) {
    INFO("Deleting xdg_toplevel_t ({})", (void *)wl_resource_get_user_data(res));
    delete static_cast<xdg_toplevel_t *>(wl_resource_get_user_data(res));
  });

  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel_res, 0, 0, &states);
  wl_array_release(&states);
}

void
ack_configure(wl_client *, wl_resource *surface, uint32_t id) {
  INFO("xdg_surface::ack_configure");
}

void
xdg_surface_set_window_geometry(wl_client   *client,
                                wl_resource *xdg_surface_res,
                                int32_t      x,
                                int32_t      y,
                                int32_t      w,
                                int32_t      h) {
  // The window geometry of a surface is its "visible bounds" from the
  // user's perspective. Client-side decorations often have invisible
  // portions like drop-shadows which should be ignored for the purposes
  // of aligning, placing and constraining windows.
  xdg_surface_t *surface = (xdg_surface_t *)wl_resource_get_user_data(xdg_surface_res);
  surface->x             = x;
  surface->y             = y;
  surface->width         = w;
  surface->height        = h;

  INFO("xdg_surface#set_window_geometry(x = {}, y = {}, width = {}, height = {})", x, y, w, h);
}

struct xdg_surface_interface xdg_surface_impl = { .destroy      = nullptr,
                                                  .get_toplevel = get_toplevel,
                                                  .get_popup    = nullptr,
                                                  .set_window_geometry =
                                                    xdg_surface_set_window_geometry,
                                                  .ack_configure = ack_configure };
