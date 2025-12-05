#include "barock/core/config.hpp"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <libconfig.h++>
#include <sstream>
#include <string>

#include "../log.hpp"

// Source - https://stackoverflow.com/a
// Posted by Evan Teran, modified by community. See post 'Timeline' for change history
// Retrieved 2025-12-03, License - CC BY-SA 4.0

namespace barock {

  /// Parses mode lines, such as: `1920x1080@60`, ignores whitespace
  void
  parse_output_mode(const libconfig::Setting &setting, config_output_t &opt) {}

  config_output_t
  parse_output_group(const libconfig::Setting &setting) {
    config_output_t opt;

    opt.connector = setting.getName();

    // First parse the `mode` setting
    parse_output_mode(setting.lookup("mode"), opt);

    return opt;
  }

  config_t
  config_t::load_from_string(const std::string &source) {
    libconfig::Config config;
    config_t          cfg;
    config.readString(source);

    auto &outputs = config.lookup("output");
    assert(outputs.isGroup());

    for (auto const &output : outputs) {
      cfg.outputs.emplace(output.getName(), parse_output_group(output));
    }

    return cfg;
  }

  config_t
  config_t::load_from_file(const std::filesystem::path &path) {
    std::ifstream file(path);

    std::stringstream ss;
    ss << file.rdbuf();

    return load_from_string(ss.str());
  }

  jsl::optional_t<const config_output_t &>
  config_t::output(const std::string &connector) const {
    if (!outputs.contains(connector))
      return {};
    return outputs.at(connector);
  }

}
