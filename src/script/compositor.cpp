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

  auto callback = janet_getfunction(argv, 1);

  janet_gcroot(janet_wrap_function(callback));
  janet_array_push(janet_unwrap_array(event_list), janet_wrap_function(callback));

  return janet_wrap_nil();
}

void
janet_module_t<compositor_t>::import(JanetTable *env) {
  constexpr static JanetReg compositor_fns[] = {
    {    "add-hook",
     &cfun_add_hook,
     "(add-hook event fn)\n\nAdd a hook to the given `event', calling `fn' when it triggers."         },
    { "run-command",
     &cfun_run_command,
     "(run-command string)\n\nRun command supplied via `string', and run via `sh -c`."                },
    {       nullptr, nullptr,                                                                  nullptr }
  };
  janet_cfuns(env, "barock", compositor_fns);
}
