#pragma once

#include "barock/core/region.hpp"
#include "barock/core/signal.hpp"
#include "barock/core/wl_subcompositor.hpp"
#include "barock/resource.hpp"
#include "wl/wayland-protocol.h"

#include <vector>

extern struct wl_surface_interface wl_surface_impl;

namespace barock {
  struct compositor_t;
  struct shm_buffer_t;
  struct subsurface_t;

  struct base_surface_role_t {
    base_surface_role_t()          = default;
    virtual ~base_surface_role_t() = default;
    virtual uintptr_t
    type_id() const = 0;
  };

  template<typename CRTP>
  struct surface_role_t : public base_surface_role_t {
    public:
    surface_role_t()  = default;
    ~surface_role_t() = default;

    static uintptr_t
    id() {
      static const int marker = 0;
      return (uintptr_t)&marker;
    }

    uintptr_t
    type_id() const override {
      return id();
    }
  };

  struct surface_t;
  struct surface_state_t {
    region_t     opaque;
    region_t     input;
    region_t     damage;
    wl_resource *buffer;
    int32_t      transform;
    int32_t      scale;
    struct {
      int32_t x, y;
    } offset;

    std::vector<shared_t<resource_t<subsurface_t>>> subsurfaces;
  };

  struct surface_t {
    compositor_t *compositor;

    wl_resource *frame_callback, *wl_surface;

    surface_state_t state,
      staging; // Surface state is double buffered

    shared_t<base_surface_role_t> role;

    signal_t<shm_buffer_t &> on_buffer_attached;

    /// Create an empty surface with nothing associated.
    surface_t();

    /// Move surfaces
    surface_t(surface_t &&);

    // These are deleted to prevent copying, since surfaces are not
    // duplicatable by nature.
    surface_t(const surface_t &) = delete;

    void
    operator=(const surface_t &) = delete;

    void
    extent(int32_t &, int32_t &, int32_t &, int32_t &) const;
  };

};
