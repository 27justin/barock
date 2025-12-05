#include "barock/hotkey.hpp"
#include "barock/compositor.hpp"
#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"
#include "barock/util.hpp"
#include "log.hpp"
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#include <janet.h>
#include <sstream>

using namespace barock;

action_t
parse_hotkey_string(std::string_view sequence_str, std::function<void()> &&callback) {
  action_t action;
  // Use an array of std::string_view for efficiency in comparisons
  const std::array<const char *, 8> MODIFIER_VMOD_NAMES = {
    XKB_VMOD_NAME_ALT,  XKB_VMOD_NAME_HYPER, XKB_VMOD_NAME_LEVEL3, XKB_VMOD_NAME_LEVEL5,
    XKB_VMOD_NAME_META, XKB_VMOD_NAME_NUM,   XKB_VMOD_NAME_SCROLL, XKB_VMOD_NAME_SUPER,
  };

  action.action = std::move(callback);

  // 1. Tokenize the input string by '+'
  std::stringstream ss{ std::string(sequence_str) };
  std::string       token;

  // 2. Process each token
  while (std::getline(ss, token, '+')) {
    // Trim leading and trailing whitespace from the token
    size_t first = token.find_first_not_of(" \t");
    if (std::string::npos == first)
      continue; // Skip empty tokens
    size_t           last = token.find_last_not_of(" \t");
    std::string_view key_name =
      sequence_str.substr(sequence_str.find(token) + first, (last - first) + 1);

    if (key_name.empty())
      continue;

    // 3. Check if the token is a defined virtual modifier
    bool is_modifier = false;
    for (const auto &mod_vmod_name : MODIFIER_VMOD_NAMES) {
      if (key_name == mod_vmod_name) {
        // If it's a virtual modifier (e.g., "Super", "Alt"), add it to action.modifiers
        action.modifiers.push_back(mod_vmod_name);
        is_modifier = true;
        break;
      }
    }

    if (!is_modifier) {
      // 4. If it's not a virtual modifier, attempt to convert it to a keysym
      // NOTE: We need to convert the key_name to C-string for the XKB function
      xkb_keysym_t keysym = xkb_keysym_from_name(key_name.data(), XKB_KEYSYM_NO_FLAGS);

      if (keysym != XKB_KEY_NoSymbol) {
        // Add the resulting keysym (e.g., XKB_KEY_L, XKB_KEY_Shift_L) to action.sequence
        action.sequence.push_back(keysym);
      } else {
        // Handle unknown key error here, e.g., logging or throwing an exception
        WARN("Warning: Unknown key name in hotkey sequence: '{}'", key_name.data());
      }
    }
  }
  return action;
}

static Janet
cfun_set_key(int32_t argc, Janet *argv) {
  // (set-key "Keybind" 'callback)
  janet_fixarity(argc, 2);

  auto hotkey_sequence = janet_getcstring(argv, 0);
  auto callback        = janet_getfunction(argv, 1);

  // Prevent freeing the callback
  janet_gcroot(janet_wrap_function(callback));
  auto action = parse_hotkey_string(hotkey_sequence, [callback] {
    // Resume at the callback
    auto  fiber = janet_fiber(callback, 0, 0, nullptr);
    Janet value;
    janet_continue(fiber, janet_wrap_nil(), &value);
  });

  singleton_t<janet_interop_t>::get().compositor->hotkey->add(action);
  return janet_wrap_true();
}

hotkey_t::hotkey_t(input_manager_t &input) {
  constexpr static JanetReg hotkey_fns[] = {
    { "set-key",
     cfun_set_key, "(set-key sequence action)\n\nRegister a global hotkey, running `action', when the sequence "
 "was hit."            },
    {   nullptr, nullptr,         nullptr }
  };

  auto &interop = singleton_t<janet_interop_t>::get();
  janet_cfuns(interop.env, "barock", hotkey_fns);

  state  = input.xkb.state;
  keymap = input.xkb.keymap;

  input.on_keyboard_input.connect([this, &input](auto key) {
    this->on_keyboard_input(key, input);
    return signal_action_t::eOk;
  });
}

bool
hotkey_t::feed(xkb_keysym_t symbol) {
  chord.emplace_back(key_action_t{ current_time_msec(), symbol });

  xkb_mod_mask_t latched = xkb_state_serialize_mods(state, XKB_STATE_MODS_EFFECTIVE);

  for (auto &action : actions) {
    if (chord.size() < action.sequence.size())
      continue;

    bool is_match = true;
    // First check for all modifiers
    for (auto const &modifier_name : action.modifiers) {
      is_match = is_match && ((latched & xkb_keymap_layout_get_index(keymap, modifier_name)) > 0);
      if (!is_match)
        goto next;
    }

    // TODO: We probably want some kind of timeout for keys, i.e. 2s
    // or longer.

    for (auto i = 0; i < action.sequence.size(); ++i) {
      // Our `chord` vector has the most recent key at the back, so we
      // have to check our range against the end of chord, therefore
      // this weird offset.
      is_match =
        is_match && (action.sequence[i] == chord[chord.size() - action.sequence.size() + i].key);
      if (!is_match)
        goto next;
    }

    if (is_match) {
      // "Consume" the keys
      chord.erase(chord.end() - action.sequence.size(), chord.end());

      // Run the handler
      action.action();
      return true;
    }
  next:
  }

  if (chord.size() > max_action_size) {
    chord.erase(chord.begin());
  }
  return false;
}

void
hotkey_t::add(const action_t &action) {
  actions.emplace_back(action);

  max_action_size = std::max(max_action_size, action.sequence.size());

  std::sort(actions.begin(), actions.end(), [](auto &left, auto &right) -> bool {
    return left.sequence.size() > right.sequence.size();
  });
}

void
hotkey_t::on_keyboard_input(keyboard_event_t key, input_manager_t &input) {
  uint32_t scancode  = libinput_event_keyboard_get_key(key.keyboard);
  uint32_t key_state = libinput_event_keyboard_get_key_state(key.keyboard);

  xkb_state_update_key(input.xkb.state,
                       scancode + 8, // +8: evdev -> xkb
                       key_state == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);

  xkb_keysym_t sym = xkb_state_key_get_one_sym(input.xkb.state, scancode + 8);
  if (key_state == LIBINPUT_KEY_STATE_PRESSED && feed(sym)) {
    return;
  }
}
