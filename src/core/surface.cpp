#include "barock/core/surface.hpp"
#include "barock/compositor.hpp"
#include "barock/core/shm_pool.hpp"

#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <utility>

namespace barock {
  base_surface_t::base_surface_t()
    : buffer(nullptr)
    , compositor(nullptr)
    , callback(nullptr)
    , is_dirty(false) {}

  base_surface_t::base_surface_t(base_surface_t &&other)
    : buffer(std::exchange(other.buffer, nullptr))
    , compositor(std::exchange(other.compositor, nullptr))
    , callback(std::exchange(other.callback, nullptr))
    , is_dirty(other.is_dirty.load()) {}
}

void
wl_surface_damage(wl_client   *client,
                  wl_resource *surface,
                  int32_t      x,
                  int32_t      y,
                  int32_t      width,
                  int32_t      height) {
  // INFO("damage\n  x: {}, y: {}\n  w: {}, h: {}", x, y, width, height);
}

void
wl_surface_commit(wl_client *client, wl_resource *surface) {
  // INFO("commit surface changes");
}

void
wl_surface_destroy(wl_client *client, wl_resource *wl_surface) {
  // Destroying the surface is only allowed, once the client destroyed
  // all resources that build on this surface. (xdg surfaces, etc.)
  barock::base_surface_t *surface = (barock::base_surface_t *)wl_resource_get_user_data(wl_surface);

  if (surface->role != nullptr) {
    wl_resource_post_error(wl_surface, WL_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT,
                           "Surface has active role assigned, destroy that first.");
    return;
  }

  wl_resource_destroy(wl_surface);
}

void
wl_surface_frame(wl_client *client, wl_resource *wl_surface, uint32_t callback) {
  barock::base_surface_t *surface = (barock::base_surface_t *)wl_resource_get_user_data(wl_surface);

  wl_resource *callback_res = wl_resource_create(client, &wl_callback_interface,
                                                 wl_resource_get_version(wl_surface), callback);
  wl_resource_set_implementation(callback_res, nullptr, surface, [](wl_resource *res) {
    auto *surface = static_cast<barock::base_surface_t *>(wl_resource_get_user_data(res));
    if (surface->callback == res)
      surface->callback = nullptr; // prevent dangling ptr
  });

  surface->callback = callback_res;
}

void
wl_surface_attach(wl_client   *client,
                  wl_resource *wl_surface,
                  wl_resource *buffer,
                  int32_t      x,
                  int32_t      y) {
  barock::shm_buffer_t *shm_buffer = (barock::shm_buffer_t *)wl_resource_get_user_data(buffer);

  // Set the buffer to our surface
  barock::base_surface_t *surface = (barock::base_surface_t *)wl_resource_get_user_data(wl_surface);
  surface->buffer                 = buffer;

  surface->on_buffer_attached.emit(*shm_buffer);
}

struct wl_surface_interface wl_surface_impl = {
  .destroy = wl_surface_destroy,
  .attach  = wl_surface_attach,
  .damage  = wl_surface_damage,
  .frame   = wl_surface_frame,
  .set_opaque_region =
    [](wl_client *, wl_resource *, wl_resource *) {
      INFO("set opaque region");
      return;
    },
  .set_input_region     = nullptr,
  .commit               = wl_surface_commit,
  .set_buffer_transform = nullptr,
  .set_buffer_scale     = nullptr,
  .damage_buffer =
    [](wl_client *, wl_resource *, int32_t, int32_t, int32_t, int32_t) { INFO("Damage Buffer"); },
  .offset = nullptr
};
