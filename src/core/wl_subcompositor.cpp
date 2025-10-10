#include "barock/compositor.hpp"
#include "barock/resource.hpp"

#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/core/wl_subcompositor.hpp"

#include "wl/wayland-protocol.h"

#include "../log.hpp"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

using namespace barock;

void
wl_subcompositor_destroy(wl_client *, wl_resource *);

void
wl_subcompositor_get_subsurface(wl_client *, wl_resource *, uint32_t, wl_resource *, wl_resource *);

struct wl_subcompositor_interface wl_subcompositor_impl{
  .destroy        = wl_subcompositor_destroy,
  .get_subsurface = wl_subcompositor_get_subsurface,
};

void
wl_subsurface_set_position(wl_client *client, wl_resource *wl_subsurface, int32_t x, int32_t y) {
  auto subsurface = from_wl_resource<subsurface_t>(wl_subsurface);
  subsurface->x   = x;
  subsurface->y   = y;
}

void
wl_subsurface_destroy(wl_client *, wl_resource *);

struct wl_subsurface_interface wl_subsurface_impl{
  .destroy      = wl_subsurface_destroy,
  .set_position = wl_subsurface_set_position,
  .place_above  = nullptr,
  .place_below  = nullptr,
  .set_sync     = [](auto, auto) {},
  .set_desync   = [](auto, auto) {},
};

namespace barock {

  wl_subcompositor_t::wl_subcompositor_t(compositor_t &compositor)
    : compositor(compositor) {
    wl_subcompositor_global =
      wl_global_create(compositor.display(), &wl_subcompositor_interface, VERSION, this, bind);
  }

  void
  wl_subcompositor_t::bind(wl_client *client,
                           void      *wl_subcompositor,
                           uint32_t   version,
                           uint32_t   id) {
    wl_resource *res = wl_resource_create(client, &wl_subcompositor_interface, version, id);
    if (!res) {
      wl_client_post_no_memory(client);
      return;
    }

    wl_resource_set_implementation(res, &wl_subcompositor_impl, wl_subcompositor, nullptr);
  }
}

void
wl_subcompositor_destroy(wl_client *client, wl_resource *resource) {
  // We do not hold custom data for our bound subcompositors
  // interface; thus we can safely ignore.
  return;
}

void
wl_subcompositor_get_subsurface(wl_client   *client,
                                wl_resource *wl_subcompositor,
                                uint32_t     id,
                                wl_resource *wl_surface,
                                wl_resource *parent) {
  // Create a sub-surface interface for the given surface, and
  // associate it with the given parent surface. This turns a plain
  // wl_surface into a sub-surface.

  barock::wl_subcompositor_t *subcompositor =
    (barock::wl_subcompositor_t *)wl_resource_get_user_data(wl_subcompositor);
  barock::wl_compositor_t *compositor = subcompositor->compositor.wl_compositor.get();

  // The to-be sub-surface must not already have another role, and it
  // must not have an existing wl_subsurface object. Otherwise the
  // bad_surface protocol error is raised.
  shared_t<resource_t<surface_t>> child_surface =
    *(decltype(child_surface) *)wl_resource_get_user_data(wl_surface);
  shared_t<resource_t<surface_t>> parent_surface =
    *(decltype(parent_surface) *)wl_resource_get_user_data(parent);

  // TODO: This broke
  // if (child_surface->role != nullptr) {
  //   wl_resource_post_error(wl_subcompositor, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
  //                          "Surface role has already been assigned");
  //   return;
  // }

  auto wl_subsurface = make_resource<subsurface_t>(
    client, wl_subsurface_interface, wl_subsurface_impl, wl_resource_get_version(wl_subcompositor),
    id, subsurface_t{ .parent = parent_surface, .surface = child_surface, .x = 0, .y = 0 });

  // Adding sub-surfaces to a parent is a double-buffered operation on
  // the parent (see wl_surface.commit). The effect of adding a
  // sub-surface becomes visible on the next time the state of the
  // parent surface is applied.

  parent_surface->staging.subsurfaces.emplace_back(wl_subsurface);

  // Remove subsurface from wl_compositor
  auto it = std::find(compositor->surfaces.begin(), compositor->surfaces.end(), child_surface);
  if (it != compositor->surfaces.end()) {
    compositor->surfaces.erase(it);
  }

  // The parent surface must not be one of the child surface's
  // descendants, and the parent must be different from the child
  // surface, otherwise the bad_parent protocol error is raised.

  // TODO:

  // This request modifies the behaviour of wl_surface.commit request on
  // the sub-surface, see the documentation on wl_subsurface interface.
}

void
wl_subsurface_destroy(wl_client *, wl_resource *wl_subsurface) {
  auto subsurface = from_wl_resource<subsurface_t>(wl_subsurface);

  auto parent = subsurface->parent.lock();

  // TODO: We "double"-buffer the subsurfaces, therefore we also have
  // to remove the subsurface from both buffers, this sucks.

  { // Active
    auto begin = parent->state.subsurfaces.begin();
    auto end   = parent->state.subsurfaces.begin();

    auto it = std::find(begin, end, subsurface);
    if (it != end) {
      parent->state.subsurfaces.erase(it);
    }
  }
  { // Inactive (staging)
    auto begin = parent->staging.subsurfaces.begin();
    auto end   = parent->staging.subsurfaces.begin();

    auto it = std::find(begin, end, subsurface);
    if (it != end) {
      parent->staging.subsurfaces.erase(it);
    }
  }

  wl_resource_destroy(wl_subsurface);
}
