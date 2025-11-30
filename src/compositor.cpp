#include "barock/compositor.hpp"
#include "barock/resource.hpp"
#include "barock/util.hpp"

#include "barock/core/region.hpp"
#include "barock/core/shm.hpp"
#include "barock/core/shm_pool.hpp"
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
#include <wayland-util.h>

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

      if (surface->state.buffer &&
          /* TODO: I am unsure why we need this check, when running
           * `foot`, we encounter a weird issue, where the client
           * destroys a wl_buffer, that is still attached.  I feel
           * like this is an issue in our code, but I'll have to delve
           * a bit deeper to understand why it happens. So for now,
           * we'll keep this check in place. */
          surface->state.buffer->resource() != nullptr) {
        wl_buffer_send_release(surface->state.buffer->resource());
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
    xdg_shell     = make_unique<xdg_shell_t>(*this);
    wl_compositor = make_unique<wl_compositor_t>(*this);
    shm           = make_unique<shm_t>(*this);
    // dmabuf                 = make_unique<dmabuf_t>(*this);
    wl_subcompositor       = make_unique<wl_subcompositor_t>(*this);
    wl_seat                = make_unique<wl_seat_t>(*this);
    wl_data_device_manager = make_unique<wl_data_device_manager_t>(*this);
    wl_output              = make_unique<wl_output_t>(*this);

    cursor.x = 0.;
    cursor.y = 0.;

    pointer.root  = this;
    keyboard.root = this;
    window.root   = this;
  }

  compositor_t::~compositor_t() {}

  wl_display *
  compositor_t::display() {
    return display_;
  }

  void
  compositor_t::schedule_frame_done(const shared_t<resource_t<surface_t>> &surface,
                                    uint32_t                               timestamp) {
    std::lock_guard lock(frame_updates_lock);
    frame_updates.push(std::pair(surface, timestamp));
  }

  void
  compositor_t::_pointer::send_enter(shared_t<resource_t<surface_t>> &surf) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    region_t bounds = surf->extent();

    double local_x{}, local_y{};

    local_x = root->cursor.x - bounds.x;
    local_y = root->cursor.y - bounds.y;

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_pointer` attached to that `wl_seat`.
    if (auto seat = wl_seat->find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_pointer_send_enter(pointer->resource(),
                              wl_display_next_serial(root->display()),
                              surf->resource(),
                              wl_fixed_from_double(local_x),
                              wl_fixed_from_double(local_y));
        wl_pointer_send_frame(pointer->resource());
      }
    }
  }

  void
  compositor_t::_pointer::send_leave(shared_t<resource_t<surface_t>> &surf) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    if (auto seat = wl_seat->find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_pointer_send_leave(
          pointer->resource(), wl_display_next_serial(root->display()), surf->resource());
        wl_pointer_send_frame(pointer->resource());
      }
    }
  }

  void
  compositor_t::_pointer::send_button(shared_t<resource_t<surface_t>> &surf,
                                      uint32_t                         button,
                                      uint32_t                         state) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    if (auto seat = wl_seat->find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_pointer_send_button(pointer->resource(),
                               wl_display_next_serial(root->display()),
                               current_time_msec(),
                               button,
                               state);
        wl_pointer_send_frame(pointer->resource());
      }
    }
  }

  void
  compositor_t::_pointer::send_motion(shared_t<resource_t<surface_t>> &surface) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surface->owner();

    if (auto seat = wl_seat->find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        auto position = surface->position();

        double local_x{}, local_y{};
        local_x = (root->cursor.x - position.x);
        local_y = (root->cursor.y - position.y);

        wl_pointer_send_motion(pointer->resource(),
                               current_time_msec(),
                               wl_fixed_from_double(local_x),
                               wl_fixed_from_double(local_y));
        wl_pointer_send_frame(pointer->resource());
      }
    }
  }

  void
  compositor_t::_pointer::send_motion(shared_t<resource_t<surface_t>> &surface,
                                      double                           x,
                                      double                           y) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surface->owner();

    if (auto seat = wl_seat->find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_pointer_send_motion(pointer->resource(),
                               current_time_msec(),
                               wl_fixed_from_double(x),
                               wl_fixed_from_double(y));
        wl_pointer_send_frame(pointer->resource());
      }
    }
  }

  void
  compositor_t::_pointer::set_focus(shared_t<resource_t<surface_t>> surf) {
    if (auto surface = focus.lock(); surface) {
      // Send leave event
      auto      &wl_seat = root->wl_seat;
      wl_client *client  = surface->owner();

      if (auto seat = wl_seat->find(client); seat) {
        if (auto pointer = seat->pointer.lock(); pointer) {
          wl_pointer_send_leave(
            pointer->resource(), wl_display_next_serial(root->display()), surface->resource());
        }
      }
    }

    focus = surf;
    if (surf)
      send_enter(surf);
  }

  void
  compositor_t::_keyboard::send_enter(shared_t<resource_t<surface_t>> &surf) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_keyboard` attached to that `wl_seat`.
    if (auto seat = wl_seat->find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {
        wl_array keys;
        wl_array_init(&keys);
        wl_keyboard_send_enter(
          keyboard->resource(), wl_display_next_serial(root->display()), surf->resource(), &keys);
        wl_array_release(&keys);
      }
    }
  }

  void
  compositor_t::_keyboard::send_leave(shared_t<resource_t<surface_t>> &surf) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_keyboard` attached to that `wl_seat`.
    if (auto seat = wl_seat->find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {
        wl_keyboard_send_leave(
          keyboard->resource(), wl_display_next_serial(root->display()), surf->resource());
      }
    }
  }

  void
  compositor_t::_keyboard::send_key(shared_t<resource_t<surface_t>> &surf,
                                    uint32_t                         key,
                                    uint32_t                         state) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    if (auto seat = wl_seat->find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {

        wl_keyboard_send_key(keyboard->resource(),
                             wl_display_next_serial(root->display()),
                             current_time_msec(),
                             key,
                             state);
      }
    }
  }

  void
  compositor_t::_keyboard::send_modifiers(shared_t<resource_t<surface_t>> &surf,
                                          uint32_t                         depressed,
                                          uint32_t                         latched,
                                          uint32_t                         locked,
                                          uint32_t                         group) {
    auto      &wl_seat = root->wl_seat;
    wl_client *client  = surf->owner();

    if (auto seat = wl_seat->find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {

        wl_keyboard_send_modifiers(keyboard->resource(),
                                   wl_display_next_serial(root->display()),
                                   depressed,
                                   latched,
                                   locked,
                                   group);
      }
    }
  }

  void
  compositor_t::_keyboard::set_focus(shared_t<resource_t<surface_t>> surf) {
    if (auto surface = focus.lock(); surface) {
      // Send leave event
      auto      &wl_seat = root->wl_seat;
      wl_client *client  = surface->owner();
      if (auto seat = wl_seat->find(client); seat) {
        if (auto keyboard = seat->keyboard.lock(); keyboard) {
          wl_keyboard_send_leave(
            keyboard->resource(), wl_display_next_serial(root->display()), surface->resource());
        }
      }
    }

    focus = surf;
    if (surf) // handle nullptr (no focus)
      send_enter(surf);
  }

  void
  compositor_t::_window::activate(const shared_t<surface_t> &surface) {
    // We can't activate a surface that has no role.
    if (!surface->has_role()) {
      TRACE("Tried to activate window without a role!");
      return;
    }

    if (surface->role->type_id() == xdg_surface_t::id()) {
      auto xdg_surface = shared_cast<xdg_surface_t>(surface->role);
      root->xdg_shell->activate(xdg_surface);
      activated = surface;
      return;
    }
    assert(false && "Unhandled surface role in compositor_t::window_#activate");
  }

  void
  compositor_t::_window::deactivate(const shared_t<surface_t> &surface) {
    // We can't deactivate a surface that has no role.
    if (!surface->has_role()) {
      TRACE("Trying to deactivate surface that has no role!");
      return;
    }

    if (surface->role->type_id() == xdg_surface_t::id()) {
      auto xdg_surface = shared_cast<xdg_surface_t>(surface->role);
      root->xdg_shell->deactivate(xdg_surface);
      activated = nullptr;
      return;
    }
    assert(false && "Unhandled surface role in compositor_t::window_#activate");
  }

} // namespace barock
