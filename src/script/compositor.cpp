#include "../log.hpp"

#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

using namespace barock;

namespace barock {
  JANET_MODULE(compositor_t);
}

pid_t
run_command(std::string_view cmd) {
  pid_t             pid;
  posix_spawnattr_t attr;

  // Initialize spawn attributes
  if (posix_spawnattr_init(&attr) != 0) {
    perror("posix_spawnattr_init");
    return -1;
  }

  // Set the POSIX_SPAWN_SETSID flag to detach from the current session
  short flags = POSIX_SPAWN_SETSID;
  if (posix_spawnattr_setflags(&attr, flags) != 0) {
    perror("posix_spawnattr_setflags");
    posix_spawnattr_destroy(&attr);
    return -1;
  }

  const char *argv[] = { (char *)"sh", (char *)"-c", nullptr, nullptr };
  std::string cmdstr(cmd);
  argv[2]    = const_cast<char *>(cmdstr.c_str());
  int result = posix_spawnp(&pid, "sh", NULL, NULL, const_cast<char *const *>(argv), environ);

  posix_spawnattr_destroy(&attr);

  if (result != 0) {
    perror("posix_spawnp");
    return -1;
  }

  return pid;
}

JANET_CFUN(cfun_run_command) {
  janet_fixarity(argc, 1);

  std::string command = (const char *)janet_getstring(argv, 0);
  pid_t       pid     = run_command(command);
  return janet_wrap_integer((int)pid);
}

JANET_CFUN(cfun_add_hook) {
  janet_fixarity(argc, 2);
  auto &interop = singleton_t<janet_interop_t>::get();

  auto  event_symbol = janet_getsymbol(argv, 0);
  Janet event_list{};
  janet_resolve(interop.env, event_symbol, &event_list);

  if (janet_type(event_list) != JANET_ARRAY) {
    // TODO: Gracefully handle failure
    throw std::runtime_error{ "Expected type to be array" };
  }

  // The second argument of `(add-hook hook-symbol ?)' is either
  //
  //  A. Direct `JanetFunction' (Lambda)
  //  B. A symbol, which we have to resolve into a function

  JanetFunction *callback = nullptr;
  if (janet_type(argv[1]) == JANET_FUNCTION) {
    callback = janet_unwrap_function(argv[1]);
    goto success;
  } else if (janet_type(argv[1]) == JANET_SYMBOL) {
    Janet callback_symbol;
    janet_resolve(interop.env, janet_unwrap_symbol(argv[1]), &callback_symbol);

    // Only if the resolved type is a function can we proceed with the
    // success case.
    if (janet_type(callback_symbol) == JANET_FUNCTION) {
      callback = janet_unwrap_function(callback_symbol);
      goto success;
    }
  }
wrong_type:
  // TODO: Can we stringify the type?
  ERROR("(add-hook symbol callback): Unexpected callback parameter, expected symbol or function. "
        "Got {}",
        (int)janet_type(argv[1]));
  return janet_wrap_false();

success:
  janet_gcroot(janet_wrap_function(callback));
  janet_array_push(janet_unwrap_array(event_list), janet_wrap_function(callback));
  return janet_wrap_nil();
}

JANET_CFUN(cfun_remove_hook) {
  janet_fixarity(argc, 2);
  auto &interop = singleton_t<janet_interop_t>::get();

  auto  event_symbol = janet_getsymbol(argv, 0);
  Janet event_list{};

  // 1. Resolve the hook symbol to get the array of callbacks
  janet_resolve(interop.env, event_symbol, &event_list);

  if (janet_type(event_list) != JANET_ARRAY) {
    // Handle case where hook name doesn't point to an array
    // (This should match the error handling of add-hook)
    throw std::runtime_error{ "Expected hook target to be an array" };
  }

  JanetArray *callbacks_array = janet_unwrap_array(event_list);
  Janet       target_callback = janet_wrap_nil();

  // 2. Resolve the second argument (the callback) into a JanetFunction
  if (janet_type(argv[1]) == JANET_FUNCTION) {
    // Case A: Direct JanetFunction (Lambda)
    target_callback = argv[1];
    goto success;
  } else if (janet_type(argv[1]) == JANET_SYMBOL) {
    // Case B: A symbol, which we resolve into a function
    Janet resolved_callback;
    janet_resolve(interop.env, janet_unwrap_symbol(argv[1]), &resolved_callback);

    if (janet_type(resolved_callback) == JANET_FUNCTION) {
      target_callback = resolved_callback;
      goto success;
    }
  }

wrong_type:
  ERROR("(remove-hook symbol callback): Unexpected callback parameter, expected symbol or "
        "function. Got {}",
        (int)janet_type(argv[1]));
  return janet_wrap_false();

success:

  bool success;
  int  index = -1;
  for (auto i = 0; i < callbacks_array->count; ++i) {
    if (janet_equals(callbacks_array->data[i], target_callback)) {
      index = i;
      break;
    }
  }

  if (index != -1) {
    // Swap the last callback with this callback and reset the size
    Janet removal                = callbacks_array->data[index];
    callbacks_array->data[index] = callbacks_array->data[callbacks_array->count - 1];
    callbacks_array->data[callbacks_array->count - 1] = janet_wrap_nil();

    janet_array_setcount(callbacks_array, callbacks_array->count - 1);
    success = true;
  } else {
    WARN("Tried to remove callback from event list that wasn't present in the first place.");
    success = false;
  }

  if (success) {
    janet_gcunroot(target_callback);
    return janet_wrap_true(); // Return true on successful removal
  } else {
    // Callback not found in the list
    return janet_wrap_false(); // Return false if the item wasn't in the list
  }
}

void
janet_module_t<compositor_t>::import(JanetTable *env) {
  constexpr static JanetReg compositor_fns[] = {
    {    "add-hook",
     &cfun_add_hook,
     "(add-hook event fn)\n\nAdd a hook to the given `event', calling `fn' when it triggers."         },
    { "remove-hook",
     &cfun_remove_hook,
     "(remove-hook symbol)\n\nRemove the hook named `symbol' from the callback list."                 },
    { "run-command",
     &cfun_run_command,
     "(run-command string)\n\nRun command supplied via `string', and run via `sh -c`."                },
    {       nullptr, nullptr,                                                                  nullptr }
  };
  janet_cfuns(env, "barock", compositor_fns);
}
