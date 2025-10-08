#pragma once

#include "barock/resource.hpp"

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
  struct seat_t;

  struct wl_pointer_t {
    shared_t<resource_t<seat_t>> seat;
  };

  struct wl_keyboard_t {
    shared_t<resource_t<seat_t>> seat;
  };

  struct seat_t {
    wl_seat_t *interface;

    weak_t<resource_t<wl_pointer_t>>  pointer;
    weak_t<resource_t<wl_keyboard_t>> keyboard;
  };

  struct wl_seat_t {
    public:
    static constexpr int                                VERSION = 9;
    std::map<wl_client *, shared_t<resource_t<seat_t>>> seats;

    compositor_t &compositor;
    wl_global    *wl_seat_global;
    wl_seat_t(compositor_t &);
    ~wl_seat_t();

    shared_t<resource_t<seat_t>>
    find(wl_client *);

    static void
    bind(wl_client *, void *, uint32_t version, uint32_t id);
  };

} // namespace barock
