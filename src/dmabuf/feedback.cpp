#include "barock/dmabuf/feedback.hpp"

#include "../log.hpp"
#include <drm_fourcc.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

struct zwp_linux_dmabuf_feedback_v1_interface linux_dmabuf_feedback_impl = { [](wl_client *,
                                                                                wl_resource *) {
  WARN("dmabuf_feedback#destroy - not implemented");
} };

void
create_dmabuf_feedback_v1_resource(wl_client *client, wl_resource *dmabuf_protocol, uint32_t id) {
  INFO("dmabuf_v1#get_default_feedback");

  /*
    This request creates a new wp_linux_dmabuf_feedback object not
    bound to a particular surface. This object will deliver
    feedback about dmabuf parameters to use if the client doesn't
    support per-surface feedback (see get_surface_feedback).
  */

  wl_resource *feedback = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                                             wl_resource_get_version(dmabuf_protocol), id);
  if (!feedback) {
    wl_client_post_no_memory(client);
  }

  wl_resource_set_implementation(feedback, &linux_dmabuf_feedback_impl, nullptr, nullptr);

  /*
    dmabuf feedback

    This object advertises dmabuf parameters feedback. This includes
    the preferred devices and the supported formats/modifiers.

    The parameters are sent once when this object is created and
    whenever they change. The done event is always sent once after all
    parameters have been sent. When a single parameter changes, all
    parameters are re-sent by the compositor.

    Compositors can re-send the parameters when the current client
    buffer allocations are sub-optimal. Compositors should not re-send
    the parameters if re-allocating the buffers would not result in a
    more optimal configuration. In particular, compositors should
    avoid sending the exact same parameters multiple times in a row.

    The tranche_target_device and tranche_formats events are grouped
    by tranches of preference. For each tranche, a
    tranche_target_device, one tranche_flags and one or more
    tranche_formats events are sent, followed by a tranche_done event
    finishing the list. The tranches are sent in descending order of
    preference. All formats and modifiers in the same tranche have the
    same preference.

    To send parameters, the compositor sends one main_device event,
    tranches (each consisting of one tranche_target_device event, one
    tranche_flags event, tranche_formats events and then a
    tranche_done event), then one done event.
  */

  std::vector<std::pair<uint32_t, uint64_t>> fmtmods = {
    { DRM_FORMAT_ARGB8888, DRM_FORMAT_MOD_LINEAR },
    { DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR },
    // … etc.
  };
  size_t n          = fmtmods.size();
  size_t table_size = n * 16;

  int fd = memfd_create("", 0);
  if (fd < 0) {
    ERROR("Failed to create memory file: {}", strerror(errno));
    throw std::runtime_error("failed to create memory fd");
  }

  ftruncate(fd, table_size);

  void *map = mmap(nullptr, table_size, PROT_WRITE, MAP_SHARED, fd, 0);
  for (size_t i = 0; i < n; i++) {
    uint32_t fmt = fmtmods[i].first;
    uint64_t mod = fmtmods[i].second;
    struct {
      uint32_t format;
      uint32_t padding;
      uint64_t modifier;
    } *ent        = ((decltype(ent))map) + i;
    ent->format   = fmt;
    ent->padding  = 0;
    ent->modifier = mod;
  }
  msync(map, table_size, MS_SYNC);
  munmap(map, table_size);

  wl_array device_arr;
  wl_array_init(&device_arr);
  dev_t dev = 0;                                      // your DRM device id
  void *p   = wl_array_add(&device_arr, sizeof(dev)); // careful with size
  memcpy(p, &dev, sizeof(dev));
  zwp_linux_dmabuf_feedback_v1_send_main_device(feedback, &device_arr);
  wl_array_release(&device_arr);

  zwp_linux_dmabuf_feedback_v1_send_format_table(feedback, fd, table_size);
  close(fd);

  wl_array target_dev_arr;
  wl_array_init(&target_dev_arr);
  p = wl_array_add(&target_dev_arr, sizeof(dev));
  memcpy(p, &dev, sizeof(dev));
  zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(feedback, &target_dev_arr);
  wl_array_release(&target_dev_arr);

  // flags
  uint32_t flags = 0; // or ZWP_LINUX_DMABUF_FEEDBACK_V1_TRANCHE_FLAGS_SCANOUT
  zwp_linux_dmabuf_feedback_v1_send_tranche_flags(feedback, flags);

  wl_array indices;
  wl_array_init(&indices);
  for (uint16_t i = 0; i < n; i++) {
    uint16_t *p = (uint16_t *)wl_array_add(&indices, sizeof(i));
    *p          = i;
  }
  zwp_linux_dmabuf_feedback_v1_send_tranche_formats(feedback, &indices);
  wl_array_release(&indices);

  zwp_linux_dmabuf_feedback_v1_send_tranche_done(feedback);
  zwp_linux_dmabuf_feedback_v1_send_done(feedback);
}
