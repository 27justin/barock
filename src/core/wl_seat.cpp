#include "barock/core/wl_seat.hpp"
#include "../log.hpp"
#include "barock/compositor.hpp"
#include "barock/input.hpp"
#include "wl/wayland-protocol.h"
#include <libinput.h>
#include <wayland-server-core.h>

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
}

void
barock::wl_seat_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
  wl_seat_t *seat = (wl_seat_t *)ud;

  auto *resource = wl_resource_create(client, &wl_seat_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }

  auto client_seat     = new seat_t;
  client_seat->wl_seat = seat;
  wl_resource_set_implementation(resource, &wl_seat_impl, client_seat, [](wl_resource *r) {
    auto seat = static_cast<seat_t *>(wl_resource_get_user_data(r));
    seat->wl_seat->seats.erase(wl_resource_get_client(r));
    delete seat;
  });

  // Add a record to our map to identify clients -> seats
  seat->seats[client] = client_seat;

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

void
wl_seat_get_pointer(wl_client *client, wl_resource *seat_res, uint32_t id) {
  // This request only takes effect if the seat has the pointer
  // capability, or has had the pointer capability in the past. It is
  // a protocol violation to issue this request on a seat that has
  // never had the pointer capability. The missing_capability error
  // will be sent in this case.
  //
  // TODO: Implement that, currently we do not care.
  auto seat = (barock::seat_t *)wl_resource_get_user_data(seat_res);

  auto wl_pointer =
    wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(seat_res), id);
  if (!wl_pointer) {
    wl_client_post_no_memory(client);
    return;
  }
  seat->pointer = wl_pointer;
  wl_resource_set_implementation(wl_pointer, &wl_pointer_impl, seat, [](wl_resource *r) {
    static_cast<barock::seat_t *>(wl_resource_get_user_data(r))->pointer = nullptr;
  });
}

void
wl_seat_get_keyboard(wl_client *client, wl_resource *seat_res, uint32_t id) {

  // This request only takes effect if the seat has the pointer
  // capability, or has had the pointer capability in the past. It is
  // a protocol violation to issue this request on a seat that has
  // never had the pointer capability. The missing_capability error
  // will be sent in this case.
  //
  // TODO: Implement that, currently we do not care.
  INFO("wl_seat#get_keyboard");
  auto seat = (barock::seat_t *)wl_resource_get_user_data(seat_res);

  auto wl_keyboard =
    wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(seat_res), id);
  if (!wl_keyboard) {
    wl_client_post_no_memory(client);
    return;
  }
  seat->keyboard = wl_keyboard;
  wl_resource_set_implementation(wl_keyboard, &wl_keyboard_impl, seat, [](wl_resource *r) {
    static_cast<barock::seat_t *>(wl_resource_get_user_data(r))->keyboard = nullptr;
  });
}

void
wl_seat_release(wl_client *client, wl_resource *res) {
  wl_resource_destroy(res);
}

void
wl_pointer_set_cursor(struct wl_client   *client,
                      struct wl_resource *seat_res,
                      uint32_t            serial,
                      struct wl_resource *surface,
                      int32_t             hotspot_x,
                      int32_t             hotspot_y) {
  auto seat = static_cast<barock::seat_t *>(wl_resource_get_user_data(seat_res));
  seat->wl_seat->compositor.cursor.surface =
    (barock::surface_t *)wl_resource_get_user_data(surface);
  seat->wl_seat->compositor.cursor.hotspot = { hotspot_x, hotspot_y };
}

void
wl_pointer_release(wl_client *, wl_resource *res) {
  static_cast<barock::seat_t *>(wl_resource_get_user_data(res))->pointer = nullptr;
}

void
wl_keyboard_release(wl_client *, wl_resource *res) {
  static_cast<barock::seat_t *>(wl_resource_get_user_data(res))->keyboard = nullptr;
}
