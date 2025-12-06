#include "barock/core/wl_output.hpp"
#include "barock/compositor.hpp"

#include "../log.hpp"
#include "barock/core/output_manager.hpp"

#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <xf86drmMode.h>

struct wl_output_interface wl_output_impl{ .release = nullptr };

barock::wl_output_t::wl_output_t(wl_display *display, service_registry_t &registry)
  : display(display)
  , registry(registry) {
  wl_output_global = wl_global_create(display, &wl_output_interface, VERSION, this, bind);
}

void
barock::wl_output_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
  wl_output_t *interface = (wl_output_t *)ud;
  wl_resource *output    = wl_resource_create(client, &wl_output_interface, version, id);
  if (!output) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(output, &wl_output_impl, ud, nullptr);

  // Send initial outputs
  for (auto &conn : interface->registry.output->outputs()) {
    // We skip disconnected connectors.
    wl_output_send_geometry(output,
                            0,
                            0,
                            0,
                            0,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            "Virtual",
                            "Monitor",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(output,
                        WL_OUTPUT_MODE_PREFERRED,
                        conn->mode().width(),
                        conn->mode().height(),
                        conn->mode().refresh_rate());
    wl_output_send_done(output);
  }
}
