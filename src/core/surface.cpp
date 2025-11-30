#include "barock/compositor.hpp"
#include "barock/resource.hpp"

#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_subcompositor.hpp"

#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <utility>

using namespace barock;

namespace barock {
  surface_t::surface_t()
    : compositor(nullptr)
    , frame_callback(nullptr)
    , state({ .subsurface = nullptr })
    , staging({ .subsurface = nullptr }) {

    // The initial value for an input region is infinite. That means
    // the whole surface will accept input.
    state.input = region_t::infinite;
  }

  surface_t::surface_t(surface_t &&other)
    : compositor(std::exchange(other.compositor, nullptr))
    , frame_callback(std::exchange(other.frame_callback, nullptr))
    , state(std::exchange(other.state, { .subsurface = nullptr }))
    , staging(std::exchange(other.staging, { .subsurface = nullptr }))
    , role(std::exchange(other.role, nullptr)) {}

  region_t
  surface_t::extent() const {
    region_t bounds{};
    if (state.buffer) {
      bounds.w = state.buffer->width;
      bounds.h = state.buffer->height;
    }
    return bounds;
  }

  region_t
  surface_t::full_extent() const {
    region_t region = extent();

    for (auto &child : state.children) {
      if (auto subsurface = child->surface.lock()) {
        region_t child_region = subsurface->full_extent();
        child_region.x += subsurface->x;
        child_region.y += subsurface->y;

        region = region.union_with(child_region); // merge bounding boxes
      }
    }

    return region;
  }

  region_t
  surface_t::position() const {
    region_t bounds = extent();
    bounds.x        = 0;
    bounds.y        = 0;

    auto current = this;
    while (current->parent.lock()) {
      current = current->parent.lock().get();
      bounds.x += current->x;
      bounds.y += current->y;
    }

    bounds.x += x;
    bounds.y += y;
    return bounds;
  }

  shared_t<surface_t>
  surface_t::lookup_at(double x, double y) {
    for (auto it = state.children.rbegin(); it != state.children.rend(); ++it) {
      if (auto subsurface = (*it)->surface.lock()) {
        // First compute the hit test point relative to the subsurface
        auto position = subsurface->position();
        auto extent   = subsurface->extent();
        position.w    = extent.w;
        position.h    = extent.h;

        // INFO("Lookup:\n  Surface: x = {}, y = {}\n  Cursor: x = {}, y = {}\n",
        // position.x, position.y, x, y);

        if (position.intersects(x, y)) {
          if (auto deeper = subsurface->lookup_at(x, y))
            return deeper;
          else
            return subsurface;
        }
      }
    }

    // No child matched; return nullptr
    return nullptr;
  }

  shared_t<surface_t>
  surface_t::find_parent(const std::function<bool(shared_t<surface_t> &)> &condition) const {
    // Do we even have a parent?
    if (shared_t<surface_t> current_surface = parent.lock()) {
      // We have; then ascend upwards and test until we match
      // something, or until we have no parent anymore.
      do {
        if (!current_surface)
          return nullptr;

        if (condition(current_surface)) {
          return current_surface;
        }

        // Exit when the surface doesn't have a parent.
        if (!current_surface->state.subsurface)
          return nullptr;
      } while ((current_surface = current_surface->parent.lock()));
    }
    return nullptr;
  }

  bool
  surface_t::has_role() const {
    return role.get() != nullptr;
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
  surface->staging.damage += barock::region_t{ x, y, width, height };
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
  surface->staging.damage += barock::region_t{ x, y, width, height };
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
    surface->on_buffer_attach.emit(*surface->state.buffer);
  }

  surface->staging = barock::surface_state_t{ // Copy our subsurfaces, those are persistent
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

  auto weak = new weak_t<resource_t<surface_t>>(surface);

  wl_resource *callback_res = wl_resource_create(
    client, &wl_callback_interface, wl_resource_get_version(wl_surface), callback);

  wl_resource_set_implementation(callback_res, nullptr, weak, [](wl_resource *res) {
    auto weak_surface = (weak_t<resource_t<surface_t>> *)wl_resource_get_user_data(res);
    if (auto surface = weak_surface->lock(); surface) {
      if (surface->frame_callback == res) {
        surface->frame_callback = nullptr; // prevent dangling ptr
      } else {
        WARN("Tried to zero frame callback, but isn't owned by this resource");
      }
    } else {
      WARN("wl_surface#on_destroy tried to get a strong reference to surface, but already out of "
           "scope.");
    }
    delete weak_surface;
  });

  surface->frame_callback = callback_res;
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

  // auto shm_buffer = from_wl_resource<shm_buffer_t>(wl_buffer);
  // WARN("dimensions: {} x {}", shm_buffer->width, shm_buffer->height);

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
