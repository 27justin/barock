#include "barock/compositor.hpp"

#include "barock/core/surface.hpp"
#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"
#include "wl/xdg-shell-protocol.h"
#include <wayland-server-core.h>

static const struct xdg_wm_base_interface xdg_wm_base_impl = {
  .destroy           = &barock::xdg_shell_t::handle_xdg_base_destroy,
  .create_positioner = nullptr,
  .get_xdg_surface   = &barock::xdg_shell_t::handle_xdg_base_get_surface,
  .pong              = nullptr,
};

namespace barock {

  xdg_shell_t::~xdg_shell_t() {}

  xdg_shell_t::xdg_shell_t(compositor_t &compositor)
    : compositor(compositor) {
    wl_global_create(compositor.display(), &xdg_wm_base_interface, 1, this, bind);
  }

  void
  xdg_shell_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
    xdg_shell_t        *shell    = reinterpret_cast<xdg_shell_t *>(ud);
    struct wl_resource *resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);

    wl_resource_set_implementation(resource, &xdg_wm_base_impl, shell, NULL);
  }

  // ----------------------------------
  //  WAYLAND PROTOCOL IMPLEMENTATION
  // ----------------------------------

  void
  xdg_shell_t::handle_xdg_base_destroy(wl_client *client, wl_resource *res) {
    wl_resource_destroy(res);
  }

  void
  xdg_shell_t::handle_xdg_base_get_surface(wl_client   *client,
                                           wl_resource *wm_base_resource,
                                           uint32_t     id,
                                           wl_resource *surface_result) {
    xdg_shell_t *shell =
      reinterpret_cast<xdg_shell_t *>(wl_resource_get_user_data(wm_base_resource));

    // Check whether client created a surface on our wl_compositor yet.
    if (!surface_result) {
      wl_client_post_no_memory(client);
      return;
    }

    shared_t<resource_t<surface_t>> compositor_surface =
      *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(surface_result);

    xdg_surface_t *surface          = new xdg_surface_t(*shell, compositor_surface);
    compositor_surface->get()->role = surface;

    // Create resource
    struct wl_resource *xdg_surface_resource = wl_resource_create(
      client, &xdg_surface_interface, wl_resource_get_version(wm_base_resource), id);

    if (!xdg_surface_resource) {
      wl_client_post_no_memory(client);
      return;
    }

    wl_resource_set_implementation(
      xdg_surface_resource, &xdg_surface_impl, surface, [](wl_resource *resource) {
        auto surface = static_cast<xdg_surface_t *>(wl_resource_get_user_data(resource));
        switch (surface->role) {
          case barock::xdg_role_t::eToplevel: {
            surface->as.toplevel->surface = nullptr;
            break;
          }
          default: {
          }
        }
        delete surface;
      });

    // Send the configure event
    xdg_surface_send_configure(xdg_surface_resource,
                               wl_display_next_serial(shell->compositor.display()));
  }
};
