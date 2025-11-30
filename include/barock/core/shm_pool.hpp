#pragma once

#include "barock/resource.hpp"
#include "wl/wayland-protocol.h"
#include <vector>

extern struct wl_shm_pool_interface wl_shm_pool_impl;

namespace barock {
  struct shm_buffer_t;

  struct shm_pool_t {
    void                             *data;
    int32_t                           size;
    int32_t                           fd;
    std::vector<weak_t<shm_buffer_t>> buffers;

    shm_pool_t(int fd, int size, void *ptr);
    ~shm_pool_t();
  };

  struct shm_buffer_t {
    shared_t<shm_pool_t> pool;
    int32_t              offset, width, height, stride;
    uint32_t             format;

    void *
    data();
  };
};
