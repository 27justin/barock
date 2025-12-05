#include "barock/dmabuf/buffer.hpp"

#include "../log.hpp"

struct zwp_linux_buffer_params_v1_interface linux_buffer_params_impl = {
  nullptr,
  [](struct wl_client   *client,
     struct wl_resource *resource,
     int32_t             fd,
     uint32_t            plane_idx,
     uint32_t            offset,
     uint32_t            stride,
     uint32_t            modifier_hi,
     uint32_t modifier_lo) { WARN("zwp_linux_buffer_params_v1::add - not implemented!"); },
  nullptr,
  nullptr
};
