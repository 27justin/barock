#include "wl/wayland-protocol.h"

#include "../log.hpp"
#include "barock/compositor.hpp"
#include "barock/core/region.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/resource.hpp"
#include <GLES2/gl2.h>
#include <iostream>
#include <wayland-server-core.h>

using namespace barock;

void
wl_compositor_create_surface(wl_client *, wl_resource *, uint32_t);

void
wl_compositor_create_region(wl_client *, wl_resource *, uint32_t);

static const struct wl_compositor_interface wl_compositor_impl = {
  .create_surface = &wl_compositor_create_surface,
  .create_region  = &wl_compositor_create_region,
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

    wl_resource_set_implementation(resource, &wl_compositor_impl, ud, nullptr);
  }
}; // namespace barock

void
wl_compositor_create_surface(wl_client *client, wl_resource *wl_compositor, uint32_t id) {
  auto *compositor = static_cast<wl_compositor_t *>(wl_resource_get_user_data(wl_compositor));

  auto surface = make_resource<surface_t>(
    client, wl_surface_interface, wl_surface_impl, wl_resource_get_version(wl_compositor), id);

  surface->compositor = &compositor->compositor;
  surface->role       = nullptr;

  // Infinite damage to force redraw.
  surface->state.damage   = region_t::infinite;
  surface->staging.damage = region_t::infinite;
}

void
wl_compositor_create_region(wl_client *client, wl_resource *wl_compositor, uint32_t id) {
  make_resource<region_t>(
    client, wl_region_interface, wl_region_impl, wl_resource_get_version(wl_compositor), id);
}
