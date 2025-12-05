#include "barock/core/wl_data_device_manager.hpp"
#include "barock/compositor.hpp"

#include "../log.hpp"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

void
wl_data_device_manager_get_data_device(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id,
                                       struct wl_resource *seat);

struct wl_data_device_manager_interface wl_data_device_manager_impl{
  .create_data_source = nullptr,
  .get_data_device    = wl_data_device_manager_get_data_device,
};

struct wl_data_device_interface wl_data_device_impl{ .start_drag    = nullptr,
                                                     .set_selection = nullptr,
                                                     .release = [](wl_client *, wl_resource *) {
                                                     } };

barock::wl_data_device_manager_t::wl_data_device_manager_t(compositor_t &compositor)
  : compositor(compositor) {
  wl_data_device_manager_global =
    wl_global_create(compositor.display(), &wl_data_device_manager_interface, VERSION, this, bind);
}

void
barock::wl_data_device_manager_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {

  wl_resource *data_dev_manager =
    wl_resource_create(client, &wl_data_device_manager_interface, version, id);
  if (!data_dev_manager) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(data_dev_manager, &wl_data_device_manager_impl, ud, nullptr);
}

void
wl_data_device_manager_get_data_device(struct wl_client   *client,
                                       struct wl_resource *resource,
                                       uint32_t            id,
                                       struct wl_resource *seat) {

  auto data_device =
    wl_resource_create(client, &wl_data_device_interface, wl_resource_get_version(resource), id);
  if (!data_device) {
    wl_client_post_no_memory(client);
    return;
  }

  wl_resource_set_implementation(
    data_device, &wl_data_device_impl, wl_resource_get_user_data(resource), nullptr);
}
