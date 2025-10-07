#include "barock/core/region.hpp"
#include "../log.hpp"
#include "barock/compositor.hpp"

#include <algorithm>
#include <wayland-server-core.h>

namespace barock {
  region_t
  region_t::operator+(const region_t &other) const {
    return region_t{ .x = std::min(x, other.x),
                     .y = std::min(y, other.y),
                     .w = std::max(other.x + other.w, x + w),
                     .h = std::max(other.y + other.h, y + h) };
  }

  void
  region_t::operator+=(const region_t &other) {
    *this = *this + other;
  }

  region_t
  region_t::operator-(const region_t &other) const {
    // Return a rectangle that removes a∩b (the intersection) from `a`
    int x1 = std::max(x, other.x);
    int y1 = std::max(y, other.y);
    int x2 = std::min(x + w, other.x + other.w);
    int y2 = std::min(y + h, other.y + other.h);

    if (x2 <= x1 || y2 <= y1) {
      // No intersection, return an empty region
      return region_t{ .x = 0, .y = 0, .w = 0, .h = 0 };
    }

    return region_t{ .x = x1, .y = y1, .w = x2 - x1, .h = y2 - y1 };
  }

  void
  region_t::operator-=(const region_t &other) {
    *this = *this - other;
  }

  bool
  region_t::operator==(const region_t &other) const {
    return x == other.x && y == other.y && w == other.w && h == other.h;
  }

  region_t region_t::infinite = { 0, 0, -1, -1 };
}

void
wl_region_add(wl_client *,
              wl_resource *wl_region,
              int32_t      x,
              int32_t      y,
              int32_t      width,
              int32_t      height) {
  barock::region_t *region = (barock::region_t *)wl_resource_get_user_data(wl_region);
  *region                  = *region + barock::region_t{ x, y, width, height };
}

void
wl_region_subtract(wl_client *,
                   wl_resource *wl_region,
                   int32_t      x,
                   int32_t      y,
                   int32_t      width,
                   int32_t      height) {
  barock::region_t *region = (barock::region_t *)wl_resource_get_user_data(wl_region);
  *region                  = *region - barock::region_t{ x, y, width, height };
}

void
wl_region_destroy(wl_client *, wl_resource *wl_region) {
  barock::region_t *region = (barock::region_t *)wl_resource_get_user_data(wl_region);
  delete region;
}

// Wayland protocol implementation
struct wl_region_interface wl_region_impl{ .destroy  = wl_region_destroy,
                                           .add      = wl_region_add,
                                           .subtract = wl_region_subtract };
