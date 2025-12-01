#include "barock/hotkey.hpp"
#include "barock/util.hpp"
#include "log.hpp"
#include <xkbcommon/xkbcommon.h>

using namespace barock;

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
