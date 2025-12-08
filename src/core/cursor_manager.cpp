#include "barock/core/cursor_manager.hpp"
#include "barock/compositor.hpp"
#include "barock/core/input.hpp"
#include "barock/core/output.hpp"
#include "barock/core/renderer.hpp"
#include "barock/core/signal.hpp"
#include "jsl/optional.hpp"

#include "../log.hpp"
#include <X11/Xcursor/Xcursor.h>
#include <cassert>
#include <variant>

using namespace barock;

const std::vector<std::string> CURSOR_NAMES = {
  "alias",
  "all-resize",
  "all-scroll",
  "arrow",
  "bd_double_arrow",
  "bottom_left_corner",
  "bottom_right_corner",
  "bottom_side",
  "cell",
  "col-resize",
  "context-menu",
  "copy",
  "cross",
  "crosshair",
  "cross_reverse",
  "default",
  "diamond_cross",
  "dnd-ask",
  "dnd-move",
  "e-resize",
  "ew-resize",
  "fd_double_arrow",
  "fleur",
  "grab",
  "grabbing",
  "hand1",
  "hand2",
  "help",
  "left_ptr",
  "left_side",
  "move",
  "ne-resize",
  "nesw-resize",
  "no-drop",
  "not-allowed",
  "n-resize",
  "ns-resize",
  "nw-resize",
  "nwse-resize",
  "pointer",
  "progress",
  "question_arrow",
  "right_side",
  "row-resize",
  "sb_h_double_arrow",
  "sb_v_double_arrow",
  "se-resize",
  "s-resize",
  "sw-resize",
  "tcross",
  "text",
  "top_left_arrow",
  "top_left_corner",
  "top_right_corner",
  "top_side",
  "vertical-text",
  "wait",
  "watch",
  "w-resize",
  "X_cursor",
  "xterm",
  "zoom-in",
  "zoom-out",
};

cursor_manager_t::cursor_manager_t(service_registry_t &registry)
  : registry_(registry) {
  registry.input->on_mouse_move.connect(
    std::bind(&cursor_manager_t::on_mouse_move, this, std::placeholders::_1));

  // TODO: `XcursorLibraryLoadImage' may fail and return nullptr, it
  // may also fail loading the default cursor ("left_ptr", nullptr),
  // in which case we have no fallback (?) to use.
  texture_ = XcursorLibraryLoadImage("left_ptr", "Adwaita", 32);
  assert(std::get<XcursorImage *>(texture_) != nullptr);

  registry_.output->events.on_mode_set.connect([this] {
    set_output(registry_.output->outputs()[0].get());
    return signal_action_t::eDelete;
  });
}

cursor_manager_t::~cursor_manager_t() {
  if (std::holds_alternative<XcursorImage *>(texture_)) {
    XcursorImageDestroy(std::get<XcursorImage *>(texture_));
  }
}

signal_action_t
cursor_manager_t::paint(output_t &output) {
  fpoint_t screen = output.to<output_t::eWorkspace, output_t::eScreenspace>(position_);

  std::visit(
    [&]<typename T>(T &texture) {
      if constexpr (std::is_same_v<std::decay_t<decltype(texture)>, XcursorImage *>) {
        output.renderer().draw(texture,
                               output.to<output_t::eWorkspace, output_t::eScreenspace>(position_));
      } else {
        // shared_t<surface_t>
        output.renderer().draw(
          *texture, output.to<output_t::eWorkspace, output_t::eScreenspace>(position_) - hotspot_);
      }
    },
    texture_);

  return signal_action_t::eOk;
}

void
cursor_manager_t::set_output(output_t *output) {
  if (paint_token_)
    output_->events.on_repaint[CURSOR_PAINT_LAYER].disconnect(paint_token_.value());

  if (output != nullptr) {
    // Update our output variable
    output_      = output;
    paint_token_ = output_->events.on_repaint[CURSOR_PAINT_LAYER].connect(
      std::bind(&cursor_manager_t::paint, this, std::placeholders::_1));
  }
}

bool
cursor_manager_t::transfer(const direction_t &direction) {
  auto adjacent = output_->adjacent(direction);
  if (!adjacent) {
    return false;
  }

  auto old_output   = output_;
  auto old_position = position_;

  // Transferring the cursor between outputs also means warping the
  // cursor position, depending on the resolution of the new screen.
  fpoint_t scale_factor{
    static_cast<float>(adjacent->mode().width()) / static_cast<float>(old_output->mode().width()),
    static_cast<float>(adjacent->mode().height()) / static_cast<float>(old_output->mode().height())
  };

  fpoint_t scaled_position{ old_position.x * scale_factor.x, old_position.y * scale_factor.y };

  set_output(&adjacent.value());
  position_ = scaled_position;

  if (direction == direction_t::eNorth) {
    // Top edge of the new output
    position_.y = adjacent->mode().height() - 1;
  } else if (direction == direction_t::eEast) {
    // Left edge of the new output
    position_.x = 0.f;
  } else if (direction == direction_t::eSouth) {
    // Bottom edge of the new output
    position_.y = 0.f;
  } else if (direction == direction_t::eWest) {
    // Right edge of the new output
    position_.x = adjacent->mode().width() - 1;
  }

  return true;
}

output_t &
cursor_manager_t::current_output() {
  // Shouldn't ever happen, if we have no outputs, we don't even
  // initialize.
  assert(output_ != nullptr);
  return *output_;
}

void
cursor_manager_t::xcursor(const char *name) {
  if (std::holds_alternative<XcursorImage *>(texture_)) {
    // First free the old one.
    XcursorImageDestroy(std::get<XcursorImage *>(texture_));
  }

  if (name)
    texture_ = XcursorLibraryLoadImage(name, nullptr, 30);
  // on nullptr, we reset to left_ptr
  else
    texture_ = XcursorLibraryLoadImage("left_ptr", nullptr, 30);
}

shared_t<surface_t>
cursor_manager_t::cursor() const {
  if (!std::holds_alternative<shared_t<surface_t>>(texture_))
    return nullptr;
  return std::get<shared_t<surface_t>>(texture_);
}

void
cursor_manager_t::set_cursor(shared_t<surface_t> surface, ipoint_t hotspot) {
  texture_ = surface;
  hotspot_ = hotspot;
}

const fpoint_t &
cursor_manager_t::position() const {
  return position_;
}

const fpoint_t &
cursor_manager_t::position(const fpoint_t &value) {
  position_ = value;
  return position_;
}

signal_action_t
cursor_manager_t::on_mouse_move(mouse_event_t move) {
  enum libinput_event_type ty = libinput_event_get_type(move.event);
  // Our `on_mouse_move` signal triggers on two separate libinput events:
  // - LIBINPUT_EVENT_POINTER_MOTION
  // - LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE
  //
  // The latter (MOTION_ABSOLUTE), has its values based on the
  // absolute extent of the device (imagine a graphics tablet, or a
  // touch-screen).
  //
  // While the former reports just delta.
  //
  // Therefore to correctly handle these two cases, we query all
  // active monitors for their size, and from that compute a
  // relative cursor position.
  double dx = 0., dy = 0.;

  // Calculate the delta the mouse moved.
  if (ty == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
    static double last_x = 0., last_y = 0.;
    uint32_t      sx = 0, sy = 0;
    // for (auto &s : monitors) {
    //   sx += s->mode.width();
    //   sy = std::max(sy, s->mode.height());
    // }

    fpoint_t updated{};

    updated.x = libinput_event_pointer_get_absolute_x_transformed(move.pointer, sx);
    updated.y = libinput_event_pointer_get_absolute_y_transformed(move.pointer, sy);

    // Convert absolute screen coords to workspace coords
    position_ = output_->to<output_t::eScreenspace, output_t::eWorkspace>(updated);

    // Compute delta in workspace space
    double screen_dx = updated.x - last_x;
    double screen_dy = updated.y - last_y;

    dx = screen_dx;
    dy = screen_dy;

    last_x = updated.x;
    last_y = updated.y;
  } else {
    dx = libinput_event_pointer_get_dx(move.pointer);
    dy = libinput_event_pointer_get_dy(move.pointer);
    position_.x += dx * 0.1;
    position_.y += dy * 0.1;
  }

  direction_t transfer_direction = direction_t::eNone;
  if (position_.x > output_->mode().width())
    transfer_direction |= direction_t::eEast;

  if (position_.y > output_->mode().height())
    transfer_direction |= direction_t::eSouth;

  if (position_.x < 0)
    transfer_direction |= direction_t::eWest;

  if (position_.y < 0)
    transfer_direction |= direction_t::eNorth;

  if (transfer_direction != direction_t::eNone) {
    // Transfer onto that monitor, `transfer' takes care of cursor
    // warping.
    bool transferred = this->transfer(transfer_direction);

    if (!transferred) {
      // Output has no adjacent monitor in the direction, if this is
      // the case, we clamp the position to our viewport.
      region_t viewport = {
        output_->pan(),
        { static_cast<float>(output_->mode().width()),
                 static_cast<float>(output_->mode().height()) }
      };

      position_.x = std::clamp(
        position_.x, static_cast<float>(viewport.x), static_cast<float>(viewport.x + viewport.w));
      position_.y = std::clamp(
        position_.y, static_cast<float>(viewport.y), static_cast<float>(viewport.y + viewport.w));
    }
  }

  return signal_action_t::eOk;
}

signal_action_t
cursor_manager_t::on_mouse_click(mouse_button_t event) {
  return signal_action_t::eOk;
}

signal_action_t
cursor_manager_t::on_mouse_scroll(mouse_axis_t event) {
  return signal_action_t::eOk;
}

void
cursor_manager_t::set_cursor_position(const fpoint_t &position) {
  position_ = position;
}
