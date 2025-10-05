#include "barock/core/shm_pool.hpp"
#include "wl/wayland-protocol.h"
#include <sys/mman.h>

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
  auto pool = (barock::shm_pool_t *)wl_resource_get_user_data(wl_shm_pool);

  wl_resource *buffer =
    wl_resource_create(client, &wl_buffer_interface, wl_resource_get_version(wl_shm_pool), id);
  if (!buffer) {
    wl_client_post_no_memory(client);
    return;
  }

  barock::shm_buffer_t *buf = new barock::shm_buffer_t;
  buf->pool                 = pool;
  buf->resource             = buffer;
  buf->offset               = offset;
  buf->width                = width;
  buf->height               = height;
  buf->stride               = stride;
  buf->format               = format;
  buf->data = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buf->pool->data) + offset);

  pool->buffers.emplace_back(buf);

  INFO("create buffer\n  offset: {}\n  width: {}\n  height: {}\n  stride: {}\n  format: {}", offset,
       width, height, stride, format);

  wl_resource_set_user_data(buffer, buf);
  wl_resource_set_implementation(buffer, &wl_buffer_impl, buf, [](wl_resource *resource) {
    barock::shm_buffer_t *buf = (barock::shm_buffer_t *)wl_resource_get_user_data(resource);
    // Remove the buffer from the shm_pool_t.
    // Since the shm_pool_t is reference counted (The mmapped memory will be released when all
    // buffers that have been created from this pool are gone.), we also check for buffers.
    // When none are present, and the pool is marked for deletion, we delete it.
    auto it = std::find(buf->pool->buffers.begin(), buf->pool->buffers.end(), buf);
    if (it != buf->pool->buffers.end())
      buf->pool->buffers.erase(it);

    if (buf->pool->marked_delete && buf->pool->buffers.size() == 0) {
      // References are 0, we can delete it.
      delete buf->pool;
    }
    delete buf;
  });
}

void
destroy(struct wl_client *client, struct wl_resource *resource) {
  /*
    destroy the pool

    Destroy the shared memory pool.

    The mmapped memory will be released when all buffers that have been created from this pool are
    gone.
   */
  ERROR("shm pool :: destroy");
  barock::shm_pool_t *pool = static_cast<barock::shm_pool_t *>(wl_resource_get_user_data(resource));
  // Only delete the associated memory when no buffers hold the memory anymore.
  if (pool->buffers.size() == 0) {
    delete pool;
  } else {
    // Otherwise mark for deletion, then it will be cleaned up by the
    // wl_resource cleanup function of wl_buffer. (see above.)
    pool->marked_delete = true;
  }
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
    munmap(data, size);
  }

  void
  shm_pool_t::destroy(wl_resource *res) {
    shm_pool_t *pool = reinterpret_cast<shm_pool_t *>(wl_resource_get_user_data(res));
    delete pool;
  }

};
