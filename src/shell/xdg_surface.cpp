#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>

void
get_toplevel(wl_client *client, wl_resource *resource, uint32_t id) {
  INFO("xdg_surface::get_toplevel");

  wl_resource *toplevel =
    wl_resource_create(client, &xdg_toplevel_interface, wl_resource_get_version(resource), id);
  if (!toplevel) {
    wl_client_post_no_memory(client);
  }

  wl_resource_set_implementation(toplevel, &xdg_toplevel_impl, nullptr, nullptr);

  wl_array states;
  wl_array_init(&states);
  xdg_toplevel_send_configure(toplevel, 0, 0, &states);
  wl_array_release(&states);
}

void
ack_configure(wl_client *, wl_resource *surface, uint32_t) {
  INFO("xdg_surface::ack_configure");
}

struct xdg_surface_interface xdg_surface_impl = { .destroy             = nullptr,
                                                  .get_toplevel        = get_toplevel,
                                                  .get_popup           = nullptr,
                                                  .set_window_geometry = nullptr,
                                                  .ack_configure       = ack_configure };
