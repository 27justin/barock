#include "wl/linux-dmabuf-v1-protocol.h"

#include "barock/compositor.hpp"
#include "barock/dmabuf/buffer.hpp"
#include "barock/dmabuf/dmabuf.hpp"
#include "barock/dmabuf/feedback.hpp"

#include "../log.hpp"
#include <wayland-server-core.h>

struct zwp_linux_dmabuf_v1_interface dmabuf_impl{
  [](wl_client *, wl_resource *) { WARN("dmabuf#destroy - not impemented!"); },
  [](wl_client *client, wl_resource *dmabuf_protocol, uint32_t id) {
    INFO("dmabuf#create_params");

    wl_resource                     *buffer_params = wl_resource_create(
      client, &zwp_linux_buffer_params_v1_interface, wl_resource_get_version(dmabuf_protocol), id);
    if (!buffer_params) {
      wl_client_post_no_memory(client);
    }

    wl_resource_set_implementation(buffer_params, &linux_buffer_params_impl, nullptr, nullptr);
  },
  create_dmabuf_feedback_v1_resource,
  nullptr
};

namespace barock {
  dmabuf_t::dmabuf_t(compositor_t &comp)
    : compositor(comp) {
    wl_global_create(comp.display(), &zwp_linux_dmabuf_v1_interface, VERSION, nullptr, bind);
  }

  void
  dmabuf_t::bind(wl_client *client, void *, uint32_t version, uint32_t id) {
    INFO("DMABUF BIND");
    wl_resource *resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
    if (!resource) {
      wl_client_post_no_memory(client);
      return;
    }

    wl_resource_set_implementation(resource, &dmabuf_impl, nullptr, nullptr);
  }

}
