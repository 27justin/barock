#pragma once

#include "barock/core/region.hpp"
#include "barock/core/signal.hpp"
#include "barock/core/wl_subcompositor.hpp"
#include "barock/resource.hpp"
#include "wl/wayland-protocol.h"

#include <cstdint>
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
    region_t                           opaque;
    region_t                           input;
    region_t                           damage;
    shared_t<resource_t<shm_buffer_t>> buffer;
    int32_t                            transform;
    int32_t                            scale;
    struct {
      int32_t x, y;
    } offset;

    // struct {
    //   std::vector<shared_t<subsurface_t>> children;
    //   weak_t<surface_t>                   parent;
    // } subsurface;
    shared_t<subsurface_t>              subsurface;
    std::vector<shared_t<subsurface_t>> children;
  };

  struct surface_t {
    compositor_t *compositor;

    wl_resource *frame_callback, *wl_surface;

    surface_state_t state,
      staging; // Surface state is double buffered

    weak_t<surface_t> parent;
    int32_t           x{}, y{};

    shared_t<base_surface_role_t> role;

    signal_t<shm_buffer_t &> on_buffer_attach;

    /// Create an empty surface with nothing associated.
    surface_t();

    /// Move surfaces
    surface_t(surface_t &&);

    // These are deleted to prevent copying, since surfaces are not
    // duplicatable by nature.
    surface_t(const surface_t &) = delete;

    void
    operator=(const surface_t &) = delete;

    region_t
    extent() const;

    /**
     * @brief Compute the full extent of a surface by recursively adding up buffer sizes.
     * The returned region encompasses a region that the entire tree of surfaces takes up.
     */
    region_t
    full_extent() const;

    region_t
    position() const;

    /**
     * @brief Lookup a subsurface at the given coordinates, returns a
     * empty `nullptr` shared when none found, or out of bounds.
     *
     * @param x Horizontal position relative to the surface.
     * @param y Vertical position relative to the surface.
     */
    shared_t<surface_t>
    lookup_at(double x, double y);

    bool
    has_role() const;

    shared_t<surface_t>
    find_parent(const std::function<bool(shared_t<surface_t> &)> &) const;

    shared_t<surface_t>
    find_child(const std::function<bool(shared_t<surface_t> &)> &) const;
  };

};
