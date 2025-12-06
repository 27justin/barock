#pragma once

#include "barock/core/input.hpp"
#include <functional>
#include <vector>
#include <xkbcommon/xkbcommon.h>

#include <janet.h>

#define MOUSE_HOTKEY_MASK (1ul << 31)
enum { MOUSE_PRESSED = 1, MOUSE_RELEASED, MWHEEL_UP, MWHEEL_DOWN };

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

  struct service_registry_t;

  struct hotkey_t {
    std::vector<key_action_t> chord;
    std::vector<action_t>     actions;

    size_t              max_action_size = 0;
    xkb_keymap         *keymap;
    xkb_state          *state;
    service_registry_t &registry;

    hotkey_t(service_registry_t &);

    bool
    feed(xkb_keysym_t symbol);

    void
    add(const action_t &action);

    void
    on_keyboard_input(keyboard_event_t, input_manager_t &);
  };

}
