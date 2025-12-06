#include "../log.hpp"

#include "barock/compositor.hpp"
#include "barock/core/input.hpp"
#include "barock/hotkey.hpp"
#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"

namespace barock {
  JANET_MODULE(hotkey_t);
};

using namespace barock;

action_t
parse_hotkey_string(std::string_view sequence_str, std::function<void()> &&callback) {
  action_t                          action;
  const std::array<const char *, 8> MODIFIER_VMOD_NAMES = {
    XKB_VMOD_NAME_ALT,  XKB_VMOD_NAME_HYPER, XKB_VMOD_NAME_LEVEL3, XKB_VMOD_NAME_LEVEL5,
    XKB_VMOD_NAME_META, XKB_VMOD_NAME_NUM,   XKB_VMOD_NAME_SCROLL, XKB_VMOD_NAME_SUPER,
  };

  action.action = std::move(callback);

  // Tokenize the input string by '+'
  std::stringstream ss{ std::string(sequence_str) };
  std::string       token;

  // Process each token
  while (std::getline(ss, token, '+')) {
    // Trim leading and trailing whitespace from the token
    size_t first = token.find_first_not_of(" \t");
    // Skip empty tokens
    if (std::string::npos == first)
      continue;

    size_t           last = token.find_last_not_of(" \t");
    std::string_view key_name =
      sequence_str.substr(sequence_str.find(token) + first, (last - first) + 1);

    if (key_name.empty())
      continue;

    // Check if the token is a defined virtual modifier
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
      // If it's not a virtual modifier, attempt to convert it to a keysym
      // NOTE: We need to convert the key_name to C-string for the XKB function
      xkb_keysym_t keysym = xkb_keysym_from_name(key_name.data(), XKB_KEYSYM_NO_FLAGS);

      if (keysym != XKB_KEY_NoSymbol) {
        // Add the resulting keysym (e.g., XKB_KEY_L, XKB_KEY_Shift_L) to action.sequence
        action.sequence.push_back(keysym);
      } else {
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

  singleton_t<janet_interop_t>::get().compositor->registry_.hotkey->add(action);
  return janet_wrap_true();
}

void
janet_module_t<hotkey_t>::import(JanetTable *env) {
  constexpr static JanetReg hotkey_fns[] = {
    { "set-key",
     cfun_set_key, "(set-key sequence action)\n\nRegister a global hotkey, running `action', when the sequence "
 "was hit."            },
    {   nullptr, nullptr,         nullptr }
  };

  auto &compositor = singleton_t<compositor_t>::get();
  janet_cfuns(compositor.context_, "barock", hotkey_fns);
}
