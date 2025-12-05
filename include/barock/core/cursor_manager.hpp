#pragma once
#include "barock/core/input.hpp"
#include "barock/core/output_manager.hpp"
#include "barock/core/point.hpp"
#include "barock/resource.hpp"

#include "jsl/optional.hpp"

extern "C" {
#include <X11/Xcursor/Xcursor.h>
}

#include <limits>
#include <variant>

namespace barock {
  struct output_t;
  struct surface_t;
  struct input_manager_t;
  enum class direction_t;

  class cursor_manager_t {
    private:
    fpoint_t  position_; ///< Position of the cursor
    ipoint_t  hotspot_;  ///< Cursor hotspot in buffer local coordinates
    output_t *output_;   ///< The output the cursor is on

    output_manager_t &output_manager_;

    std::variant<shared_t<surface_t>, XcursorImage *> texture_;

    // Focus management
    weak_t<surface_t> focus_;

    jsl::optional_t<signal_token_t>
      paint_token_; ///< Token for the `on_repaint` handler on the current `output_`

    /**
     * @brief Transfer the cursor onto a different output. If there is
     * no connected output in the `direction`, this function does
     * nothing.  Returns true, when the cursor was moved onto a
     * different output, false if not.
     *
     * @return bool
     */
    bool
    transfer(const direction_t &);

    void
    paint(output_t &);

    public:
    static constexpr size_t CURSOR_PAINT_LAYER = std::numeric_limits<size_t>::max();

    cursor_manager_t(output_manager_t &, input_manager_t &);

    const fpoint_t &
    position();

    ///! Set the position of the cursor (in workspace coordinates.)
    const fpoint_t &
    position(const fpoint_t &value);

    void on_mouse_move(mouse_event_t);
    void on_mouse_click(mouse_button_t);
    void on_mouse_scroll(mouse_axis_t);

    /**
     * @brief Set the output the cursor is on. This function warps the
     * cursor, and does not modify the position.
     */
    void
    set_output(output_t *);

    /**
     * @brief Return the output the cursor is currently on
     */
    output_t &
    current_output();
  };
}
