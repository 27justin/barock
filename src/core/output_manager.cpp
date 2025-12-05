#include "../log.hpp"

#include "barock/compositor.hpp"
#include "barock/core/output_manager.hpp"
#include "barock/render/opengl.hpp"
#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"
#include "janet.h"

#include <jsl/optional.hpp>
#include <jsl/result.hpp>

#include <xf86drmMode.h>

using namespace barock;

struct mode_setting_t {
  int32_t                width, height;
  jsl::optional_t<float> refresh_rate;
};

// Trim from the start (in place)
inline void
ltrim(std::string &s) {
  s.erase(s.begin(),
          std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

// Trim from the end (in place)
inline void
rtrim(std::string &s) {
  s.erase(
    std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
    s.end());
}

inline void
trim(std::string &s) {
  ltrim(s);
  rtrim(s);
}

static jsl::result_t<mode_setting_t, std::string /* err */>
parse_mode_line(const std::string &line) {
  constexpr const char *REFRESH_RATE_DELIMITER = "@";
  constexpr const char *DIMENSION_DELIMITER    = "x";

  mode_setting_t setting;

  // First split on REFRESH_RATE_DELIMITER
  std::string dimensions = line.substr(0, line.find(REFRESH_RATE_DELIMITER));

  std::string w = dimensions.substr(0, dimensions.find(DIMENSION_DELIMITER));
  if (w.size() < 1 || w.size() == dimensions.size())
    return jsl::result_t<mode_setting_t, std::string>::err(
      "Expected syntax `<width>x<height>(@<refresh rate>)?`");
  ;

  std::string h = dimensions.substr(w.size() + 1);
  if (h.size() < 1)
    return jsl::result_t<mode_setting_t, std::string>::err(
      "Expected syntax `<width>x<height>(@<refresh rate>)?`");

  trim(w);
  trim(h);

  setting.width  = std::stol(w);
  setting.height = std::stol(h);

  // Refresh rate is optional
  if (dimensions.size() == line.size()) {
    return jsl::result_t<mode_setting_t, std::string>::ok(setting);
  }

  std::string refresh_rate = line.substr(dimensions.size() + 1);
  trim(refresh_rate);
  setting.refresh_rate = std::stof(refresh_rate);

  return jsl::result_t<mode_setting_t, std::string>::ok(setting);
}

static Janet
cfun_configure_output(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  auto connector  = janet_getkeyword(argv, 0);
  auto parameters = janet_gettable(argv, 1);

  auto &interop = singleton_t<janet_interop_t>::get();
  // Find the connector
  auto &outputs = interop.compositor->output->outputs();
  auto  it      = std::find_if(outputs.begin(), outputs.end(), [connector](auto &output) {
    return output->connector().type() == std::string_view((const char *)connector);
  });

  if (it == outputs.end()) {
    WARN("Tried to configure output '{}', which is not connected.", (const char *)connector);
    return janet_wrap_nil();
  }

  // Else, we can parse the parameters table
  janet_table_get(parameters, janet_ckeywordv("mode"));

  auto mode_opt       = janet_table_get(parameters, janet_ckeywordv("mode"));
  auto preferred_mode = parse_mode_line(janet_getcstring(&mode_opt, 0));

  if (!preferred_mode.valid()) {
    ERROR("{}", preferred_mode.error());
    return janet_wrap_false();
  }

  // List the modes we have, and try to match either the exact one, or
  // the preferred.
  auto &drm_connector = (*it)->connector();
  auto  modes         = drm_connector.modes();
  auto  best_match    = modes.end();

  for (auto it = modes.begin(); it != modes.end(); ++it) {
    auto &mode = *it;
    if (best_match == modes.end() && mode.preferred) {
      best_match = it;
    }

    if (mode.width() == preferred_mode.value().width &&
        mode.height() == preferred_mode.value().height &&
        mode.refresh_rate() == preferred_mode.value().refresh_rate.value_or(mode.refresh_rate())) {
      best_match = it;
    }
  }

  if (best_match == modes.end()) {
    ERROR("Could not match any mode based on the configuration for {}!", (const char *)connector);
    return janet_wrap_false();
  }

  interop.compositor->output->configure(**it, *best_match);
  INFO("Configured '{}' to use mode {}x{} @ {} Hz",
       (const char *)connector,
       best_match->width(),
       best_match->height(),
       best_match->refresh_rate());

  return janet_wrap_true();
}

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

  auto                     &interop              = singleton_t<janet_interop_t>::get();
  constexpr static JanetReg output_manager_fns[] = {
    { "configure-output",
     cfun_configure_output, "(configure-output output parameters)\n\nConfigure `output' with parameters"       },
    {            nullptr, nullptr,                                                                      nullptr }
  };

  janet_cfuns(interop.env, "barock", output_manager_fns);
}

void
output_manager_t::mode_set() {
  // Iterate out populated `outputs_` list, and mode set according to
  // their mode. Outputs should've been configured by the user
  // previously (requires the config to have ran)
  TRACE("Performing mode-set on {} outputs", outputs_.size());
  for (auto &output : outputs_) {
    output->renderer(
      gl_renderer_t{ output->mode_, crtc_planner_.mode_set(output->connector_, output->mode_) });
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
