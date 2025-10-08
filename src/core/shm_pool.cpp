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

  pool->buffers.emplace_back(buf);

  wl_resource_set_implementation(buffer, &wl_buffer_impl, buf, [](wl_resource *resource) {
    barock::shm_buffer_t *buf = (barock::shm_buffer_t *)wl_resource_get_user_data(resource);
    INFO("wl_buffer#cleanup pool: {}", (void *)buf->pool);
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
  /*
    change the size of the pool mapping

    This request will cause the server to remap the backing memory for the pool from the file
    descriptor passed when the pool was created, but using the new size. This request can only be
    used to make the pool bigger.

    This request only changes the amount of bytes that are mmapped by the server and does not touch
    the file corresponding to the file descriptor passed at creation time. It is the client's
    responsibility to ensure that the file is at least as big as the new pool size.
   */
  barock::shm_pool_t *pool = (barock::shm_pool_t *)wl_resource_get_user_data(resource);

  if (size < pool->size) {
    wl_client_post_implementation_error(client, "new size is smaller than original size");
    return;
  }

  if (munmap(pool->data, pool->size)) {
    ERROR("wl_shm_pool#resize failed with error: {}", strerror(errno));
    return;
  }

  pool->size = size;
  pool->data = mmap(nullptr, pool->size, PROT_READ | PROT_WRITE, MAP_SHARED, pool->fd, 0);
  if (!pool->data) {
    wl_client_post_no_memory(client);
    return;
  }
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
    if (pool->buffers.size() > 0) {
      pool->marked_delete = true;
    } else {
      delete pool;
    }
  }

  void *
  shm_buffer_t::data() {
    return (void *)(((uintptr_t)pool->data) + offset);
  }
};
