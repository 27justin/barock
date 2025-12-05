#pragma once

#include <wayland-server-core.h>
#include <wayland-server.h>

#include "../drm/minidrm.hpp"

#include "resource.hpp"

// output_t
#include "barock/core/event_bus.hpp"
#include "barock/core/output.hpp"

#include <janet.h>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace barock {
  struct dmabuf_t;
  struct surface_t;
  struct input_t;
  struct wl_subcompositor_t;
  struct wl_seat_t;
  struct wl_data_device_manager_t;
  struct wl_output_t;
  struct hotkey_t;

  // Forward declaratios
  class event_loop_t;
  class input_manager_t;
  class cursor_manager_t;
  class output_manager_t;
  class wl_compositor_t;
  class shm_t;
  class xdg_shell_t;

  class compositor_t {
    private:
    wl_display *display_;

    public:
    minidrm::drm::handle_t drm_handle;

    wl_event_loop *event_loop_;
    JanetTable    *context_;

    std::unique_ptr<event_loop_t>       event_loop;
    std::unique_ptr<input_manager_t>    input;
    std::unique_ptr<cursor_manager_t>   cursor;
    std::unique_ptr<output_manager_t>   output;
    std::unique_ptr<wl_compositor_t>    wl_compositor;
    std::unique_ptr<wl_subcompositor_t> wl_subcompositor;
    std::unique_ptr<shm_t>              shm;
    std::unique_ptr<hotkey_t>           hotkey;
    std::unique_ptr<xdg_shell_t>        xdg_shell;
    std::unique_ptr<wl_seat_t>          wl_seat;
    std::unique_ptr<wl_output_t>        wl_output;

    std::unique_ptr<wl_data_device_manager_t> wl_data_device_manager;

    std::unique_ptr<event_bus_t> event_bus;

    compositor_t(minidrm::drm::handle_t drm_handle, const std::string &seat);
    ~compositor_t();

    wl_display *
    display();

    void
    load_file(const std::string &path);
  };

  struct barock_userdata_t {
    compositor_t *compositor;
  };

  extern JanetAbstractType const barock_at;
};
