#pragma once

#include <functional>
#include <vector>
#include <xkbcommon/xkbcommon.h>

#define MOUSE_HOTKEY_MASK (1ul << 31)
enum { MOUSE_PRESSED = 1, MOUSE_RELEASED };

namespace barock {
  struct key_action_t {
    uint32_t     timestamp;
    xkb_keysym_t key;
  };

  struct action_t {
    std::vector<xkb_keysym_t> sequence;
    std::vector<const char *> modifiers;
    std::function<void()>     action;
  };

  struct hotkey_t {
    std::vector<key_action_t> chord;
    std::vector<action_t>     actions;

    size_t      max_action_size = 0;
    xkb_keymap *keymap;
    xkb_state  *state;

    bool
    feed(xkb_keysym_t symbol);
    void
    add(const action_t &action);
  };

}
