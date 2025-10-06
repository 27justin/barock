#pragma once

#include "wl/wayland-protocol.h"
#include <wayland-server-core.h>

/*
  This interface offers ways to create generic dmabuf-based wl_buffers.

  For more information about dmabuf, see:
  https://www.kernel.org/doc/html/next/userspace-api/dma-buf-alloc-exchange.html

  Clients can use the get_surface_feedback request to get dmabuf feedback for a particular surface.
  If the client wants to retrieve feedback not tied to a surface, they can use the
  get_default_feedback request.

  The following are required from clients:

  Clients must ensure that either all data in the dma-buf is coherent for all subsequent read access
  or that coherency is correctly handled by the underlying kernel-side dma-buf implementation. Don't
  make any more attachments after sending the buffer to the compositor. Making more attachments
  later increases the risk of the compositor not being able to use (re-import) an existing
  dmabuf-based wl_buffer.

  The underlying graphics stack must ensure the following:

  The dmabuf file descriptors relayed to the server will stay valid for the whole lifetime of the
  wl_buffer. This means the server may at any time use those fds to import the dmabuf into any
  kernel sub-system that might accept it.

  However, when the underlying graphics stack fails to deliver the promise, because of e.g. a device
  hot-unplug which raises internal errors, after the wl_buffer has been successfully created the
  compositor must not raise protocol errors to the client when dmabuf import later fails.

  To create a wl_buffer from one or more dmabufs, a client creates a zwp_linux_dmabuf_params_v1
  object with a zwp_linux_dmabuf_v1.create_params request. All planes required by the intended
  format are added with the 'add' request. Finally, a 'create' or 'create_immed' request is issued,
  which has the following outcome depending on the import success.

  The 'create' request,

  on success, triggers a 'created' event which provides the final wl_buffer to the client.
  on failure, triggers a 'failed' event to convey that the server cannot use the dmabufs received
  from the client.

  For the 'create_immed' request,

  on success, the server immediately imports the added dmabufs to create a wl_buffer. No event is
  sent from the server in this case. on failure, the server can choose to either: terminate the
  client by raising a fatal error. mark the wl_buffer as failed, and send a 'failed' event to the
  client. If the client uses a failed wl_buffer as an argument to any request, the behaviour is
  compositor implementation-defined.

  For all DRM formats and unless specified in another protocol extension, pre-multiplied alpha is
  used for pixel values.

  Unless specified otherwise in another protocol extension, implicit synchronization is used. In
  other words, compositors and clients must wait and signal fences implicitly passed via the
  DMA-BUF's reservation mechanism.
*/

namespace barock {
  struct compositor_t;

  class dmabuf_t {
    public:
    static constexpr int VERSION = 5;
    dmabuf_t(compositor_t &);

    static void
    bind(wl_client *client, void *, uint32_t version, uint32_t id);

    private:
    compositor_t &compositor;
  };
}
