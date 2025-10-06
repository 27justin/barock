#include "barock/input.hpp"
#include "log.hpp"
#include <fcntl.h>
#include <libinput.h>
#include <memory>
#include <stdexcept>
#include <sys/poll.h>
#include <unistd.h>

static int
open_restriced(const char *path, int flags, void *ud) {
  int fd;
  if (fd = open(path, flags); fd < 0) {
    ERROR("Failed to open libinput device '{}': {}", path, strerror(errno));
    throw std::runtime_error("Failed to open libinput device");
  }
  return fd;
}

static void
close_restricted(int fd, void *) {
  close(fd);
}

namespace barock {

  input_t::input_t(const std::string &seat) {
    interface_.open_restricted  = open_restriced;
    interface_.close_restricted = close_restricted;

    udev_ = udev_new();
    if (!udev_) {
      throw std::runtime_error("Failed to open udev");
    }

    input_ = libinput_udev_create_context(&interface_, this, udev_);

    if (libinput_udev_assign_seat(input_, seat.c_str())) {
      throw std::runtime_error("Failed to assign seat");
    }

    fd_ = libinput_get_fd(input_);
  }

  int
  input_t::poll(int timeout) {
    int events = 0;

    struct pollfd pollfd{ .fd = fd_, .events = POLLIN };
    if (::poll(&pollfd, 1, timeout) <= 0) {
      WARN("libinput poll timed out");
      return 0;
    }

    libinput_dispatch(input_);

    struct libinput_event *event;
    while ((event = libinput_get_event(input_)) != nullptr) {
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
          on_mouse_button.emit(
            mouse_button_t{ button, static_cast<decltype(mouse_button_t::state)>(state) });
          break;
        }
        case LIBINPUT_EVENT_POINTER_AXIS: {
          on_mouse_scroll.emit(event);
          break;
        }
        case LIBINPUT_EVENT_KEYBOARD_KEY: {
          struct libinput_event_keyboard *k = libinput_event_get_keyboard_event(event);
          on_keyboard_input.emit({ event, k });
          break;
        }
        case LIBINPUT_EVENT_DEVICE_ADDED: {
          on_device_add.emit(libinput_event_get_device(event));
          break;
        }
        case LIBINPUT_EVENT_DEVICE_REMOVED: {
          on_device_remove.emit();
          break;
        }
        default:
          WARN("Unknown type '{}'", (int)type);
      }
      libinput_event_destroy(event);
    }

    return events;
  }

  input_t::~input_t() {
    libinput_unref(input_);
    udev_unref(udev_);
  }

}
