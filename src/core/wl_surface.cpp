#include "barock/core/wl_surface.hpp"
#include "barock/core/shm_pool.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

void
damage(wl_client   *client,
       wl_resource *surface,
       int32_t      x,
       int32_t      y,
       int32_t      width,
       int32_t      height) {
  INFO("damage\n  x: {}, y: {}\n  w: {}, h: {}", x, y, width, height);
}

void
commit(wl_client *client, wl_resource *surface) {
  INFO("commit surface changes");
}

uint32_t current_time_msec2() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void
frame(wl_client *client, wl_resource *wl_surface, uint32_t callback) {
  INFO("frame");
  barock::surface_t *surface = (barock::surface_t *) wl_resource_get_user_data(wl_surface);

  wl_resource *callback_res = wl_resource_create(client, &wl_callback_interface, wl_resource_get_version(wl_surface), callback);

  // Mark the surface dirty to be re-rendered.
  surface->is_dirty.store(true);
  surface->callback = callback_res;

  // Done by main loop currently
  // wl_callback_send_done(callback_res, current_time_msec2());
  // wl_resource_destroy(callback_res);
  // wl_buffer_send_release(surface->buffer);
}


void
attach(wl_client *client, wl_resource *wl_surface, wl_resource *buffer, int32_t x, int32_t y) {
  barock::shm_buffer_t *shm_buffer = (barock::shm_buffer_t *) wl_resource_get_user_data(buffer);

  // Set the buffer to our surface
  barock::surface_t *surface = (barock::surface_t *) wl_resource_get_user_data(wl_surface);
  surface->buffer = buffer;
}

struct wl_surface_interface wl_surface_impl = {
    .destroy = nullptr,
    .attach = attach,
    .damage = damage,
    .frame = frame,
    .set_opaque_region = nullptr,
    .set_input_region = nullptr,
    .commit = commit,
    .set_buffer_transform = nullptr,
    .set_buffer_scale = nullptr,
    .damage_buffer = nullptr,
    .offset = nullptr
};
