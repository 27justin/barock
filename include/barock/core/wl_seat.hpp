#pragma once

#include "barock/core/cursor_manager.hpp"
#include "barock/core/input.hpp"
#include "barock/core/signal.hpp"
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
  struct service_registry_t;

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

    service_registry_t &registry;

    wl_display *display;
    wl_global  *wl_seat_global;
    wl_seat_t(wl_display *, service_registry_t &);
    ~wl_seat_t();

    shared_t<resource_t<seat_t>>
    find(wl_client *);

    void set_keyboard_focus(shared_t<resource_t<surface_t>>);

    void set_mouse_focus(shared_t<resource_t<surface_t>>);

    private:
    static void
    bind(wl_client *, void *, uint32_t version, uint32_t id);

    shared_t<resource_t<surface_t>>
    find_best_surface(fpoint_t cursor) const;

    struct {
      weak_t<resource_t<surface_t>> pointer;
      weak_t<resource_t<surface_t>> keyboard;
    } focus_;

    signal_action_t on_keyboard_input(keyboard_event_t);
    signal_action_t on_mouse_click(mouse_button_t);
    signal_action_t on_mouse_move(mouse_event_t);
  };

} // namespace barock
