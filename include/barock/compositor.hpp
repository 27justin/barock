#pragma once

#include <wayland-server-core.h>
#include <wayland-server.h>

#include <memory>
#include <mutex>
#include <queue>

namespace barock {
  class xdg_shell_t;
  class wl_compositor_t;
  class shm_t;
  struct base_surface_t;

  class compositor_t {
    private:
    wl_display    *display_;
    wl_event_loop *event_loop_;

    std::mutex                                                frame_updates_lock;
    std::queue<std::pair<barock::base_surface_t *, uint32_t>> frame_updates;
    wl_event_source                                          *frame_event_source;

    public:
    std::unique_ptr<xdg_shell_t>     xdg_shell;
    std::unique_ptr<wl_compositor_t> wl_compositor;
    std::unique_ptr<shm_t>           shm;

    compositor_t();
    ~compositor_t();

    void
    run();
    int
    redraw();

    wl_display *
    display();

    void
    schedule_frame_done(base_surface_t *surface, uint32_t timestamp);
    static int
    frame_done_flush_callback(void *);
  };

};
