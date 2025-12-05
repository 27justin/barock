#include "barock/shell/xdg_surface.hpp"
#include "barock/core/cursor_manager.hpp"
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
    , role(xdg_role_t::eNone)
    , role_impl()
    , position({ 0., 0. })
    , size({ 0., 0. })
    , offset({ 0., 0. }) {}

  xdg_surface_t::~xdg_surface_t() {}
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

  if (surface->role != xdg_role_t::eNone) {
    wl_resource_post_error(surface->resource(),
                           WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface role was already assigned.");
    return;
  }

  auto toplevel = make_resource<xdg_toplevel_t>(client,
                                                xdg_toplevel_interface,
                                                xdg_toplevel_impl,
                                                wl_resource_get_version(surface->resource()),
                                                id,
                                                surface,
                                                xdg_toplevel_data_t{
                                                  .title  = "",
                                                  .app_id = "",
                                                });

  surface->role_impl = toplevel;
  surface->role      = xdg_role_t::eToplevel;

  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel->resource(), 0, 0, &states);
  wl_array_release(&states);

  // Once we have the toplevel, we move it to the current output.
  auto &output = surface->shell.cursor_manager.current_output();
  output.metadata.get<xdg_window_list_t>().emplace_back(surface);
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

  surface->offset = { static_cast<float>(x), static_cast<float>(y) };
  surface->size   = { static_cast<float>(w), static_cast<float>(h) };

  surface->events.on_geometry_change.emit();
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
