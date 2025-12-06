#include "barock/compositor.hpp"
#include "barock/core/region.hpp"
#include "barock/resource.hpp"

#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_subcompositor.hpp"

#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"
#include <optional>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <utility>

using namespace barock;

namespace barock {
  surface_t::surface_t()
    : state({ .subsurface = nullptr })
    , staging({ .subsurface = nullptr })
    , role(nullptr) {

    // The initial value for an input region is infinite. That means
    // the whole surface will accept input.
    state.input = region_t::infinite;
  }

  surface_t::surface_t(surface_t &&other)
    : state(std::exchange(other.state, { .subsurface = nullptr }))
    , staging(std::exchange(other.staging, { .subsurface = nullptr }))
    , role(std::exchange(other.role, nullptr)) {}

  ipoint_t
  surface_t::extent() const {
    if (!state.buffer)
      return { 0, 0 };
    else
      return { state.buffer->width, state.buffer->height };
  }

  ipoint_t
  surface_t::full_extent() const {
    ipoint_t region = extent();

    for (auto &child : state.children) {
      if (auto subsurface = child->surface.lock()) {
        auto child_extent = subsurface->full_extent();
        region.x          = std::max(region.x, child_extent.x + child->position.x);
        region.y          = std::max(region.y, child_extent.y + child->position.y);
      }
    }
    return region;
  }

  bool
  surface_t::has_role() const {
    return role != nullptr;
  }

  ipoint_t
  surface_t::position() const {
    if (!state.subsurface)
      return { 0, 0 };

    ipoint_t position{ state.subsurface->position };
    auto     parent = state.subsurface->parent;
    while (auto surface = parent.lock()) {
      if (surface->state.subsurface) {
        position += surface->state.subsurface->position;
        parent = surface->state.subsurface->parent;
      } else {
        // No more parent
        return position;
      }
    }
    return position;
  }

  surface_t &
  surface_t::root() {
    surface_t *candidate = this;
    if (!candidate->state.subsurface)
      return *candidate;

    while (auto surface = candidate->state.subsurface->parent.lock()) {
      candidate = surface.get();
      if (!candidate->state.subsurface)
        return *candidate;
    }
    return *candidate;
  }

  const surface_t &
  surface_t::root() const {
    surface_t const *candidate = this;
    if (!candidate->state.subsurface)
      return *candidate;

    while (auto surface = candidate->state.subsurface->parent.lock()) {
      candidate = surface.get();
      if (!candidate->state.subsurface)
        return *candidate;
    }
    return *candidate;
  }

  shared_t<surface_t>
  surface_t::lookup(const ipoint_t &position) {
    shared_t<surface_t> surface = nullptr;

    for (auto &child : state.children) {
      if (auto subsurface = child->surface.lock(); subsurface) {
        auto &child_position = subsurface->state.subsurface->position;
        // We can ignore subsurfaces that don't match our position
        if (child_position >= position)
          continue;

        // We can also ignore the surface if it doesn't extend past our point
        if (subsurface->full_extent() + child_position < position)
          continue;

        auto vresult = subsurface->lookup(position - child_position);
        if (vresult)
          return vresult;
      }
    }

    auto dimensions = extent();
    if (state.subsurface && position >= ipoint_t{ 0, 0 } && position <= dimensions)
      return state.subsurface->surface.lock();

    return surface;
  }
}

void
wl_surface_damage(wl_client   *client,
                  wl_resource *wl_surface,
                  int32_t      x,
                  int32_t      y,
                  int32_t      width,
                  int32_t      height) {
  auto surface = from_wl_resource<surface_t>(wl_surface);
  // surface->staging.damage += barock::region_t{ x, y, width, height };
  // WARN("Legacy damage, skipping this");
}

void
wl_surface_damage_buffer(wl_client   *client,
                         wl_resource *wl_surface,
                         int32_t      x,
                         int32_t      y,
                         int32_t      width,
                         int32_t      height) {
  // TODO: Figure out the coordinate difference between `damage` and `damage_buffer`
  auto surface = from_wl_resource<surface_t>(wl_surface);

  if (!surface->staging.damage)
    surface->staging.damage = barock::region_t{ x, y, width, height };
  else
    surface->staging.damage =
      surface->staging.damage->union_with(barock::region_t{ x, y, width, height });
}

void
wl_surface_commit(wl_client *client, wl_resource *wl_surface) {
  auto surface = from_wl_resource<surface_t>(wl_surface);

  barock::surface_state_t old_state = surface->state;
  surface->state                    = surface->staging;

  // Check whether the buffers changed, and the new buffer is not a
  // nullptr, when set to nullptr, the compositor detaches the buffer
  // and stops rendering that surface.
  if (surface->state.buffer) {
    surface->events.on_buffer_attach.emit(*surface->state.buffer);
  }

  surface->staging = barock::surface_state_t{ // By default, our surface has no pending damage.
                                              .damage = std::nullopt,

                                              // Copy our subsurfaces, those are persistent
                                              .subsurface = surface->state.subsurface,
                                              .children   = surface->state.children
  };
}

void
wl_surface_destroy(wl_client *client, wl_resource *wl_surface) {
  // Destroying the surface is only allowed, once the client destroyed
  // all resources that build on this surface. (xdg surfaces, etc.)
  auto surface = from_wl_resource<surface_t>(wl_surface);

  if (surface->role != nullptr) {
    wl_resource_post_error(wl_surface,
                           WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface has active role assigned, destroy that first.");
    return;
  }

  surface->state.buffer = nullptr;
}

void
wl_surface_frame(wl_client *client, wl_resource *wl_surface, uint32_t callback) {
  auto surface = from_wl_resource<surface_t>(wl_surface);
  auto weak    = new weak_t<resource_t<surface_t>>(surface);

  wl_resource *callback_res = wl_resource_create(
    client, &wl_callback_interface, wl_resource_get_version(wl_surface), callback);

  wl_resource_set_implementation(callback_res, nullptr, weak, [](wl_resource *res) {
    auto weak_surface = (weak_t<resource_t<surface_t>> *)wl_resource_get_user_data(res);
    if (auto surface = weak_surface->lock(); surface) {
      if (surface->state.pending == res) {
        surface->state.pending = nullptr; // prevent dangling ptr
      } else {
        WARN("Tried to zero frame callback, but isn't owned by this resource");
      }
    } else {
      WARN("wl_surface#on_destroy tried to get a strong reference to surface, but already out of "
           "scope.");
    }
    delete weak_surface;
  });

  surface->state.pending   = callback_res;
  surface->staging.pending = callback_res;
}

void
wl_surface_attach(wl_client   *client,
                  wl_resource *wl_surface,
                  wl_resource *wl_buffer,
                  int32_t      x,
                  int32_t      y) {
  auto surface = from_wl_resource<surface_t>(wl_surface);

  if (wl_buffer == nullptr) {
    TRACE("wl_surface#attach: removing buffer from wl_surface");
    surface->staging.buffer = nullptr;
    return;
  }

  // Buffers are double-buffered
  surface->staging.buffer = from_wl_resource<shm_buffer_t>(wl_buffer);
}

void
wl_surface_set_opaque_region(wl_client *, wl_resource *wl_surface, wl_resource *wl_region) {
  auto surface = from_wl_resource<surface_t>(wl_surface);

  if (wl_region != nullptr) {
    barock::region_t *region = (barock::region_t *)wl_resource_get_user_data(wl_region);
    surface->staging.opaque  = *region;
  } else {
    // A NULL wl_region causes the pending opaque region to be set to
    // empty.
    surface->staging.opaque = barock::region_t{};
  }
}

void
wl_surface_set_input_region(wl_client *, wl_resource *wl_surface, wl_resource *wl_region) {
  auto surface = from_wl_resource<surface_t>(wl_surface);

  if (wl_region != nullptr) {
    barock::region_t *region = (barock::region_t *)wl_resource_get_user_data(wl_region);
    surface->staging.input   = *region;
  } else {
    // A NULL wl_region causes the input region to be set to infinite.
    surface->staging.opaque = barock::region_t::infinite;
  }
}

void
wl_surface_set_buffer_transform(wl_client *, wl_resource *wl_surface, int32_t transform) {
  auto surface               = from_wl_resource<surface_t>(wl_surface);
  surface->staging.transform = transform;
}

void
wl_surface_set_buffer_scale(wl_client *, wl_resource *wl_surface, int32_t scale) {
  auto surface           = from_wl_resource<surface_t>(wl_surface);
  surface->staging.scale = scale;
}

void
wl_surface_offset(wl_client *, wl_resource *wl_surface, int32_t x, int32_t y) {
  auto surface              = from_wl_resource<surface_t>(wl_surface);
  surface->staging.offset.x = x;
  surface->staging.offset.y = y;
}

struct wl_surface_interface wl_surface_impl = { .destroy           = wl_surface_destroy,
                                                .attach            = wl_surface_attach,
                                                .damage            = wl_surface_damage,
                                                .frame             = wl_surface_frame,
                                                .set_opaque_region = wl_surface_set_opaque_region,
                                                .set_input_region  = wl_surface_set_input_region,
                                                .commit            = wl_surface_commit,
                                                .set_buffer_transform =
                                                  wl_surface_set_buffer_transform,
                                                .set_buffer_scale = wl_surface_set_buffer_scale,
                                                .damage_buffer    = wl_surface_damage_buffer,
                                                .offset           = wl_surface_offset };
