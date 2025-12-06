#include "barock/core/input.hpp"
#include "../log.hpp"
#include "barock/compositor.hpp"
#include <fcntl.h>
#include <libinput.h>
#include <linux/input.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <unistd.h>

static int
open_restriced(const char *path, int flags, void *ud) {
  int fd;
  if (fd = open(path, flags); fd < 0) {
    ERROR("Failed to open libinput device '{}': {}", path, strerror(errno));
    throw std::runtime_error("Failed to open libinput device");
  }
  ioctl(fd, EVIOCGRAB, 1);
  return fd;
}

static void
close_restricted(int fd, void *) {
  close(fd);
}

using namespace barock;

input_manager_t::input_manager_t(const std::string &xdg_seat, service_registry_t &registry)
  : registry(registry) {
  interface_.open_restricted  = open_restriced;
  interface_.close_restricted = close_restricted;

  udev_ = udev_new();
  if (!udev_) {
    throw std::runtime_error("Failed to open udev");
  }

  libinput_ = libinput_udev_create_context(&interface_, this, udev_);

  if (libinput_udev_assign_seat(libinput_, xdg_seat.c_str())) {
    throw std::runtime_error("Failed to assign seat");
  }

  fd_ = libinput_get_fd(libinput_);

  // Initialize XKB
  xkb.context       = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb.keymap        = xkb_keymap_new_from_names(xkb.context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  xkb.keymap_string = xkb_keymap_get_as_string(xkb.keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  xkb.state         = xkb_state_new(xkb.keymap);
}

int
input_manager_t::poll(int timeout) {
  int events = 0;

  struct pollfd pollfd{ .fd = fd_, .events = POLLIN };
  if (::poll(&pollfd, 1, timeout) <= 0) {
    return 0;
  }

  libinput_dispatch(libinput_);

  struct libinput_event *event;
  while ((event = libinput_get_event(libinput_)) != nullptr) {
    enum libinput_event_type type = libinput_event_get_type(event);
    ++events;

    switch (type) {
      case LIBINPUT_EVENT_POINTER_MOTION: {
        struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);

        on_mouse_move.emit({ .event = event, .pointer = p });
        break;
      }
      case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
        struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);

        on_mouse_move.emit({ .event = event, .pointer = p });
        break;
      }
      case LIBINPUT_EVENT_POINTER_BUTTON: {
        struct libinput_event_pointer *p      = libinput_event_get_pointer_event(event);
        uint32_t                       button = libinput_event_pointer_get_button(p);
        enum libinput_button_state     state  = libinput_event_pointer_get_button_state(p);
        on_mouse_click.emit(
          mouse_button_t{ button, static_cast<decltype(mouse_button_t::state)>(state) });
        break;
      }
      case LIBINPUT_EVENT_POINTER_AXIS: {
        // LIBINPUT_EVENT_POINTER_AXIS events are sent for regular
        // wheel clicks, usually those representing one detent on
        // the device. These wheel clicks usually require a rotation
        // of 15 or 20 degrees. This event is deprecated as of
        // libinput 1.19.
        //
        // (source: https://wayland.freedesktop.org/libinput/doc/latest/wheel-api.html)
        break;
      }
      case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL: {
        struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);
        double                         horizontal{}, vertical{};

        if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL))
          horizontal = libinput_event_pointer_get_scroll_value_v120(
            p, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);

        if (libinput_event_pointer_has_axis(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
          vertical =
            libinput_event_pointer_get_scroll_value_v120(p, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);

        on_mouse_scroll.emit(
          mouse_axis_t{ .event = p, .horizontal = horizontal, .vertical = vertical });
        break;
      }
      case LIBINPUT_EVENT_KEYBOARD_KEY: {
        struct libinput_event_keyboard *k         = libinput_event_get_keyboard_event(event);
        uint32_t                        scancode  = libinput_event_keyboard_get_key(k);
        uint32_t                        key_state = libinput_event_keyboard_get_key_state(k);
        xkb_state_update_key(xkb.state,
                             scancode + 8, // +8: evdev -> xkb
                             key_state == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
        on_keyboard_input.emit({ event, k });
        break;
      }
      case LIBINPUT_EVENT_DEVICE_ADDED: {
        auto dev = libinput_event_get_device(event);
        devices_.push_back(dev);
        on_device_add.emit(dev);
        break;
      }
      case LIBINPUT_EVENT_DEVICE_REMOVED: {
        auto dev = libinput_event_get_device(event);
        auto it  = std::find(devices_.begin(), devices_.end(), dev);
        if (it != devices_.end())
          devices_.erase(it);
        on_device_remove.emit();
        break;
      }
      default:
        WARN("Input Manager: unknown event type '{}'", (int)type);
    }
    libinput_event_destroy(event);
  }
  return events;
}

int
input_manager_t::fd() const {
  return fd_;
}

input_manager_t::~input_manager_t() {
  xkb_state_unref(xkb.state);
  xkb_keymap_unref(xkb.keymap);
  xkb_context_unref(xkb.context);
  free(xkb.keymap_string);

  libinput_unref(libinput_);
  udev_unref(udev_);
}

std::span<libinput_device *>
input_manager_t::devices() {
  return devices_;
}
