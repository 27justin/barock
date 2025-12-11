#include "../log.hpp"

#include "barock/core/output.hpp"
#include "barock/core/renderer.hpp"
#include "barock/util.hpp"
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

    if (!encoder) {
      ERROR("Failed to retrieve DRM encoder information about connector {}", connector.name());
      continue;
    }

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
      plan_.emplace(connector.name(), i);
      // CRTC found; now we can directly exit
      return;
    }
  }
}

minidrm::framebuffer::egl_t
mode_set_allocator_t::mode_set(const minidrm::drm::connector_t &connector,
                               const minidrm::drm::mode_t      &mode) {
  if (!plan_.contains(connector.name()))
    throw std::runtime_error("Tried to `mode_set` a connector that wasn't adopted before!");

  auto crtcs = handle_.crtcs();
  auto crtc  = crtcs[plan_[connector.name()]];

  auto handle = minidrm::framebuffer::egl_t(handle_, connector, crtc, mode, 2);
  handle.mode_set();
  return std::move(handle);
}

float
easing(float x) {
  return x < 0.5 ? (1 - std::sqrt(1 - std::pow(2 * x, 2))) / 2
                 : (std::sqrt(1 - std::pow(-2 * x + 2, 2)) + 1) / 2;
}

output_t::output_t(const minidrm::drm::connector_t &connector, const minidrm::drm::mode_t &mode)
  : pan_({ 0.f, 0.f }, { 0.f, 0.f }, 1.f, easing)
  , damage_(ipoint_t{ 0, 0 }, ipoint_t{ (int)mode.width(), (int)mode.height() })
  , force_render_(true)
  , zoom_(1.f)
  , connector_(connector)
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

std::mutex &
output_t::dirty() const {
  return dirty_;
}

std::condition_variable &
output_t::dirty_cv() const {
  return dirty_cv_;
}

void
output_t::force_render() const {
  std::lock_guard<std::mutex> guard(dirty_);
  force_render_.store(true);
  dirty_cv_.notify_all();
}

void
output_t::damage(const region_t &region) const {
  {
    std::lock_guard<std::mutex> guard(dirty_);
    damage_.insert(node_t<int, void *>({ region.x, region.y }, nullptr));
    damage_.insert(node_t<int, void *>({ region.x + region.w, region.y + region.h }, nullptr));
  }
  dirty_cv_.notify_all();
}

bool
output_t::damaged(const ipoint_t &point) const {
  std::lock_guard<std::mutex> guard(dirty_);
  return force_render_.load() || damage_.query(point, point + ipoint_t{ 1, 1 }).size() > 0;
}

bool
output_t::damaged(const region_t &region) const {
  std::lock_guard<std::mutex> guard(dirty_);
  return force_render_.load() ||
         damage_.query({ region.x, region.y }, { region.x + region.w, region.y + region.h })
             .size() > 0;
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

  xform = from - pan_.sample();

  // TODO: Actually apply transformation matrix (rotation, additional scaling, etc.)
  return xform;
}

template<>
fpoint_t
output_t::to<barock::coordinate_space_t::eScreenspace, barock::coordinate_space_t::eWorkspace>(
  const fpoint_t &from) const {
  fpoint_t xform;

  xform = pan_.sample() + from;

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

void
output_t::set_adjacent(direction_t direction, output_t *output) {
  switch (direction) {
    case direction_t::eNorth:
      top_            = output;
      output->bottom_ = this;
      break;
    case direction_t::eEast:
      right_        = output;
      output->left_ = this;
      break;
    case direction_t::eSouth:
      bottom_      = output;
      output->top_ = this;
      break;
    case direction_t::eWest:
      left_          = output;
      output->right_ = this;
      break;
    default:
      CRITICAL("Tried to set adjacent output on {}, with invalid direction enum "
               "value. Do not use composed cardinal directions, use eEast, eNorth, etc.");
  }
}

bool
output_t::is_visible(const region_t &region) const {
  region_t bounds{
    pan_.sample(), fpoint_t{ mode_.width() / zoom_, mode_.height() / zoom_ }
  };
  return bounds.intersects(region);
}

fpoint_t
output_t::pan() const {
  return pan_.sample();
}

fpoint_t
output_t::pan(const fpoint_t &value, bool skip_animation) {
  if (!skip_animation) {
    pan_ = animation_t<fpoint_t>(pan_.sample(), value, 0.3f, easing);
  } else {
    pan_ = animation_t<fpoint_t>(pan_.sample(), value, 0.3f, easing);
    pan_.update(0.3f); // Skip to done
  }
  return pan_.sample();
}

void
output_t::paint() {
  uint32_t start = current_time_msec();
  renderer_->bind();
  renderer_->clear(0.08f, 0.08f, 0.15f, 1.f);
  for (auto &[_, signal] : events.on_repaint) {
    signal.emit(*this);
  }
  renderer_->commit();

  uint32_t end = current_time_msec();
  pan_.update((end - start) / 1000.f);

  damage_.clear();
}
