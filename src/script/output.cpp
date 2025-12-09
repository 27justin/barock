#include "../log.hpp"

#include "barock/compositor.hpp"
#include "barock/core/output_manager.hpp"
#include "barock/script/interop.hpp"
#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"

#include <jsl/optional.hpp>
#include <jsl/result.hpp>

#include <spawn.h>
#include <unistd.h>

using namespace barock;

namespace barock {
  JANET_MODULE(output_manager_t);

  template<>
  struct janet_converter_t<output_t> {
    Janet
    operator()(const output_t &output) {
      JanetTable *table = janet_table(4);

      janet_table_put(table, janet_ckeywordv("width"), janet_wrap_integer(output.mode().width()));
      janet_table_put(table, janet_ckeywordv("height"), janet_wrap_integer(output.mode().height()));

      janet_table_put(table,
                      janet_ckeywordv("size"),
                      janet_converter_t<ipoint_t>{}({ static_cast<int>(output.mode().width()),
                                                      static_cast<int>(output.mode().height()) }));

      janet_table_put(
        table, janet_ckeywordv("refresh-rate"), janet_wrap_number(output.mode().refresh_rate()));

      std::string connector_name = output.connector().name();
      janet_table_put(
        table, janet_ckeywordv("name"), janet_wrap_keyword(janet_cstring(connector_name.c_str())));

      auto pan = janet_tuple_begin(2);
      pan[0]   = janet_wrap_number(output.pan().x);
      pan[1]   = janet_wrap_number(output.pan().y);

      janet_table_put(table, janet_ckeywordv("pan"), janet_wrap_tuple(janet_tuple_end(pan)));

      return janet_wrap_table(table);
    }
  };
}

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

JANET_CFUN(cfun_output_configure) {
  janet_fixarity(argc, 2);

  auto connector  = janet_getkeyword(argv, 0);
  auto parameters = janet_gettable(argv, 1);

  auto &compositor = singleton_t<compositor_t>::get();
  auto  output     = compositor.registry_.output->by_name((const char *)connector);

  if (output.valid() == false) {
    ERROR("(output/configure :{}) Unknown output '{}'",
          (const char *)connector,
          (const char *)connector);
    return janet_wrap_false();
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
  auto &drm_connector = output->connector();
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

  compositor.registry_.output->configure(*output, *best_match);
  INFO("Configured '{}' to use mode {}x{} @ {} Hz",
       (const char *)connector,
       best_match->width(),
       best_match->height(),
       best_match->refresh_rate());

  // Test for screen arrangement
  std::map<const char *, direction_t> adjacent_map = {
    {    "top", direction_t::eNorth },
    {  "right",  direction_t::eEast },
    { "bottom", direction_t::eSouth },
    {   "left",  direction_t::eWest }
  };

  for (auto &[direction_keyword, direction] : adjacent_map) {

    Janet parameter_value = janet_table_rawget(parameters, janet_ckeywordv(direction_keyword));
    if (janet_type(parameter_value) == JANET_KEYWORD) {
      const char *output_name = (const char *)janet_unwrap_keyword(parameter_value);

      if (auto output_ptr = compositor.registry_.output->by_name(output_name)) {
        output->set_adjacent(direction, &*output_ptr);
      }
    }
  }
  return janet_wrap_true();
}

JANET_CFUN(cfun_output_get) {
  janet_fixarity(argc, 1);

  auto connector_name = janet_getkeyword(argv, 0);

  auto &compositor = singleton_t<compositor_t>::get();
  auto  output     = compositor.registry_.output->by_name((const char *)connector_name);
  if (output.valid() == false) {
    ERROR("(output/get) No connector named {} found!", (const char *)connector_name);
    return janet_wrap_nil();
  }

  auto table = janet_converter_t<output_t>{}(output.value());
  return table;
}

JANET_CFUN(cfun_output_pan) {
  janet_arity(argc, 2, 3); // :output-name [x y &opt skip-animation]

  auto connector_name = janet_getkeyword(argv, 0);

  auto &compositor = singleton_t<compositor_t>::get();
  auto  output     = compositor.registry_.output->by_name((const char *)connector_name);

  if (output.valid() == false) {
    WARN("Connector :{} not found during (output/pan)", (const char *)connector_name);
    return janet_wrap_nil();
  }

  auto pan = janet_gettuple(argv, 1);
  auto x   = janet_unwrap_number(pan[0]);
  auto y   = janet_unwrap_number(pan[1]);

  auto animation = janet_optboolean(argv, argc, 2, true);

  output.value().pan(fpoint_t{ static_cast<float>(x), static_cast<float>(y) }, animation == 0);

  return janet_wrap_true();
}

void
janet_module_t<output_manager_t>::import(JanetTable *env) {
  constexpr static JanetReg output_manager_fns[] = {
    { "output/configure",
     cfun_output_configure,          "(output/configure output parameters)\n\nConfigure `output' with parameters"       },
    {       "output/get",
     cfun_output_get,   "(output/get connector-name)\n\nReturn an object containing information about the output at "
   "connector `connector-name'.\nReturns nil, when the output couldn't be found."                  },
    {       "output/pan",
     cfun_output_pan, "(output/pan output [x y] &opt skip-animation)\n\nSet the workspace pan to [`x' `y']"             },
    {            nullptr, nullptr,                                                                               nullptr }
  };
  janet_cfuns(env, "barock", output_manager_fns);
}
