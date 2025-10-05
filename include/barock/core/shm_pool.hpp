#pragma once

#include "wl/wayland-protocol.h"
#include <vector>

extern struct wl_shm_pool_interface wl_shm_pool_impl;

namespace barock {
  struct shm_buffer_t;

  struct shm_pool_t {
    public:
    wl_resource                *resource;
    void                       *data;
    int32_t                     size;
    int32_t                     fd;
    std::vector<shm_buffer_t *> buffers;
    bool                        marked_delete;

    shm_pool_t(wl_resource *res, int fd, int size, void *ptr);
    ~shm_pool_t();

    static void
    destroy(wl_resource *);
  };

  struct shm_buffer_t {
    barock::shm_pool_t *pool;
    wl_resource        *resource;
    int32_t             offset, width, height, stride;
    uint32_t            format;
    void               *data;
  };
};
