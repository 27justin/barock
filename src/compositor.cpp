#include "barock/compositor.hpp"

#include "barock/core/shm.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "log.hpp"

#include <wayland-server-core.h>

namespace barock {

  compositor_t::compositor_t() {
    using std::make_unique;
    display_ = wl_display_create();
    wl_display_add_socket(display_, nullptr);
    event_loop_ = wl_display_get_event_loop(display_);

    // Initialize protocols
    xdg_shell = make_unique<xdg_shell_t>(*this);
    wl_compositor = make_unique<wl_compositor_t>(display_);
    shm = make_unique<shm_t>(display_);

    // wl_event_source *source = wl_event_loop_add_timer(event_loop_, [](void *ud) {
    //   return static_cast<compositor_t *>(ud)->redraw();
    // }, this);
    // wl_event_source_timer_update(source, 16);
  }

  compositor_t::~compositor_t() {}

  wl_display *
  compositor_t::display() {
    return display_;
  }

  void
  compositor_t::run() {
    wl_display_run(display_);
    wl_display_destroy(display_);
  }

  int
  compositor_t::redraw() {
    // INFO("redraw");
    // std::exit(0);
    return 16;
  }

}

