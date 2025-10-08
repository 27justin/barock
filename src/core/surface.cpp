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
    , staging({})
    , role(nullptr) {

    // The initial value for an input region is infinite. That means
    // the whole surface will accept input.
    state.input = region_t::infinite;
  }

  surface_t::surface_t(surface_t &&other)
    : compositor(std::exchange(other.compositor, nullptr))
    , frame_callback(std::exchange(other.frame_callback, nullptr))
    , state(std::exchange(other.state, {}))
    , staging(std::exchange(other.staging, {})) {}

  void
  surface_t::extent(int32_t &x, int32_t &y, int32_t &width, int32_t &height) const {
    x      = 0;
    y      = 0;
    width  = 0;
    height = 0;
    if (role && role->type_id() == barock::xdg_surface_t::id()) {
      auto &xdg_surface = *reinterpret_cast<barock::xdg_surface_t *>(role);

      switch (xdg_surface.role) {
        case barock::xdg_role_t::eToplevel: {
          auto &role = xdg_surface.as.toplevel->data;

          // Compute bounds of window's drawable content
          x      = role.x + xdg_surface.x;
          y      = role.y + xdg_surface.y;
          width  = role.width;
          height = role.height;
          break;
        }
        case barock::xdg_role_t::ePopup: {
          WARN("surface_t#extent not implemented for role xdg_popup.");
          return;
        }
      }
    }
  }
}

void
wl_surface_damage(wl_client   *client,
                  wl_resource *wl_surface,
                  int32_t      x,
                  int32_t      y,
                  int32_t      width,
                  int32_t      height) {

  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);
  surface->get()->staging.damage += barock::region_t{ x, y, width, height };
}

void
wl_surface_damage_buffer(wl_client   *client,
                         wl_resource *wl_surface,
                         int32_t      x,
                         int32_t      y,
                         int32_t      width,
                         int32_t      height) {
  // TODO: Figure out the coordinate difference between `damage` and `damage_buffer`
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);
  surface->get()->staging.damage += barock::region_t{ x, y, width, height };
}

void
wl_surface_commit(wl_client *client, wl_resource *wl_surface) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);

  barock::surface_state_t old_state = surface->get()->state;
  surface->get()->state             = surface->get()->staging;

  if (old_state.buffer != surface->get()->state.buffer) {
    barock::shm_buffer_t *buffer =
      (barock::shm_buffer_t *)wl_resource_get_user_data(surface->get()->state.buffer);
    surface->get()->on_buffer_attached.emit(*buffer);
  }

  surface->get()->staging =
    barock::surface_state_t{ // Copy our subsurfaces, those are persistent
                             .subsurfaces = surface->get()->state.subsurfaces
    };
}

void
wl_surface_destroy(wl_client *client, wl_resource *wl_surface) {
  // Destroying the surface is only allowed, once the client destroyed
  // all resources that build on this surface. (xdg surfaces, etc.)
  WARN("wl_surface#destroy");
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);

  if (surface->get()->role != nullptr) {
    wl_resource_post_error(wl_surface, WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface has active role assigned, destroy that first.");
    return;
  }

  wl_resource_destroy(wl_surface);
}

void
wl_surface_frame(wl_client *client, wl_resource *wl_surface, uint32_t callback) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);

  wl_resource *callback_res = wl_resource_create(client, &wl_callback_interface,
                                                 wl_resource_get_version(wl_surface), callback);
  wl_resource_set_implementation(
    callback_res, nullptr, wl_resource_get_user_data(wl_surface), [](wl_resource *res) {
      shared_t<resource_t<surface_t>> surface =
        *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(res);
      if (surface->get()->frame_callback == res)
        surface->get()->frame_callback = nullptr; // prevent dangling ptr
    });

  surface->get()->frame_callback = callback_res;
}

void
wl_surface_attach(wl_client   *client,
                  wl_resource *wl_surface,
                  wl_resource *buffer,
                  int32_t      x,
                  int32_t      y) {
  barock::shm_buffer_t *shm_buffer = (barock::shm_buffer_t *)wl_resource_get_user_data(buffer);
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);
  // Buffers are double-buffered
  surface->get()->staging.buffer = buffer;
}

void
wl_surface_set_opaque_region(wl_client *, wl_resource *wl_surface, wl_resource *wl_region) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);

  if (wl_region != nullptr) {
    barock::region_t *region       = (barock::region_t *)wl_resource_get_user_data(wl_region);
    surface->get()->staging.opaque = *region;
  } else {
    // A NULL wl_region causes the pending opaque region to be set to
    // empty.
    surface->get()->staging.opaque = barock::region_t{};
  }
}

void
wl_surface_set_input_region(wl_client *, wl_resource *wl_surface, wl_resource *wl_region) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);

  if (wl_region != nullptr) {
    barock::region_t *region      = (barock::region_t *)wl_resource_get_user_data(wl_region);
    surface->get()->staging.input = *region;
  } else {
    // A NULL wl_region causes the input region to be set to infinite.
    surface->get()->staging.opaque = barock::region_t::infinite;
  }
}

void
wl_surface_set_buffer_transform(wl_client *, wl_resource *wl_surface, int32_t transform) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);
  surface->get()->staging.transform = transform;
}

void
wl_surface_set_buffer_scale(wl_client *, wl_resource *wl_surface, int32_t scale) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);
  surface->get()->staging.scale = scale;
}

void
wl_surface_offset(wl_client *, wl_resource *wl_surface, int32_t x, int32_t y) {
  shared_t<resource_t<surface_t>> surface =
    *(shared_t<resource_t<surface_t>> *)wl_resource_get_user_data(wl_surface);
  surface->get()->staging.offset.x = x;
  surface->get()->staging.offset.y = y;
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
