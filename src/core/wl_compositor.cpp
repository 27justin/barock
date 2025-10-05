#include "wl/wayland-protocol.h"

#include "../log.hpp"
#include "barock/compositor.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include <GLES2/gl2.h>
#include <iostream>

static const struct wl_compositor_interface wl_compositor_impl = {
  .create_surface = &barock::wl_compositor_t::handle_create_surface,
  .create_region  = &barock::wl_compositor_t::handle_create_region,
};

namespace barock {
  wl_compositor_t::~wl_compositor_t() {}

  wl_compositor_t::wl_compositor_t(compositor_t &comp)
    : compositor(comp) {
    wl_global_create(comp.display(), &wl_compositor_interface, VERSION, this, bind);
  }

  void
  wl_compositor_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
    struct wl_resource *resource =
      wl_resource_create(client, &wl_compositor_interface, version, id);

    wl_resource_set_implementation(resource, &wl_compositor_impl, ud, NULL);
  }

  // ----------------------------------
  //  WAYLAND PROTOCOL IMPLEMENTATION
  // ----------------------------------

  void
  wl_compositor_t::handle_create_surface(wl_client   *client,
                                         wl_resource *compositor_base_res,
                                         uint32_t     id) {
    auto *compositor =
      static_cast<wl_compositor_t *>(wl_resource_get_user_data(compositor_base_res));

    wl_resource *surface_res = wl_resource_create(client, &wl_surface_interface,
                                                  wl_resource_get_version(compositor_base_res), id);

    if (!surface_res) {
      wl_client_post_no_memory(client);
      return;
    }

    INFO("Create surface");

    barock::base_surface_t *surface = new barock::base_surface_t;
    surface->buffer                 = nullptr;
    surface->is_dirty               = false;
    // TODO: Ugrh.. I hate this nomenclature
    surface->compositor = &compositor->compositor;
    surface->role       = nullptr;

    compositor->surfaces.push_back(surface);

    // Set the protocol method handlers
    wl_resource_set_implementation(
      surface_res, &wl_surface_impl,
      surface, // pointer to our surface object
      [](wl_resource *resource) {
        auto surface    = (barock::base_surface_t *)wl_resource_get_user_data(resource);
        auto compositor = surface->compositor->wl_compositor.get();

        auto it = std::find(compositor->surfaces.begin(), compositor->surfaces.end(), surface);
        if (it != compositor->surfaces.end())
          compositor->surfaces.erase(it);

        delete surface;
      });
  }

  void
  wl_compositor_t::handle_create_region(wl_client *, wl_resource *, uint32_t) {
    std::cerr << "create region\n";
  }

};
