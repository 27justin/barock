#include "../log.hpp"

#include "barock/compositor.hpp"
#include "barock/core/output_manager.hpp"
#include "barock/render/opengl.hpp"
#include "barock/singleton.hpp"

#include <jsl/optional.hpp>
#include <jsl/result.hpp>

#include <algorithm>
#include <xf86drmMode.h>

using namespace barock;

output_manager_t::output_manager_t(minidrm::drm::handle_t handle)
  : handle_(handle)
  , crtc_planner_(handle) {

  // Iterate once, and populate our CRTC planner with usable
  // connectors.
  for (auto const &connector : handle_.connectors()) {
    // We do not care for unused connectors (TODO: though we should,
    // atleast keep track of them.)
    if (connector.connection() == DRM_MODE_DISCONNECTED)
      continue;

    crtc_planner_.adopt(connector);
    outputs_.emplace_back(new output_t{ connector, connector.modes()[0] });
  }
}

void
output_manager_t::mode_set() {
  // Iterate out populated `outputs_` list, and mode set according to
  // their mode. Outputs should've been configured by the user
  // previously (requires the config to have ran)
  TRACE("Performing mode-set on {} outputs", outputs_.size());
  for (auto &output : outputs_) {
    auto mode = output->mode_;
    TRACE("Initializing {} with {}x{} @ {}",
          output->connector().type(),
          mode.width(),
          mode.height(),
          mode.refresh_rate());
    output->renderer(gl_renderer_t{ mode, crtc_planner_.mode_set(output->connector_, mode) });
  }
  events.on_mode_set.emit();
}

const std::vector<shared_t<output_t>> &
output_manager_t::outputs() const {
  return outputs_;
}

std::vector<shared_t<output_t>> &
output_manager_t::outputs() {
  return outputs_;
}

void
output_manager_t::configure(output_t &output, minidrm::drm::mode_t mode) {
  output.mode_ = mode;
}

jsl::optional_t<output_t &>
output_manager_t::by_name(const std::string &connector_name) {
  jsl::optional_t<output_t &> value = jsl::nullopt;

  auto it = std::find_if(outputs_.begin(), outputs_.end(), [&connector_name](auto &output) {
    return output->connector().type() == connector_name;
  });

  if (it != outputs_.end())
    value.emplace(**it);

  return value;
}
