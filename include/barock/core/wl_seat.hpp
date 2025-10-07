#pragma once

#include "wl/wayland-protocol.h"
#include <map>
#include <wayland-server-protocol.h>

extern struct wl_seat_interface     wl_seat_impl;
extern struct wl_pointer_interface  wl_pointer_impl;
extern struct wl_keyboard_interface wl_keyboard_impl;

namespace barock {
  struct compositor_t;
  struct wl_seat_t;
  struct surface_t;

  struct seat_t {
    wl_seat_t   *wl_seat;
    wl_resource *pointer, *keyboard, *touch;
    surface_t   *pointer_focus = nullptr;
  };

  struct wl_seat_t {
    public:
    static constexpr int            VERSION = 9;
    std::map<wl_client *, seat_t *> seats;

    compositor_t &compositor;
    wl_global    *wl_seat_global;
    wl_seat_t(compositor_t &);

    static void
    bind(wl_client *, void *, uint32_t version, uint32_t id);
  };

} // namespace barock
