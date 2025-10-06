#pragma once

#include "wl/linux-dmabuf-v1-protocol.h"

extern struct zwp_linux_dmabuf_feedback_v1_interface linux_dmabuf_feedback_impl;

void
create_dmabuf_feedback_v1_resource(wl_client *client, wl_resource *dmabuf_protocol, uint32_t id);
