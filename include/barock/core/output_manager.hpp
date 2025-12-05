#pragma once

#include "barock/core/output.hpp"
#include "barock/core/signal.hpp"
#include "barock/resource.hpp"
#include "jsl/optional.hpp"
#include "minidrm.hpp"
#include <cstdint>

namespace barock {

  class output_manager_t {
    private:
    std::vector<shared_t<output_t>> outputs_;
    minidrm::drm::handle_t          handle_;

    mode_set_allocator_t crtc_planner_;

    public:
    output_manager_t(minidrm::drm::handle_t);

    void
    mode_set();

    void
    configure(output_t &output, minidrm::drm::mode_t);

    const std::vector<shared_t<output_t>> &
    outputs() const;

    std::vector<shared_t<output_t>> &
    outputs();

    jsl::optional_t<output_t &>
    by_name(const std::string &connector_name);

    struct {
      signal_t<void>       on_mode_set;
      signal_t<output_t &> on_output_new;
    } events;
  };
}
