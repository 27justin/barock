#include "barock/compositor.hpp"
#include "barock/core/cursor_manager.hpp"
#include "barock/core/event_loop.hpp"
#include "barock/core/input.hpp"
#include "barock/core/shm.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/core/wl_data_device_manager.hpp"
#include "barock/core/wl_output.hpp"
#include "barock/core/wl_seat.hpp"
#include "barock/hotkey.hpp"
#include "barock/render/opengl.hpp"
#include "barock/resource.hpp"
#include "barock/script/janet.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "barock/singleton.hpp"
#include "barock/util.hpp"
#include "log.hpp"

#include <wayland-egl-backend.h>
#include <wayland-server-core.h>
#include <wayland-util.h>

#include <fstream>
#include <sstream>

using namespace barock;

compositor_t::compositor_t(minidrm::drm::handle_t drm_handle, const std::string &seat)
  : drm_handle(drm_handle) {

  // TODO: I usually am firmly opposed to using singletons, because
  // they make testing difficult. However, not having a singleton for
  // the compositor available makes it very difficult to write good
  // `Janet' bindings, because more often than not, we need access to
  // the compositor.
  //
  // Anyhow; Figure out whether we can do without singletons, it's
  // fine for now, but if it becomes unmaintainable, we have to think
  // of some other approach.
  singleton_t<compositor_t>::set(this);

  using std::make_unique;
  display_ = wl_display_create();
  wl_display_add_socket(display_, nullptr);

  auto wl_event_loop = wl_display_get_event_loop(display_);

  janet_init();
  context_ = janet_core_env(nullptr);
  singleton_t<janet_interop_t>::ensure(janet_interop_t{ .env = context_, .compositor = this });

  TRACE("* Initializing Event Loop");
  registry_.event_loop = make_unique<event_loop_t>(wl_event_loop);

  TRACE("* Initializing Input Manager");
  registry_.input = make_unique<input_manager_t>(seat, registry_);

  registry_.event_loop->add_fd(
    registry_.input->fd(),
    WL_EVENT_READABLE,
    [](auto, auto, void *ud) -> int {
      reinterpret_cast<compositor_t *>(ud)->registry_.input->poll(0);
      return 0;
    },
    this);

  registry_.output = make_unique<output_manager_t>(drm_handle);

  TRACE("* Initializing Cursor Manager");
  registry_.cursor = make_unique<cursor_manager_t>(registry_);

  TRACE("* Initializing Hotkey Manager");
  registry_.hotkey = make_unique<hotkey_t>(registry_);

  TRACE("* Initializing `wl_compositor` Protocol");
  registry_.wl_compositor = make_unique<wl_compositor_t>(display_);

  TRACE("* Initializing `wl_subcompositor` Protocol");
  registry_.wl_subcompositor = make_unique<wl_subcompositor_t>(display_, registry_);

  TRACE("* Initializing `wl_shm` Protocol");
  registry_.shm = make_unique<shm_t>(display_);

  TRACE("* Initializing `wl_data_device_manager` Protocol");
  registry_.wl_data_device_manager = make_unique<wl_data_device_manager_t>(display_);

  TRACE("* Initializing `wl_seat` Protocol");
  registry_.seat = make_unique<wl_seat_t>(display_, registry_);

  TRACE("* Initializing `wl_output` Protocol");
  registry_.wl_output = make_unique<wl_output_t>(display_, registry_);

  TRACE("* Initializing XDG Shell Protocol");
  registry_.xdg_shell = make_unique<xdg_shell_t>(display_, registry_);

  TRACE("* Initializing Event Bus");
  registry_.event_bus = make_unique<event_bus_t>();

  TRACE("* Initializing Janet modules ({})", janet_module_loader_t::get_modules().size());
  janet_module_loader_t::run_all_imports(context_);
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

  auto code = janet_dostring(context_, ss.str().c_str(), file.c_str(), nullptr);
  if (code != 0) {
    CRITICAL("Failed to load Janet config: {}", code);
    std::exit(1);
  }
}

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
