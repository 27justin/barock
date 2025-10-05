#include "barock/compositor.hpp"

#include "barock/core/shm.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "log.hpp"

#include <cstdint>
#include <wayland-server-core.h>

namespace barock {
  int
  compositor_t::frame_done_flush_callback(void *data) {
    barock::compositor_t *compositor = static_cast<barock::compositor_t *>(data);

    std::lock_guard lock(compositor->frame_updates_lock);

    while (!compositor->frame_updates.empty()) {
      auto &[surface, timestamp] = compositor->frame_updates.front();

      if (surface->callback) {
        wl_callback_send_done(surface->callback, timestamp);
        wl_resource_destroy(surface->callback);
        surface->callback = nullptr;
      }

      if (surface->buffer) {
        wl_buffer_send_release(surface->buffer);
        // surface->buffer = nullptr; // optional if reused
      }

      compositor->frame_updates.pop();
    }
    return 1;
  }

  compositor_t::compositor_t() {
    using std::make_unique;
    display_ = wl_display_create();
    wl_display_add_socket(display_, nullptr);
    event_loop_ = wl_display_get_event_loop(display_);

    // Initialize protocols
    xdg_shell     = make_unique<xdg_shell_t>(*this);
    wl_compositor = make_unique<wl_compositor_t>(*this);
    shm           = make_unique<shm_t>(display_);

    // -- Event loop subscribers

    // Create an idle source that will run in the main thread
    frame_event_source =
      wl_event_loop_add_timer(event_loop_, compositor_t::frame_done_flush_callback, this);
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

  void
  compositor_t::schedule_frame_done(base_surface_t *surface, uint32_t timestamp) {
    std::lock_guard lock(frame_updates_lock);
    frame_updates.push(std::pair<barock::base_surface_t *, uint32_t>(surface, timestamp));
  }

}
