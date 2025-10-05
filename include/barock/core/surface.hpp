#pragma once

#include "barock/core/signal.hpp"
#include "wl/wayland-protocol.h"
#include <atomic>

extern struct wl_surface_interface wl_surface_impl;

namespace barock {
  struct compositor_t;
  struct shm_buffer_t;

  struct base_surface_role_t {
    base_surface_role_t()          = default;
    virtual ~base_surface_role_t() = default;
    virtual const void *
    type_id() = 0;
  };

  template<typename CRTP>
  struct surface_role_t : base_surface_role_t {
    public:
    surface_role_t()  = default;
    ~surface_role_t() = default;

    static const void *
    id() {
      static const int marker = 0;
      return &marker;
    }

    const void *
    type_id() override {
      return id();
    }
  };

  struct base_surface_t {
    compositor_t     *compositor;
    wl_resource      *buffer, *callback;
    std::atomic<bool> is_dirty;

    base_surface_role_t *role;

    signal_t<shm_buffer_t &> on_buffer_attached;

    /// Create an empty surface with nothing associated.
    base_surface_t();

    /// Move surfaces
    base_surface_t(base_surface_t &&);

    // These are deleted to prevent copying, since surfaces are not
    // duplicatable by nature.
    base_surface_t(const base_surface_t &) = delete;
    void
    operator=(const base_surface_t &) = delete;
  };

};
