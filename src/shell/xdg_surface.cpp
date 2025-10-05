#include "barock/shell/xdg_surface.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

/*
assign the xdg_toplevel surface role

This creates an xdg_toplevel object for the given xdg_surface and gives the associated wl_surface
the xdg_toplevel role.

See the documentation of xdg_toplevel for more details about what an xdg_toplevel is and how it is
used.
*/
void
get_toplevel(wl_client *client, wl_resource *resource, uint32_t id) {
  // Retrieve the base_surface_t we got from our compositor.
  barock::xdg_surface_t *surface = (barock::xdg_surface_t *)wl_resource_get_user_data(resource);

  if (surface->surface->role != nullptr) {
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
  // base_surface_t
  barock::xdg_toplevel_t *toplevel =
    new barock::xdg_toplevel_t(*surface, barock::xdg_toplevel_data_t{
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

  surface->surface->role = toplevel;

  wl_resource_set_implementation(toplevel_res, &xdg_toplevel_impl, toplevel, [](wl_resource *res) {
    INFO("Deleting xdg_toplevel_t");
    delete static_cast<barock::xdg_toplevel_t *>(wl_resource_get_user_data(res));
  });

  // If we derive the window size from their preferred size, we wait
  // for wl_surface#attach, then use the buffer dimensions.
  if (toplevel->data.width == -1 && toplevel->data.height == -1) {
    surface->surface->on_buffer_attached.connect([=](const barock::shm_buffer_t &buf) {
      toplevel->data.width  = buf.width;
      toplevel->data.height = buf.height;
    });
  }

  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel_res, 0, 0, &states);
  wl_array_release(&states);
}

void
ack_configure(wl_client *, wl_resource *surface, uint32_t id) {
  INFO("xdg_surface::ack_configure");
}

struct xdg_surface_interface xdg_surface_impl = { .destroy             = nullptr,
                                                  .get_toplevel        = get_toplevel,
                                                  .get_popup           = nullptr,
                                                  .set_window_geometry = nullptr,
                                                  .ack_configure       = ack_configure };
