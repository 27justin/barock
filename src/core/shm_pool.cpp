#include "barock/core/shm_pool.hpp"
#include "barock/resource.hpp"
#include "wl/wayland-protocol.h"
#include <sys/mman.h>

#include "../log.hpp"
#include <wayland-server-core.h>

using namespace barock;

void
wl_buffer_destroy(wl_client *, wl_resource *);

void
wl_shm_pool_create_buffer(wl_client   *client,
                          wl_resource *wl_shm_pool,
                          uint32_t     id,
                          int32_t      offset,
                          int32_t      width,
                          int32_t      height,
                          int32_t      stride,
                          uint32_t     format);

void
wl_shm_pool_destroy(wl_client *, wl_resource *);

void
wl_shm_pool_resize(wl_client *client, wl_resource *resource, int32_t size);

struct wl_buffer_interface wl_buffer_impl = { .destroy = wl_buffer_destroy };

struct wl_shm_pool_interface wl_shm_pool_impl = { .create_buffer = wl_shm_pool_create_buffer,
                                                  .destroy       = wl_shm_pool_destroy,
                                                  .resize        = wl_shm_pool_resize };

void
wl_shm_pool_create_buffer(wl_client   *client,
                          wl_resource *wl_shm_pool,
                          uint32_t     id,
                          int32_t      offset,
                          int32_t      width,
                          int32_t      height,
                          int32_t      stride,
                          uint32_t     format) {
  auto pool = from_wl_resource<shm_pool_t>(wl_shm_pool);

  auto buffer = make_resource<shm_buffer_t>(
    client, wl_buffer_interface, wl_buffer_impl, wl_resource_get_version(wl_shm_pool), id);

  buffer->pool = pool;

  buffer->offset = offset;
  buffer->width  = width;
  buffer->height = height;
  buffer->stride = stride;
  buffer->format = format;

  buffer->on_destroy.connect([](wl_resource *resource) mutable {
    auto buffer = from_wl_resource<shm_buffer_t>(resource);

    auto it = std::find(buffer->pool->buffers.begin(), buffer->pool->buffers.end(), buffer);
    if (it != buffer->pool->buffers.end())
      buffer->pool->buffers.erase(it);
  });

  pool->buffers.emplace_back(buffer);
}

void
wl_shm_pool_destroy(wl_client *client, wl_resource *wl_shm_pool) {
  /*
    destroy the pool

    Destroy the shared memory pool.

    The `mmap`ed memory will be released when all buffers that have been created from this pool are
    gone.
   */
  auto pool = from_wl_resource<shm_pool_t>(wl_shm_pool);
  wl_resource_destroy(wl_shm_pool);
}

void
wl_shm_pool_resize(wl_client *client, wl_resource *wl_shm_pool, int32_t size) {
  /*
    change the size of the pool mapping

    This request will cause the server to remap the backing memory for the pool from the file
    descriptor passed when the pool was created, but using the new size. This request can only be
    used to make the pool bigger.

    This request only changes the amount of bytes that are mmapped by the server and does not touch
    the file corresponding to the file descriptor passed at creation time. It is the client's
    responsibility to ensure that the file is at least as big as the new pool size.
   */
  auto pool = from_wl_resource<shm_pool_t>(wl_shm_pool);
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
  if (pool->data == MAP_FAILED) {
    wl_client_post_no_memory(client);
    return;
  }
}

void
wl_buffer_destroy(wl_client *, wl_resource *wl_buffer) {
  auto buffer = from_wl_resource<shm_buffer_t>(wl_buffer);
  wl_resource_destroy(wl_buffer);
}

namespace barock {

  shm_pool_t::shm_pool_t(int fd, int size, void *ptr)
    : data(ptr)
    , size(size)
    , fd(fd) {}

  shm_pool_t::~shm_pool_t() {
    munmap(data, size);
  }

  void *
  shm_buffer_t::data() {
    return (void *)(((uintptr_t)pool->data) + offset);
  }
};
