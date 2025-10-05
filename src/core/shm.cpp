#include "../log.hpp"
#include "wl/wayland-protocol.h"
#include <sys/mman.h>
#include <wayland-server-core.h>

#include "barock/core/shm.hpp"
#include "barock/core/shm_pool.hpp"

static const struct wl_shm_interface shm_impl = { .create_pool = &barock::shm_t::handle_create_pool,
                                                  .release     = &barock::shm_t::handle_release };

namespace barock {
  shm_t::~shm_t() {}

  shm_t::shm_t(wl_display *display) {
    wl_global_create(display, &wl_shm_interface, VERSION, nullptr, bind);
  }

  void
  shm_t::bind(wl_client *client, void *, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(client, &wl_shm_interface, version, id);

    wl_resource_set_implementation(resource, &shm_impl, NULL, NULL);
    wl_shm_send_format(resource, WL_SHM_FORMAT_XRGB8888);
    wl_shm_send_format(resource, WL_SHM_FORMAT_RGBA8888);
  }

  // ----------------------------------
  //  WAYLAND PROTOCOL IMPLEMENTATION
  // ----------------------------------

  void
  shm_t::handle_create_pool(wl_client   *client,
                            wl_resource *resource,
                            uint32_t     id,
                            int32_t      fd,
                            int32_t      size) {
    ERROR("shm create pool");

    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
      wl_client_post_no_memory(client);
      return;
    }

    wl_resource *shm =
      wl_resource_create(client, &wl_shm_pool_interface, wl_resource_get_version(resource), id);

    shm_pool_t *pool    = new shm_pool_t(shm, fd, size, data);
    pool->marked_delete = false;
    wl_resource_set_implementation(shm, &wl_shm_pool_impl, pool, shm_pool_t::destroy);
    wl_resource_set_user_data(shm, pool);
  }

  void
  shm_t::handle_release(wl_client *, wl_resource *resource) {
    /*
      release the shm object

      Using this request a client can tell the server that it is not going to use the shm object
      anymore.

      Objects created via this interface remain unaffected.
     */
    ERROR("shm release");
    // TODO: Is this correct?
    wl_resource_destroy(resource);
  }

};
