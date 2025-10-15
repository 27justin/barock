#pragma once

#include <cstdint>
#include <wayland-server-protocol.h>

namespace barock {
  struct region_t {
    static region_t infinite;
    int32_t         x, y, w, h;

    region_t
    operator+(const region_t &) const;

    void
    operator+=(const region_t &);

    region_t
    operator-(const region_t &) const;

    void
    operator-=(const region_t &);

    bool
    operator==(const region_t &) const;

    bool
    intersects(int32_t x, int32_t y) const;

    region_t
    union_with(const region_t &other) const;
  };

  // Wayland protocol implementation
  struct compositor_t;
  struct wl_region_t {
    public:
    static constexpr int VERSION = 1;
    wl_global           *wl_region_global;

    wl_region_t(compositor_t &);
    static void
    bind(wl_client *, void *, uint32_t version, uint32_t id);
  };
}

extern struct wl_region_interface wl_region_impl;
