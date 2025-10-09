#include "barock/compositor.hpp"
#include "barock/input.hpp"
#include "barock/resource.hpp"

#include "barock/core/surface.hpp"
#include "barock/core/wl_seat.hpp"

#include "../log.hpp"

#include "wl/wayland-protocol.h"
#include <fcntl.h>
#include <libinput.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>
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

barock::wl_seat_t::wl_seat_t(barock::compositor_t &comp)
  : compositor(comp) {
  wl_seat_global = wl_global_create(comp.display(), &wl_seat_interface, VERSION, this, bind);

  // Initialize the keyboard XKB state & keymap
  comp.keyboard.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  comp.keyboard.xkb.keymap =
    xkb_keymap_new_from_names(comp.keyboard.xkb.context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
  comp.keyboard.xkb.keymap_string =
    xkb_keymap_get_as_string(comp.keyboard.xkb.keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
  comp.keyboard.xkb.state = xkb_state_new(comp.keyboard.xkb.keymap);
}

barock::wl_seat_t::~wl_seat_t() {
  free(compositor.keyboard.xkb.keymap_string);
}

void
barock::wl_seat_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
  wl_seat_t *seat = (wl_seat_t *)ud;

  auto *resource = wl_resource_create(client, &wl_seat_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  auto wl_seat       = make_resource<seat_t>(client, wl_seat_interface, wl_seat_impl, version, id);
  wl_seat->interface = seat;
  wl_seat->on_destroy.connect([orig = wl_seat->resource(), seat](auto resource) {
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
      return;
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
  });

  // Add a record to our map to identify clients -> seats
  seat->seats.insert(std::pair(client, wl_seat));

  uint32_t capabilities = 0;
  for (auto &dev : seat->compositor.input->devices) {
    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD))
      capabilities |= WL_SEAT_CAPABILITY_KEYBOARD;

    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER))
      capabilities |= WL_SEAT_CAPABILITY_POINTER;

    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH))
      capabilities |= WL_SEAT_CAPABILITY_TOUCH;
  }

  wl_seat_send_capabilities(resource, capabilities);
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

  auto wl_pointer = make_resource<wl_pointer_t>(client, wl_pointer_interface, wl_pointer_impl,
                                                wl_resource_get_version(wl_seat), id, seat);
  wl_pointer->on_destruct.connect([](auto &resource) { resource.seat->pointer = nullptr; });
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

  auto wl_keyboard = make_resource<wl_keyboard_t>(client, wl_keyboard_interface, wl_keyboard_impl,
                                                  wl_resource_get_version(wl_seat), id, seat);
  wl_keyboard->on_destruct.connect([](auto &resource) { resource.seat->pointer = nullptr; });
  seat->keyboard = wl_keyboard;

  auto &keymap_string = seat->interface->compositor.keyboard.xkb.keymap_string;
  int   keymap_fd     = create_xkb_keymap_fd(keymap_string, strlen(keymap_string));
  wl_keyboard_send_keymap(wl_keyboard->resource(), WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, keymap_fd,
                          strlen(keymap_string));
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

  if (wl_surface == nullptr) {
    pointer->seat->interface->compositor.cursor.surface = nullptr;
  } else {
    shared_t<resource_t<surface_t>> surface             = from_wl_resource<surface_t>(wl_surface);
    pointer->seat->interface->compositor.cursor.surface = surface;
    pointer->seat->interface->compositor.cursor.hotspot = { hotspot_x, hotspot_y };
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
