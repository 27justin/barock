#include "barock/compositor.hpp"
#include "barock/core/cursor_manager.hpp"
#include "barock/core/input.hpp"
#include "barock/core/signal.hpp"
#include "barock/resource.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_seat.hpp"

#include "../log.hpp"

#include "barock/util.hpp"
#include "wl/wayland-protocol.h"
#include <fcntl.h>
#include <libinput.h>
#include <print>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <xkbcommon/xkbcommon.h>

using namespace barock;

int
create_xkb_keymap_fd(const char *keymap_string, size_t length) {
  int fd = memfd_create("xkb_keymap", MFD_CLOEXEC);
  if (fd < 0)
    return -1;
  if (ftruncate(fd, length) < 0) {
    close(fd);
    return -1;
  }
  void *map = mmap(nullptr, length, PROT_WRITE, MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    close(fd);
    return -1;
  }
  memcpy(map, keymap_string, length);
  munmap(map, length);
  return fd;
}

void
wl_seat_get_pointer(wl_client *, wl_resource *, uint32_t id);
void
wl_seat_get_keyboard(wl_client *, wl_resource *, uint32_t id);
void
wl_seat_release(wl_client *, wl_resource *);

void
wl_pointer_set_cursor(struct wl_client   *client,
                      struct wl_resource *resource,
                      uint32_t            serial,
                      struct wl_resource *surface,
                      int32_t             hotspot_x,
                      int32_t             hotspot_y);

void
wl_pointer_release(wl_client *, wl_resource *);

void
wl_keyboard_release(wl_client *, wl_resource *);

struct wl_seat_interface wl_seat_impl{ .get_pointer  = wl_seat_get_pointer,
                                       .get_keyboard = wl_seat_get_keyboard,
                                       .get_touch    = nullptr,
                                       .release      = wl_seat_release };

struct wl_pointer_interface wl_pointer_impl{ .set_cursor = wl_pointer_set_cursor,
                                             .release    = wl_pointer_release };

struct wl_keyboard_interface wl_keyboard_impl{ .release = wl_keyboard_release };

wl_seat_t::wl_seat_t(wl_display *display, service_registry_t &registry)
  : display(display)
  , registry(registry) {

  wl_seat_global = wl_global_create(display, &wl_seat_interface, VERSION, this, bind);

  registry.input->on_keyboard_input.connect(
    std::bind(&wl_seat_t::on_keyboard_input, this, std::placeholders::_1));

  registry.input->on_mouse_click.connect(
    std::bind(&wl_seat_t::on_mouse_click, this, std::placeholders::_1));

  registry.input->on_mouse_move.connect(
    std::bind(&wl_seat_t::on_mouse_move, this, std::placeholders::_1));
}

wl_seat_t::~wl_seat_t() {}

void
wl_seat_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
  wl_seat_t *seat = (wl_seat_t *)ud;

  auto wl_seat       = make_resource<seat_t>(client, wl_seat_interface, wl_seat_impl, version, id);
  wl_seat->interface = seat;
  wl_seat->on_destroy.connect([orig = wl_seat->resource(), seat](auto resource) -> signal_action_t {
    // The wl_seat object has a more complicated destructor. Some
    // clients (>_> weston-terminal), seem to be more lenient in what
    // order they destroy their resources, sometimes first the
    // dependant resource are destroyed (pointer, keyboard, etc.),
    // while at other times, they first destroy the wl_seat object.
    //
    // Now, this handler automatically destroys the resource of any
    // stored pointer, keyboard, and touch devices, which should
    // prevent any leak from occuring.

    auto tmp = from_wl_resource<seat_t>(resource);
    if (!tmp) {
      WARN("Seat is invalid, can't clean up pointer, etc.");
      return signal_action_t::eOk;
    }

    auto pointer = tmp->pointer.lock();
    if (pointer)
      wl_resource_destroy(pointer->resource());

    auto keyboard = tmp->keyboard.lock();
    if (keyboard)
      wl_resource_destroy(keyboard->resource());

    // The cleanup of wl_seat is more involved, as this resource
    // creates new resources, which have to be
    if (orig == resource) {
      WARN("Removing client from seats map");
      seat->seats.erase(wl_resource_get_client(orig));
    }
    return signal_action_t::eOk;
  });

  // Add a record to our map to identify clients -> seats
  seat->seats.insert(std::pair(client, wl_seat));

  uint32_t capabilities = 0;
  for (auto &dev : seat->registry.input->devices()) {
    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD))
      capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;

    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER))
      capabilities |= WL_SEAT_CAPABILITY_POINTER;

    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH))
      capabilities |= WL_SEAT_CAPABILITY_TOUCH;
  }

  wl_seat_send_capabilities(wl_seat->resource(), capabilities);
}

shared_t<resource_t<seat_t>>
wl_seat_t::find(wl_client *client) {
  if (!seats.contains(client))
    return nullptr;
  return seats.at(client);
}

void
wl_seat_get_pointer(wl_client *client, wl_resource *wl_seat, uint32_t id) {
  // This request only takes effect if the seat has the pointer
  // capability, or has had the pointer capability in the past. It is
  // a protocol violation to issue this request on a seat that has
  // never had the pointer capability. The missing_capability error
  // will be sent in this case.
  //
  // TODO: Implement that, currently we do not care.
  auto seat = from_wl_resource<seat_t>(wl_seat);

  auto wl_pointer = make_resource<wl_pointer_t>(
    client, wl_pointer_interface, wl_pointer_impl, wl_resource_get_version(wl_seat), id, seat);
  wl_pointer->on_destruct.connect([](auto &resource) {
    resource.seat->pointer = nullptr;
    return signal_action_t::eOk;
  });
  seat->pointer = wl_pointer;
}

void
wl_seat_get_keyboard(wl_client *client, wl_resource *wl_seat, uint32_t id) {

  // This request only takes effect if the seat has the pointer
  // capability, or has had the pointer capability in the past. It is
  // a protocol violation to issue this request on a seat that has
  // never had the pointer capability. The missing_capability error
  // will be sent in this case.
  //
  // TODO: Implement that, currently we do not care.
  auto seat = from_wl_resource<seat_t>(wl_seat);

  auto wl_keyboard = make_resource<wl_keyboard_t>(
    client, wl_keyboard_interface, wl_keyboard_impl, wl_resource_get_version(wl_seat), id, seat);
  wl_keyboard->on_destruct.connect([](auto &resource) {
    resource.seat->pointer = nullptr;
    return signal_action_t::eOk;
  });
  seat->keyboard = wl_keyboard;

  auto &keymap_string = seat->interface->registry.input->xkb.keymap_string;
  int   keymap_fd     = create_xkb_keymap_fd(keymap_string, strlen(keymap_string));
  wl_keyboard_send_keymap(
    wl_keyboard->resource(), WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymap_fd, strlen(keymap_string));
  wl_keyboard_send_repeat_info(wl_keyboard->resource(), 70, 150);
}

void
wl_seat_release(wl_client *client, wl_resource *res) {
  wl_resource_destroy(res);
}

void
wl_pointer_set_cursor(struct wl_client   *client,
                      struct wl_resource *wl_pointer,
                      uint32_t            serial,
                      struct wl_resource *wl_surface,
                      int32_t             hotspot_x,
                      int32_t             hotspot_y) {
  auto pointer = from_wl_resource<wl_pointer_t>(wl_pointer);
  auto wl_seat = pointer->seat->interface;
  if (wl_surface == nullptr) {
    wl_seat->registry.cursor->xcursor(nullptr);
  } else {
    shared_t<resource_t<surface_t>> surface = from_wl_resource<surface_t>(wl_surface);
    wl_seat->registry.cursor->set_cursor(surface, ipoint_t{ hotspot_x, hotspot_y });
  }
}

void
wl_pointer_release(wl_client *, wl_resource *res) {
  wl_resource_destroy(res);
}

void
wl_keyboard_release(wl_client *, wl_resource *res) {
  wl_resource_destroy(res);
}

void
wl_seat_t::set_keyboard_focus(shared_t<resource_t<surface_t>> surface) {
  if (auto old_surface = focus_.keyboard.lock(); old_surface) {
    // Send leave
    wl_client *client = old_surface->owner();

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_keyboard` attached to that `wl_seat`.
    if (auto seat = find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {
        wl_array keys;
        wl_keyboard_send_leave(
          keyboard->resource(), wl_display_next_serial(display), old_surface->resource());
      }
    }
  }

  if (surface) {
    wl_client *client = surface->owner();

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_keyboard` attached to that `wl_seat`.
    if (auto seat = find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {
        wl_array keys;
        wl_array_init(&keys);
        wl_keyboard_send_enter(
          keyboard->resource(), wl_display_next_serial(display), surface->resource(), &keys);
        wl_array_release(&keys);
      }
    }
  }

  focus_.keyboard = surface;
}

fpoint_t
get_workspace_position(surface_t &surface) {
  auto &root     = surface.root();
  auto  position = surface.position();

  // Add XDG surface position & offset
  if (root.has_role()) {
    if (root.role->type_id() == xdg_surface_t::id()) {
      position += shared_cast<xdg_surface_t>(root.role)->position;
    }
  }

  return position.to<float>();
}

ipoint_t
get_surface_offset(surface_t &surface) {
  auto    &root = surface.root();
  ipoint_t offset{ 0, 0 };

  // Add XDG surface offset & offset
  if (root.has_role()) {
    if (root.role->type_id() == xdg_surface_t::id()) {
      offset = shared_cast<xdg_surface_t>(root.role)->offset.to<int>();
    }
  }

  return offset;
}

ipoint_t
get_surface_dimensions(surface_t &surface) {
  if (surface.has_role() && surface.role->type_id() == xdg_surface_t::id()) {
    auto xdg = shared_cast<xdg_surface_t>(surface.role);
    return xdg->size.to<int>();
  } else {
    return surface.extent();
  }
}

void
wl_seat_t::set_mouse_focus(shared_t<resource_t<surface_t>> surface) {
  if (auto old_surface = focus_.pointer.lock(); old_surface) {
    // Send leave
    wl_client *client = old_surface->owner();

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_pointer` attached to that `wl_seat`.
    if (auto seat = find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_array keys;
        wl_pointer_send_leave(
          pointer->resource(), wl_display_next_serial(display), old_surface->resource());
      }
    }
  }

  if (surface) {
    wl_client *client = surface->owner();

    // Figure out whether the client has
    // A.) A `wl_seat` configured.
    // B.) A `wl_pointer` attached to that `wl_seat`.
    if (auto seat = find(client); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_client *client = surface->owner();

        // Figure out whether the client has
        // A.) A `wl_seat` configured.
        // B.) A `wl_pointer` attached to that `wl_seat`.
        if (auto seat = find(client); seat) {
          if (auto pointer = seat->pointer.lock(); pointer) {
            fpoint_t position = get_workspace_position(*surface);
            position          = registry.cursor->position() - position;

            wl_pointer_send_enter(pointer->resource(),
                                  wl_display_next_serial(display),
                                  surface->resource(),
                                  wl_fixed_from_double(position.x),
                                  wl_fixed_from_double(position.y));
          }
        }
      }
    }
  }

  focus_.pointer = surface;
}

signal_action_t
wl_seat_t::on_keyboard_input(keyboard_event_t event) {
  auto &input = *registry.input;

  xkb_mod_mask_t depressed = xkb_state_serialize_mods(input.xkb.state, XKB_STATE_MODS_DEPRESSED);
  xkb_mod_mask_t latched   = xkb_state_serialize_mods(input.xkb.state, XKB_STATE_MODS_LATCHED);
  xkb_mod_mask_t locked    = xkb_state_serialize_mods(input.xkb.state, XKB_STATE_MODS_LOCKED);
  xkb_layout_index_t group =
    xkb_state_serialize_layout(input.xkb.state, XKB_STATE_LAYOUT_EFFECTIVE);

  uint32_t scan_code = libinput_event_keyboard_get_key(event.keyboard);
  uint32_t key_state = libinput_event_keyboard_get_key_state(event.keyboard);

  xkb_keysym_t sym = xkb_state_key_get_one_sym(input.xkb.state, scan_code + 8);

  if (auto surface = focus_.keyboard.lock(); surface) {
    wl_client *client = surface->owner();
    if (auto seat = find(client); seat) {
      if (auto keyboard = seat->keyboard.lock(); keyboard) {
        wl_keyboard_send_modifiers(
          keyboard->resource(), wl_display_next_serial(display), depressed, latched, locked, group);

        wl_keyboard_send_key(keyboard->resource(),
                             wl_display_next_serial(display),
                             current_time_msec(),
                             scan_code,
                             key_state);
      }
    }
  }
  return signal_action_t::eOk;
}

shared_t<resource_t<surface_t>>
wl_seat_t::find_best_surface(fpoint_t point) const {
  // Find the best surface for the position at `cursor'.

  // First get the window list on the active output.
  auto xdg_window = registry.xdg_shell->by_position(registry.cursor->current_output(), point);
  if (!xdg_window)
    return nullptr;

  shared_t<resource_t<surface_t>> result{ nullptr };

  if (auto wl_surface = xdg_window->surface.lock(); wl_surface) {
    fpoint_t workspace_position = get_workspace_position(*wl_surface);
    ipoint_t dimensions         = get_surface_dimensions(*wl_surface);
    ipoint_t offset             = get_surface_offset(*wl_surface);

    // `xdg_window' is the window directly beneath `point', now we
    // localize our `point' to be relative to the origin of
    // `xdg_window'.
    point = (point - workspace_position) + offset;

    // With this local offset, we can now query the actual wl_surface
    // of `xdg_window' for a subsurface
    auto subsurface = wl_surface->lookup(point.to<int>());
    if (subsurface) {
      return shared_cast<resource_t<surface_t>>(subsurface);
    } else {
      // nullptr here means that there is no subsurface more suitable
      // for `point', but since we know that `point' is already within
      // the `xdg_window', we can directly return `wl_surface' as the
      // best surface.
      return wl_surface;
    }
  }
  return nullptr;
}

signal_action_t
wl_seat_t::on_mouse_click(mouse_button_t event) {
  auto &input  = *registry.input;
  auto &cursor = *registry.cursor;

  // Clicking our mouse sends the event to whatever is stored in
  // `focus_.pointer', this field is updated on each mouse move event,
  // therefore not guaranteed, but highly likely to be on the correct
  // surface.
  if (auto surface = focus_.pointer.lock(); surface) {
    set_keyboard_focus(surface);

    // Also activate this xdg_window (?) via the xdg_shell, and raise
    // it to the top.
    if (surface->has_role() && surface->role->type_id() == xdg_surface_t::id()) {
      auto xdg = shared_cast<xdg_surface_t>(surface->role);
      registry.xdg_shell->raise_to_top(xdg);
      registry.xdg_shell->activate(xdg);
    }

    // Find the attached wl_seat
    if (auto seat = find(surface->owner()); seat) {
      if (auto pointer = seat->pointer.lock(); pointer) {
        wl_pointer_send_button(pointer->resource(),
                               wl_display_next_serial(display),
                               current_time_msec(),
                               event.button,
                               event.state);
      }
    }
  }

  return signal_action_t::eOk;
}

signal_action_t
wl_seat_t::on_mouse_move(mouse_event_t event) {
  auto &input          = *registry.input;
  auto &cursor         = *registry.cursor;
  auto &current_output = registry.cursor->current_output();

// We don't have any window active, query one.
start:
  if (!focus_.pointer.lock()) {
    set_mouse_focus(find_best_surface(registry.cursor->position()));
  }

  if (auto surface = focus_.pointer.lock(); surface) {
    auto &root = surface->root();

    // Determine the workspace-local position & size of `root' (the window)
    fpoint_t workspace_position = get_workspace_position(root);
    ipoint_t dimensions         = get_surface_dimensions(root);
    ipoint_t offset             = get_surface_offset(root);

    // Make our mouse position relative to `root'
    fpoint_t mouse_local_position = registry.cursor->position() - workspace_position;

    // Then check if the local coordinates are past the window
    // dimensions
    if (mouse_local_position.x < 0 || mouse_local_position.x > dimensions.x ||
        mouse_local_position.y < 0 || mouse_local_position.y > dimensions.y) {
      // If they are, reset focus (`set_mouse_focus' sends the wl_pointer#leave event)
      set_mouse_focus(nullptr);
      // And jump back to `start', where we try to find a window under the cursor.
      goto start;
    } else {
      // Our mouse is still within the window, now we can add our
      // offset.
      //
      // Some clients, e.g. foot, declare their XDG surface with
      // negative offsets, to encompass their client-side decoration,
      // since we also adjust for those offsets in rendering, we
      // naturally also have to consider them here, otherwise our view
      // & data would be out of sync.
      mouse_local_position += offset;

      // Now try to find a better subsurface within `root' for the
      // given mouse position.
      auto candidate = root.lookup(mouse_local_position.to<int>());
      // The returned candidate may either be nullptr (when the point
      // intersects no subsurface -> point is on the `root' surface).
      //
      // ... Or it may be some other surface, that might also be the
      // `surface' itself.
      if (candidate && surface != candidate) {
        // Found a better suited candidate, send wl_pointer#enter to that.
        set_mouse_focus(shared_cast<resource_t<surface_t>>(candidate));
        surface = candidate;
      }

      // At this point, `mouse_local_position' is still relative to
      // `root', for our purpose of now informing the client where the
      // mouse is, though, we have to make the origin of
      // `mouse_local_position' the actual subsurface.
      mouse_local_position = mouse_local_position - surface->position();

      if (auto seat = find(surface->owner()); seat) {
        if (auto pointer = seat->pointer.lock(); pointer) {
          wl_pointer_send_motion(pointer->resource(),
                                 current_time_msec(),
                                 wl_fixed_from_double(mouse_local_position.x),
                                 wl_fixed_from_double(mouse_local_position.y));
          wl_pointer_send_frame(pointer->resource());
        }
      }
    }
  }
  return signal_action_t::eOk;
}
