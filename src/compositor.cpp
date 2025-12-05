#include "barock/compositor.hpp"
#include "barock/core/cursor_manager.hpp"
#include "barock/core/event_loop.hpp"
#include "barock/core/input.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/hotkey.hpp"
#include "barock/render/opengl.hpp"
#include "barock/resource.hpp"
#include "barock/script/janet.hpp"
#include "barock/singleton.hpp"
#include "barock/util.hpp"
#include "log.hpp"

#include <spawn.h>
#include <wayland-egl-backend.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <fstream>
#include <sstream>

using namespace barock;

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

static Janet
cfun_run_command(int argc, Janet *argv) {
  janet_fixarity(argc, 1);

  std::string command = (const char *)janet_getstring(argv, 0);
  pid_t       pid     = run_command(command);
  return janet_wrap_integer((int)pid);
}

compositor_t::compositor_t(minidrm::drm::handle_t drm_handle, const std::string &seat)
  : drm_handle(drm_handle)
  , input() {
  using std::make_unique;

  janet_init();

  context_ = janet_core_env(nullptr);
  singleton_t<janet_interop_t>::ensure(janet_interop_t{ .env = context_, .compositor = this });

  display_ = wl_display_create();
  wl_display_add_socket(display_, nullptr);

  auto wl_event_loop = wl_display_get_event_loop(display_);

  TRACE("* Initializing Event Loop");
  event_loop = make_unique<event_loop_t>(wl_event_loop);

  TRACE("* Initializing Input Manager");
  input = make_unique<input_manager_t>(seat);

  event_loop->add_fd(
    input->fd(),
    WL_EVENT_READABLE,
    [](auto, auto, void *ud) -> int {
      reinterpret_cast<compositor_t *>(ud)->input->poll(0);
      return 0;
    },
    this);

  output = make_unique<output_manager_t>(drm_handle);

  TRACE("* Initializing Cursor Manager");
  cursor = make_unique<cursor_manager_t>(*output, *input);

  TRACE("* Initializing Hotkey Manager");
  hotkey = make_unique<hotkey_t>(*input);

  TRACE("* Initializing wl_compositor protocol");
  wl_compositor = make_unique<wl_compositor_t>(display_);

  constexpr static JanetReg compositor_fns[] = {
    { "run-command",
     &cfun_run_command,
     "(run-command array)\n\nRun command supplied via `array', array is joined via ' ' and then "
     "sent to a shell."              },
    {       nullptr, nullptr, nullptr }
  };
  janet_cfuns(context_, "barock", compositor_fns);
}

compositor_t::~compositor_t() {
  janet_deinit();
}

wl_display *
compositor_t::display() {
  return display_;
}

void
compositor_t::load_file(const std::string &file) {
  std::ifstream     stream(file);
  std::stringstream ss;
  ss << stream.rdbuf();

  janet_dostring(context_, ss.str().c_str(), file.c_str(), nullptr);
}

// void
// compositor_t::_pointer::send_enter(shared_t<resource_t<surface_t>> &surf) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   region_t bounds = surf->extent();

//   double local_x{}, local_y{};

//   local_x = root->cursor.x - bounds.x;
//   local_y = root->cursor.y - bounds.y;

//   // Figure out whether the client has
//   // A.) A `wl_seat` configured.
//   // B.) A `wl_pointer` attached to that `wl_seat`.
//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto pointer = seat->pointer.lock(); pointer) {
//       wl_pointer_send_enter(pointer->resource(),
//                             wl_display_next_serial(root->display()),
//                             surf->resource(),
//                             wl_fixed_from_double(local_x),
//                             wl_fixed_from_double(local_y));
//       wl_pointer_send_frame(pointer->resource());
//     }
//   }
// }

// void
// compositor_t::_pointer::send_leave(shared_t<resource_t<surface_t>> &surf) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto pointer = seat->pointer.lock(); pointer) {
//       wl_pointer_send_leave(
//         pointer->resource(), wl_display_next_serial(root->display()), surf->resource());
//       wl_pointer_send_frame(pointer->resource());
//     }
//   }
// }

// void
// compositor_t::_pointer::send_button(shared_t<resource_t<surface_t>> &surf,
//                                     uint32_t                         button,
//                                     uint32_t                         state) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto pointer = seat->pointer.lock(); pointer) {
//       wl_pointer_send_button(pointer->resource(),
//                              wl_display_next_serial(root->display()),
//                              current_time_msec(),
//                              button,
//                              state);
//       wl_pointer_send_frame(pointer->resource());
//     }
//   }
// }

// void
// compositor_t::_pointer::send_motion(shared_t<resource_t<surface_t>> &surface) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surface->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto pointer = seat->pointer.lock(); pointer) {
//       auto position = surface->position();

//       double local_x{}, local_y{};
//       local_x = (root->cursor.x - position.x);
//       local_y = (root->cursor.y - position.y);

//       wl_pointer_send_motion(pointer->resource(),
//                              current_time_msec(),
//                              wl_fixed_from_double(local_x),
//                              wl_fixed_from_double(local_y));
//       wl_pointer_send_frame(pointer->resource());
//     }
//   }
// }

// void
// compositor_t::_pointer::send_motion(shared_t<resource_t<surface_t>> &surface, double x, double y)
// {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surface->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto pointer = seat->pointer.lock(); pointer) {
//       wl_pointer_send_motion(
//         pointer->resource(), current_time_msec(), wl_fixed_from_double(x),
//         wl_fixed_from_double(y));
//       wl_pointer_send_frame(pointer->resource());
//     }
//   }
// }

// void
// compositor_t::_pointer::send_axis(shared_t<resource_t<surface_t>> &surface,
//                                   int                              axis,
//                                   double                           delta) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surface->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto pointer = seat->pointer.lock(); pointer) {
//       wl_pointer_send_axis(
//         pointer->resource(), current_time_msec(), axis, wl_fixed_from_double(delta));
//       wl_pointer_send_frame(pointer->resource());
//     }
//   }
// }

// void
// compositor_t::_pointer::set_focus(shared_t<resource_t<surface_t>> surf) {
//   if (auto surface = focus.lock(); surface) {
//     // Send leave event
//     auto      &wl_seat = root->wl_seat;
//     wl_client *client  = surface->owner();

//     if (auto seat = wl_seat->find(client); seat) {
//       if (auto pointer = seat->pointer.lock(); pointer) {
//         wl_pointer_send_leave(
//           pointer->resource(), wl_display_next_serial(root->display()), surface->resource());
//       }
//     }
//   }

//   focus = surf;
//   if (surf)
//     send_enter(surf);
// }

// void
// compositor_t::_keyboard::send_enter(shared_t<resource_t<surface_t>> &surf) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   // Figure out whether the client has
//   // A.) A `wl_seat` configured.
//   // B.) A `wl_keyboard` attached to that `wl_seat`.
//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto keyboard = seat->keyboard.lock(); keyboard) {
//       wl_array keys;
//       wl_array_init(&keys);
//       wl_keyboard_send_enter(
//         keyboard->resource(), wl_display_next_serial(root->display()), surf->resource(), &keys);
//       wl_array_release(&keys);
//     }
//   }
// }

// void
// compositor_t::_keyboard::send_leave(shared_t<resource_t<surface_t>> &surf) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   // Figure out whether the client has
//   // A.) A `wl_seat` configured.
//   // B.) A `wl_keyboard` attached to that `wl_seat`.
//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto keyboard = seat->keyboard.lock(); keyboard) {
//       wl_keyboard_send_leave(
//         keyboard->resource(), wl_display_next_serial(root->display()), surf->resource());
//     }
//   }
// }

// void
// compositor_t::_keyboard::send_key(shared_t<resource_t<surface_t>> &surf,
//                                   uint32_t                         key,
//                                   uint32_t                         state) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto keyboard = seat->keyboard.lock(); keyboard) {

//       wl_keyboard_send_key(keyboard->resource(),
//                            wl_display_next_serial(root->display()),
//                            current_time_msec(),
//                            key,
//                            state);
//     }
//   }
// }

// void
// compositor_t::_keyboard::send_modifiers(shared_t<resource_t<surface_t>> &surf,
//                                         uint32_t                         depressed,
//                                         uint32_t                         latched,
//                                         uint32_t                         locked,
//                                         uint32_t                         group) {
//   auto      &wl_seat = root->wl_seat;
//   wl_client *client  = surf->owner();

//   if (auto seat = wl_seat->find(client); seat) {
//     if (auto keyboard = seat->keyboard.lock(); keyboard) {

//       wl_keyboard_send_modifiers(keyboard->resource(),
//                                  wl_display_next_serial(root->display()),
//                                  depressed,
//                                  latched,
//                                  locked,
//                                  group);
//     }
//   }
// }

// void
// compositor_t::_keyboard::set_focus(shared_t<resource_t<surface_t>> surf) {
//   if (auto surface = focus.lock(); surface) {
//     // Send leave event
//     auto      &wl_seat = root->wl_seat;
//     wl_client *client  = surface->owner();
//     if (auto seat = wl_seat->find(client); seat) {
//       if (auto keyboard = seat->keyboard.lock(); keyboard) {
//         wl_keyboard_send_leave(
//           keyboard->resource(), wl_display_next_serial(root->display()), surface->resource());
//       }
//     }
//   }

//   focus = surf;
//   if (surf) // handle nullptr (no focus)
//     send_enter(surf);
// }

// void
// compositor_t::_window::activate(const shared_t<surface_t> &surface) {
//   // We can't activate a surface that has no role.
//   if (!surface->has_role()) {
//     TRACE("Tried to activate window without a role!");
//     return;
//   }

//   if (surface->role->type_id() == xdg_surface_t::id()) {
//     auto xdg_surface = shared_cast<xdg_surface_t>(surface->role);
//     root->xdg_shell->activate(xdg_surface);
//     activated = surface;
//     return;
//   }
//   assert(false && "Unhandled surface role in compositor_t::window_#activate");
// }

// void
// compositor_t::_window::deactivate(const shared_t<surface_t> &surface) {
//   // We can't deactivate a surface that has no role.
//   if (!surface->has_role()) {
//     TRACE("Trying to deactivate surface that has no role!");
//     return;
//   }

//   if (surface->role->type_id() == xdg_surface_t::id()) {
//     auto xdg_surface = shared_cast<xdg_surface_t>(surface->role);
//     root->xdg_shell->deactivate(xdg_surface);
//     activated = nullptr;
//     return;
//   }
//   assert(false && "Unhandled surface role in compositor_t::window_#activate");
// }
