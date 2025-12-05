#pragma once

#include <libinput.h>
#include <span>
#include <string>
#include <xkbcommon/xkbcommon.h>

#include "barock/core/signal.hpp"

namespace barock {
  struct mouse_event_t {
    struct libinput_event         *event;
    struct libinput_event_pointer *pointer;
  };

  struct mouse_scroll_t {
    double delta;
  };

  struct mouse_button_t {
    uint32_t button;
    enum { released, pressed } state;
  };

  struct mouse_axis_t {
    libinput_event_pointer *event;
    double                  horizontal, vertical;
  };

  struct keyboard_event_t {
    struct libinput_event          *event;
    struct libinput_event_keyboard *keyboard;
  };

  struct input_manager_t {
    private:
    std::vector<struct libinput_device *> devices_;

    libinput_interface interface_;
    libinput          *libinput_;
    udev              *udev_;
    int                fd_;

    public:
    signal_t<struct libinput_device *> on_device_add;
    signal_t<void>                     on_device_remove;

    signal_t<mouse_event_t>  on_mouse_move;
    signal_t<mouse_button_t> on_mouse_click;
    signal_t<mouse_axis_t>   on_mouse_scroll;

    signal_t<keyboard_event_t> on_keyboard_input;

    struct _xkb {
      xkb_context *context;
      xkb_keymap  *keymap;
      xkb_state   *state;
      char        *keymap_string;
    } xkb;

    input_manager_t(const std::string &xdg_seat);
    ~input_manager_t();

    int
    fd() const;

    int
    poll(int timeout = -1);

    std::span<libinput_device *>
    devices();
  };
}
