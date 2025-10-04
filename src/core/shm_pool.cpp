#include "barock/core/shm_pool.hpp"
#include "wl/wayland-protocol.h"

#include "../log.hpp"
#include <wayland-server-core.h>

struct wl_buffer_interface wl_buffer_impl{ .destroy = [](wl_client *, wl_resource *buffer) {
  barock::shm_pool_t      &pool =
    *reinterpret_cast<barock::shm_pool_t *>(wl_resource_get_user_data(buffer));
  ERROR("Destroy SHM buffer");
} };

void
create_buffer(struct wl_client   *client,
              struct wl_resource *wl_shm_pool,
              uint32_t            id,
              int32_t             offset,
              int32_t             width,
              int32_t             height,
              int32_t             stride,
              uint32_t            format) {
  ERROR("shm pool :: create buffer");

  wl_resource *buffer =
    wl_resource_create(client, &wl_buffer_interface, wl_resource_get_version(wl_shm_pool), id);
  if (!buffer) {
    wl_client_post_no_memory(client);
    return;
  }

  barock::shm_buffer_t *buf = new barock::shm_buffer_t;
  buf->pool                 = (barock::shm_pool_t *)wl_resource_get_user_data(wl_shm_pool);
  buf->resource             = buffer;
  buf->offset               = offset;
  buf->width                = width;
  buf->height               = height;
  buf->stride               = stride;
  buf->format               = format;
  buf->data = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buf->pool->data) + offset);

  INFO("create buffer\n  offset: {}\n  width: {}\n  height: {}\n  stride: {}\n  format: {}", offset,
       width, height, stride, format);

  wl_resource_set_user_data(buffer, buf);
  wl_resource_set_implementation(buffer, &wl_buffer_impl, buf, [](wl_resource *resource) {
    barock::shm_buffer_t *buf = (barock::shm_buffer_t *)wl_resource_get_user_data(resource);
    delete buf;
  });
}

void
destroy(struct wl_client *client, struct wl_resource *resource) {
  ERROR("shm pool :: destroy");
}

void
resize(struct wl_client *client, struct wl_resource *resource, int32_t size) {
  ERROR("shm pool :: resize");
}

struct wl_shm_pool_interface wl_shm_pool_impl = { .create_buffer = create_buffer,
                                                  .destroy       = destroy,
                                                  .resize        = resize };

namespace barock {

  shm_pool_t::shm_pool_t(wl_resource *res, int fd, int size, void *ptr)
    : resource(res)
    , data(ptr)
    , size(size)
    , fd(fd) {}

  shm_pool_t::~shm_pool_t() {
    free(data);
  }

  void
  shm_pool_t::destroy(wl_resource *res) {
    ERROR("shm_pool::destroy");
    shm_pool_t *pool = reinterpret_cast<shm_pool_t *>(wl_resource_get_user_data(res));
    delete pool;
  }

};
