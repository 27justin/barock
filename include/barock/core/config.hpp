#pragma once

#include <filesystem>
#include <unordered_map>
#include <vector>

#include <jsl/optional.hpp>

namespace barock {

  struct config_output_t {
    std::string connector;

    /// Preferred mode set
    int                    width, height;
    jsl::optional_t<float> refresh_rate; ///< When nil, uses highest available
  };

  struct config_action_t {};

  struct config_t {
    std::unordered_map<std::string, config_output_t> outputs;
    std::vector<config_action_t>                     keybinds;

    static config_t
    load_from_file(const std::filesystem::path &);

    static config_t
    load_from_string(const std::string &);

    jsl::optional_t<const config_output_t &>
    output(const std::string &connector) const;
  };
}
