#pragma once

#include <wayland-server-core.h>
#include <wayland-server.h>

#include <map>
#include <memory>

namespace barock {
  class xdg_shell_t;
  class wl_compositor_t;
  class shm_t;

  class compositor_t {
    private:
    wl_display    *display_;
    wl_event_loop *event_loop_;

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
  };

};
