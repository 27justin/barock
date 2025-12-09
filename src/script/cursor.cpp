#include "../log.hpp"

#include "barock/compositor.hpp"
#include "barock/core/cursor_manager.hpp"
#include "barock/core/input.hpp"
#include "barock/core/signal.hpp"
#include "barock/script/interop.hpp"
#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"

#include <libinput.h>
#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon.h>

using namespace barock;

namespace barock {
  JANET_MODULE(cursor_manager_t);
}

JANET_CFUN(cfun_mouse_set_position) {
  janet_fixarity(argc, 1);

  auto coords = janet_gettuple(argv, 0);

  float x = janet_unwrap_number(coords[0]);
  float y = janet_unwrap_number(coords[1]);

  auto &compositor = singleton_t<compositor_t>::get();
  compositor.registry_.cursor->set_cursor_position({ x, y });

  return janet_wrap_true();
}

JANET_CFUN(cfun_mouse_get_position) {
  janet_fixarity(argc, 0);

  auto &compositor = singleton_t<compositor_t>::get();
  auto  pos        = compositor.registry_.cursor->position();

  auto tuple = janet_tuple_begin(2);
  tuple[0]   = janet_wrap_number(pos.x);
  tuple[1]   = janet_wrap_number(pos.y);

  return janet_wrap_tuple(tuple);
}

signal_action_t
dispatch_mouse_move(mouse_event_t ev) {
  auto &compositor = singleton_t<compositor_t>::get();
  // TRACE("(janet module cursor_manager_t) Dispatching `mouse-move-hook'");

  Janet value;
  janet_resolve(compositor.context_, janet_csymbol("mouse-move-hook"), &value);

  if (janet_type(value) != JANET_ARRAY) {
    // TODO: Gracefully handle failure
    throw std::runtime_error{ "Expected type to be array" };
  }

  auto  tuple = janet_tuple_begin(2);
  float dx    = libinput_event_pointer_get_dx(ev.pointer);
  float dy    = libinput_event_pointer_get_dy(ev.pointer);

  dispatch_hook(compositor.context_, "mouse-move-hook", fpoint_t{ dx * 0.1f, dy * 0.1f });
  return signal_action_t::eOk;
}

signal_action_t
dispatch_mouse_click(mouse_button_t ev) {
  auto &compositor = singleton_t<compositor_t>::get();
  Janet value;
  janet_resolve(compositor.context_, janet_csymbol("mouse-button-hook"), &value);

  if (janet_type(value) != JANET_ARRAY) {
    // TODO: Gracefully handle failure
    throw std::runtime_error{ "Expected type to be array" };
  }

  Janet button;
  switch (ev.button) {
    case BTN_LEFT:
      button = janet_ckeywordv("left");
      break;
    case BTN_RIGHT:
      button = janet_ckeywordv("right");
      break;
    case BTN_MIDDLE:
      button = janet_ckeywordv("middle");
      break;
  }
  Janet state = janet_ckeywordv(ev.state == mouse_button_t::pressed ? "down" : "up");

  auto args     = janet_array(2);
  args->data[0] = button;
  args->data[1] = state;

  JanetArray *array = janet_unwrap_array(value);
  for (int32_t i = 0; i < array->count; ++i) {
    assert(janet_type(array->data[i]) == JANET_FUNCTION);

    JanetFunction *cb    = janet_unwrap_function(array->data[i]);
    JanetFiber    *fiber = janet_fiber(cb, 1, 2, args->data);
    Janet          value;
    janet_continue(fiber, janet_wrap_nil(), &value);
  }
  return signal_action_t::eOk;
}

std::map<xkb_keysym_t, xkb_key_direction> last_action;

/**
 * Janet C Function: (input/key-held key-string)
 * Checks if the key identified by 'key-string' is currently logically held down.
 * @param argc 1 (key-string)
 * @param argv Janet arguments
 * @return Janet boolean (true if held, false otherwise)
 */
JANET_CFUN(cfun_key_held) {
  janet_fixarity(argc, 1);

  auto &compositor = singleton_t<compositor_t>::get();
  auto &xkb        = compositor.registry_.input->xkb;

  const char  *key_string = janet_getcstring(argv, 0);
  xkb_keysym_t keysym     = xkb_keysym_from_name(key_string, XKB_KEYSYM_NO_FLAGS);

  if (keysym == XKB_KEY_NoSymbol) {
    // If the string couldn't be resolved (e.g., "NonexistentKey"), treat as not held.
    janet_panicf("Unknown key name: %s", key_string);
  }

  if (last_action.contains(keysym)) {
    if (last_action.at(keysym) == XKB_KEY_DOWN)
      return janet_wrap_true();
    else
      janet_wrap_false();
  }

  return janet_wrap_false();
}

JANET_CFUN(cfun_current_output) {
  janet_fixarity(argc, 0);
  auto &compositor = singleton_t<compositor_t>::get();
  return janet_converter_t<output_t>{}(compositor.registry_.cursor->current_output());
}

void
janet_module_t<cursor_manager_t>::import(JanetTable *env) {
  constexpr static JanetReg output_manager_fns[] = {
    { "input/mouse-set-position",
     cfun_mouse_set_position, "(input/mouse-set-position coords)\n\nSet the mouse position, `coords' has to be a tuple "
 "containing two numbers in workspace relative coordinates"                  },
    { "input/mouse-get-position",
     cfun_mouse_get_position,                                      "(input/mouse-get-position)\n\nGet the current mouse position, in workspace coordinates on "
                                      "the current output."                  },
    {           "input/key-held",
     cfun_key_held,         "(input/key-held key-string)\n\nReturns true, or false, whether or not the `key-string' is "
         "held.\n`key-string' must be a resolvable keysym."                            },
    {     "input/current-output",
     cfun_current_output,                                            "(input/current-output)\n\nReturn the output table the cursor is"
                                            "currently on."                      },
    {                    nullptr, nullptr,                                                         nullptr }
  };
  janet_cfuns(env, "barock", output_manager_fns);

  janet_def(env, "mouse-move-hook", janet_wrap_array(janet_array(0)), "Event list");
  janet_def(env, "mouse-button-hook", janet_wrap_array(janet_array(0)), "Event list");

  auto &compositor = singleton_t<compositor_t>::get();
  compositor.registry_.input->on_mouse_move.connect(&dispatch_mouse_move);
  compositor.registry_.input->on_mouse_click.connect(&dispatch_mouse_click);
  compositor.registry_.input->on_keyboard_input.connect([&](auto event) {
    uint32_t      scancode  = libinput_event_keyboard_get_key(event.keyboard);
    uint32_t      key_state = libinput_event_keyboard_get_key_state(event.keyboard);
    xkb_keycode_t xkb_key   = scancode + 8;
    auto          sym       = xkb_state_key_get_one_sym(
      singleton_t<compositor_t>::get().registry_.input->xkb.state, xkb_key);
    last_action[sym] = key_state == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP;
    return signal_action_t::eOk;
  });
}
