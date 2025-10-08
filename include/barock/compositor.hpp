#pragma once

#include <wayland-server-core.h>
#include <wayland-server.h>

#include "../drm/minidrm.hpp"
#include "resource.hpp"

#include <memory>
#include <mutex>
#include <queue>

namespace barock {
  struct xdg_shell_t;
  struct wl_compositor_t;
  struct shm_t;
  struct dmabuf_t;
  struct surface_t;
  struct input_t;
  struct wl_subcompositor_t;
  struct wl_seat_t;
  struct wl_data_device_manager_t;
  struct wl_output_t;

  class compositor_t {
    private:
    wl_display *display_;

    std::mutex                                           frame_updates_lock;
    std::queue<std::pair<barock::surface_t *, uint32_t>> frame_updates;
    wl_event_source                                     *frame_event_source;

    public:
    minidrm::drm::handle_t drm_handle;

    wl_event_loop                            *event_loop_;
    std::unique_ptr<xdg_shell_t>              xdg_shell;
    std::unique_ptr<wl_compositor_t>          wl_compositor;
    std::unique_ptr<shm_t>                    shm;
    std::unique_ptr<dmabuf_t>                 dmabuf;
    std::unique_ptr<input_t>                  input;
    std::unique_ptr<wl_subcompositor_t>       wl_subcompositor;
    std::unique_ptr<wl_seat_t>                wl_seat;
    std::unique_ptr<wl_data_device_manager_t> wl_data_device_manager;
    std::unique_ptr<wl_output_t>              wl_output;

    struct {
      double x;
      double y;
      struct {
        int32_t x, y;
      } hotspot;
      surface_t *surface;
    } cursor;

    struct _pointer {
      compositor_t                 *root;
      weak_t<resource_t<surface_t>> focus{};

      void
      send_enter(shared_t<resource_t<surface_t>> &);

      void
      send_button(shared_t<resource_t<surface_t>> &, uint32_t, uint32_t);

      void
      send_motion(shared_t<resource_t<surface_t>> &);

      void
      send_leave(shared_t<resource_t<surface_t>> &);

      /// Set the focus to another surface, use nullptr to clear the focus
      void set_focus(shared_t<resource_t<surface_t>>);
    } pointer;

    struct _keyboard {
      compositor_t                 *root;
      weak_t<resource_t<surface_t>> focus{};

      void
      send_enter(shared_t<resource_t<surface_t>> &);

      void
      send_key(shared_t<resource_t<surface_t>> &, uint32_t, uint32_t);

      /// Set the focus to another surface, use nullptr to clear the focus
      void set_focus(shared_t<resource_t<surface_t>>);
    } keyboard;

    compositor_t(minidrm::drm::handle_t drm_handle, const std::string &seat);
    ~compositor_t();

    wl_display *
    display();

    void
    schedule_frame_done(surface_t *surface, uint32_t timestamp);

    static int
    frame_done_flush_callback(void *);
  };

};
