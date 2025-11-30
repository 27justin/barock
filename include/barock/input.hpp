#pragma once

#include <libinput.h>
#include <libudev.h>
#include <string>

#include "core/signal.hpp"

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

  struct input_t {
    public:
    // Generic libuv events
    signal_t<struct libinput_device *> on_device_add;
    signal_t<void>                     on_device_remove;

    // Mouse events
    signal_t<mouse_event_t>  on_mouse_move;
    signal_t<mouse_button_t> on_mouse_button;
    signal_t<mouse_axis_t>   on_mouse_scroll;

    // Keyboard events
    signal_t<keyboard_event_t> on_keyboard_input;

    std::vector<struct libinput_device *> devices;

    /// Poll for events on the libinput socket, returns number of emitted events.
    int
    poll(int timeout = -1);

    input_t(const std::string &seat);
    input_t(const input_t &) = delete;
    ~input_t();

    private:
    libinput_interface interface_;
    struct libinput   *input_;
    struct udev       *udev_;
    int                fd_;
  };
}
