#include "barock/core/output.hpp"
#include "barock/core/renderer.hpp"
#include "minidrm.hpp"

#include <stdexcept>
#include <xf86drmMode.h>

using namespace barock;

mode_set_allocator_t::mode_set_allocator_t(minidrm::drm::handle_t handle)
  : handle_(handle)
  , taken_(0) {}

void
mode_set_allocator_t::adopt(const minidrm::drm::connector_t &connector) {
  for (int i = 0; i < connector->count_encoders; ++i) {
    std::unique_ptr<drmModeEncoder, decltype(&drmModeFreeEncoder)> encoder(
      drmModeGetEncoder(handle_.fd, connector->encoders[i]), drmModeFreeEncoder);

    if (!encoder)
      continue;

    auto crtcs = handle_.crtcs();
    for (int i = 0; i < crtcs.size(); ++i) {
      // Skip the CRTC, if it's incompatible with our connector.
      uint32_t bit = 1 << i;
      if ((encoder->possible_crtcs & bit) == 0)
        continue;

      // Taken up CRTCs are also skipped
      if (taken_ & bit)
        continue;

      taken_ |= bit;
      plan_.emplace(connector.type(), i);
    }
  }
}

minidrm::framebuffer::egl_t
mode_set_allocator_t::mode_set(const minidrm::drm::connector_t &connector,
                               const minidrm::drm::mode_t      &mode) {
  if (!plan_.contains(connector.type()))
    throw std::runtime_error("Tried to `mode_set` a connector that wasn't adopted before!");

  auto crtcs = handle_.crtcs();
  auto crtc  = crtcs[plan_[connector.type()]];

  auto handle = minidrm::framebuffer::egl_t(handle_, connector, crtc, mode, 2);
  handle.mode_set();
  return std::move(handle);
}

output_t::output_t(const minidrm::drm::connector_t &connector, const minidrm::drm::mode_t &mode)
  : connector_(connector)
  , mode_(mode)
  , renderer_(nullptr)
  , top_(nullptr)
  , right_(nullptr)
  , bottom_(nullptr)
  , left_(nullptr) {}

output_t::~output_t() {}

const minidrm::drm::connector_t &
output_t::connector() const {
  return connector_;
}

const minidrm::drm::mode_t &
output_t::mode() const {
  return mode_;
}

renderer_t &
output_t::renderer() {
  if (!renderer_)
    throw std::runtime_error("No renderer associated on this display, is it connected?");
  return *renderer_;
}

template<>
fpoint_t
output_t::to<barock::coordinate_space_t::eWorkspace, barock::coordinate_space_t::eScreenspace>(
  const fpoint_t &from) const {
  fpoint_t xform;

  xform.x = from.x - x_;
  xform.y = from.y - y_;

  // TODO: Actually apply transformation matrix (rotation, additional scaling, etc.)
  return xform;
}

template<>
fpoint_t
output_t::to<barock::coordinate_space_t::eScreenspace, barock::coordinate_space_t::eWorkspace>(
  const fpoint_t &from) const {
  fpoint_t xform;

  xform.x = x_ + from.x;
  xform.y = y_ + from.y;

  // TODO: Actually apply transformation matrix (rotation, additional scaling, etc.)
  return xform;
}

jsl::optional_t<output_t &>
output_t::adjacent(direction_t direction) {
  output_t *result = this;

  // Iterate down, until our bitmask is set to 0, or we do not have a
  // valid reference anymore.
  while (result != nullptr && direction != direction_t::eNone) {
    if ((direction & direction_t::eNorth) != direction_t::eNone) {
      result    = result->top_;
      direction = (direction & ~direction_t::eNorth);
      continue;
    }

    if ((direction & direction_t::eEast) != direction_t::eNone) {
      result    = result->right_;
      direction = (direction & ~direction_t::eEast);
      continue;
    }

    if ((direction & direction_t::eSouth) != direction_t::eNone) {
      result    = result->bottom_;
      direction = (direction & ~direction_t::eSouth);
      continue;
    }

    if ((direction & direction_t::eWest) != direction_t::eNone) {
      result    = result->left_;
      direction = (direction & ~direction_t::eWest);
      continue;
    }
  }

  // If we don't have a valid reference, return null
  if (result == nullptr)
    return jsl::nullopt;

  // If we have a valid reference, but that reference is us, then
  // return null.
  if (result == this)
    return jsl::nullopt;

  return *result;
}
