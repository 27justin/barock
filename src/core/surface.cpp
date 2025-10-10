#include "barock/compositor.hpp"

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
    , state({})
    , staging({}) {

    // The initial value for an input region is infinite. That means
    // the whole surface will accept input.
    state.input = region_t::infinite;
  }

  surface_t::surface_t(surface_t &&other)
    : compositor(std::exchange(other.compositor, nullptr))
    , frame_callback(std::exchange(other.frame_callback, nullptr))
    , state(std::exchange(other.state, {}))
    , staging(std::exchange(other.staging, {}))
    , role(std::exchange(other.role, nullptr)) {}

  void
  surface_t::extent(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const {
    x      = 0;
    y      = 0;
    width  = 0;
    height = 0;

    if (role && role->type_id() == barock::xdg_surface_t::id()) {
      auto &xdg_surface = *reinterpret_cast<const barock::xdg_surface_t *>(&*role);

      switch (xdg_surface.role) {
        case barock::xdg_role_t::eToplevel: {
          auto role = xdg_surface.get_role<xdg_toplevel_t>();

          // Get bounds of window's drawable content
          x = role->data.x;
          y = role->data.y;

          width  = role->data.width;
          height = role->data.height;

          return;
        }
        case barock::xdg_role_t::ePopup: {
          WARN("surface_t#extent not implemented for role xdg_popup.");
          return;
        }
        default: {
          ERROR("Unhandled XDG role: {}\nxdg surface: {}", (int)xdg_surface.role,
                (void *)&xdg_surface);
          assert(false && "unhandled xdg_role_t in surface_t#extent");
        }
      }
    } else {

      // Check for buffer
      if (state.buffer) {
        auto &shm = state.buffer;
        x         = 0;
        y         = 0;
        width     = shm->width;
        height    = shm->height;
      } else {
        WARN("Surface does not have a buffer attached, no valid size!");
      }
    }
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
                                              .subsurfaces = surface->state.subsurfaces
  };
}

void
wl_surface_destroy(wl_client *client, wl_resource *wl_surface) {
  // Destroying the surface is only allowed, once the client destroyed
  // all resources that build on this surface. (xdg surfaces, etc.)
  WARN("wl_surface#destroy");
  auto surface = from_wl_resource<surface_t>(wl_surface);

  if (surface->role != nullptr) {
    wl_resource_post_error(wl_surface, WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface has active role assigned, destroy that first.");
    return;
  }

  surface->state.buffer = nullptr;

  wl_resource_destroy(wl_surface);
}

void
wl_surface_frame(wl_client *client, wl_resource *wl_surface, uint32_t callback) {
  auto surface = from_wl_resource<surface_t>(wl_surface);

  auto weak = new weak_t<resource_t<surface_t>>(surface);

  wl_resource *callback_res = wl_resource_create(client, &wl_callback_interface,
                                                 wl_resource_get_version(wl_surface), callback);

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
