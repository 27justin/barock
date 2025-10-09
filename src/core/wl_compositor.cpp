#include "wl/wayland-protocol.h"

#include "../log.hpp"
#include "barock/compositor.hpp"
#include "barock/core/region.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/resource.hpp"
#include <GLES2/gl2.h>
#include <iostream>
#include <wayland-server-core.h>

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

    auto res = make_resource<surface_t>(client, wl_surface_interface, wl_surface_impl,
                                        wl_resource_get_version(compositor_base_res), id);
    res->on_destroy.connect([compositor](auto wl_surface) {
      INFO("wl_surface#destroy");
      auto surface = from_wl_resource<surface_t>(wl_surface);

      std::lock_guard<std::mutex> lock(compositor->surfaces_mutex);

      auto it = std::find(compositor->surfaces.begin(), compositor->surfaces.end(), surface);
      if (it != compositor->surfaces.end()) {
        compositor->surfaces.erase(it);
        WARN("Erased wl_surface from compositor.");
      } else {
        WARN("wl_surface#destroy was called, but the surface is not tracked by the compositor?");
      }
    });

    res->on_destruct.connect([](auto &) { ERROR("wl_surface is now fully OOM!"); });

    res->compositor = &compositor->compositor;
    res->role       = nullptr;

    compositor->surfaces.push_back(res);
  }

  void
  wl_compositor_t::handle_create_region(wl_client   *client,
                                        wl_resource *wl_compositor,
                                        uint32_t     id) {

    auto wl_region =
      wl_resource_create(client, &wl_region_interface, wl_resource_get_version(wl_compositor), id);
    if (!wl_region) {
      wl_client_post_no_memory(client);
      return;
    }

    // Zero initialized
    region_t *region = new region_t{};

    wl_resource_set_implementation(wl_region, &wl_region_impl, region, nullptr);
  }

};
