#include "barock/compositor.hpp"

#include "barock/core/region.hpp"
#include "barock/core/shm.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/core/wl_data_device_manager.hpp"
#include "barock/core/wl_output.hpp"
#include "barock/core/wl_seat.hpp"
#include "barock/core/wl_subcompositor.hpp"

#include "barock/dmabuf/dmabuf.hpp"
#include "barock/input.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "log.hpp"

#include <EGL/eglext.h>
#include <cstdint>
#include <wayland-egl-backend.h>
#include <wayland-server-core.h>

namespace barock {
  int
  compositor_t::frame_done_flush_callback(void *data) {
    barock::compositor_t *compositor = static_cast<barock::compositor_t *>(data);

    std::lock_guard lock(compositor->frame_updates_lock);

    while (!compositor->frame_updates.empty()) {
      auto &[surface, timestamp] = compositor->frame_updates.front();

      if (surface->frame_callback) {
        wl_callback_send_done(surface->frame_callback, timestamp);
        wl_resource_destroy(surface->frame_callback);
        surface->frame_callback = nullptr;
      }

      if (surface->state.buffer) {
        wl_buffer_send_release(surface->state.buffer);
        // surface->buffer = nullptr; // optional if reused
      }

      compositor->frame_updates.pop();
    }

    return 0;
  }

  compositor_t::compositor_t(minidrm::drm::handle_t drm_handle, const std::string &seat)
    : drm_handle(drm_handle) {
    using std::make_unique;
    display_ = wl_display_create();
    wl_display_add_socket(display_, nullptr);
    event_loop_ = wl_display_get_event_loop(display_);

    // Initialize input devices
    input = make_unique<input_t>(seat);

    // Initialize protocols
    xdg_shell              = make_unique<xdg_shell_t>(*this);
    wl_compositor          = make_unique<wl_compositor_t>(*this);
    shm                    = make_unique<shm_t>(display_);
    dmabuf                 = make_unique<dmabuf_t>(*this);
    wl_subcompositor       = make_unique<wl_subcompositor_t>(*this);
    wl_seat                = make_unique<wl_seat_t>(*this);
    wl_data_device_manager = make_unique<wl_data_device_manager_t>(*this);
    wl_output              = make_unique<wl_output_t>(*this);

    cursor.x = 0.;
    cursor.y = 0.;
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
  compositor_t::schedule_frame_done(surface_t *surface, uint32_t timestamp) {
    std::lock_guard lock(frame_updates_lock);
    frame_updates.push(std::pair<barock::surface_t *, uint32_t>(surface, timestamp));
  }

} // namespace barock
