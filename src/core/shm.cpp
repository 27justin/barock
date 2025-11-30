#include "../log.hpp"
#include "wl/wayland-protocol.h"
#include <sys/mman.h>
#include <wayland-server-core.h>

#include "barock/compositor.hpp"
#include "barock/core/shm.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/resource.hpp"

using namespace barock;

void
wl_shm_create_pool(wl_client *client, wl_resource *resource, uint32_t id, int32_t fd, int32_t size);

void
wl_shm_release(wl_client *, wl_resource *wl_shm_pool);

struct wl_shm_interface wl_shm_impl = { .create_pool = wl_shm_create_pool,
                                        .release     = wl_shm_release };

namespace barock {
  shm_t::~shm_t() {}

  shm_t::shm_t(compositor_t &comp)
    : compositor(comp) {
    wl_global_create(comp.display(), &wl_shm_interface, VERSION, nullptr, bind);
  }

  void
  shm_t::bind(wl_client *client, void *, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &wl_shm_interface, version, id);

    wl_resource_set_implementation(resource, &wl_shm_impl, NULL, NULL);

    // TODO: Do we need more, or can we efficiently query what the GPU
    // can import without manually swizzling ourselves?
    // wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
    // wl_shm_send_format(resource, WL_SHM_FORMAT_RGBA8888);
    wl_shm_send_format(resource, WL_SHM_FORMAT_ARGB8888);
  }
}

void
wl_shm_create_pool(wl_client *client, wl_resource *wl_shm, uint32_t id, int32_t fd, int32_t size) {
  void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED) {
    wl_client_post_no_memory(client);
    return;
  }

  auto wl_shm_pool = make_resource<shm_pool_t>(client,
                                               wl_shm_pool_interface,
                                               wl_shm_pool_impl,
                                               wl_resource_get_version(wl_shm),
                                               id,
                                               fd,
                                               size,
                                               data);
}

void
wl_shm_release(wl_client *, wl_resource *wl_shm) {
  /*
    release the shm object

    Using this request a client can tell the server that it is not going to use the shm object
    anymore.

    Objects created via this interface remain unaffected.
  */
  // wl_resource_destroy(wl_shm);
}
