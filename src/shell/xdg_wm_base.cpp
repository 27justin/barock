#include "barock/shell/xdg_wm_base.hpp"
#include "barock/compositor.hpp"

#include "barock/core/renderer.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/script/janet.hpp"
#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/singleton.hpp"

#include "../log.hpp"
#include "wl/xdg-shell-protocol.h"
#include <wayland-server-core.h>

using namespace barock;

void
xdg_wm_base_destroy(wl_client *, wl_resource *);

void
xdg_wm_base_get_xdg_surface(wl_client   *client,
                            wl_resource *xdg_wm_base,
                            uint32_t     id,
                            wl_resource *wl_surface);

struct xdg_wm_base_interface xdg_wm_base_impl = {
  .destroy           = xdg_wm_base_destroy,
  .create_positioner = nullptr,
  .get_xdg_surface   = xdg_wm_base_get_xdg_surface,
  .pong              = nullptr,
};

namespace barock {

  xdg_shell_t::~xdg_shell_t() {}

  xdg_shell_t::xdg_shell_t(wl_display       *display,
                           input_manager_t  &input,
                           output_manager_t &output,
                           cursor_manager_t &cursor)
    : display_(display)
    , input_manager(input)
    , output_manager(output)
    , cursor_manager(cursor) {
    wl_global_create(display, &xdg_wm_base_interface, 1, this, bind);

    for (auto &out : output.outputs()) {
      on_output_new(*out);
    }
    output.events.on_output_new.connect(
      std::bind(&xdg_shell_t::on_output_new, this, std::placeholders::_1));
  }

  signal_action_t
  xdg_shell_t::on_output_new(output_t &output) {
    // The XDG shell holds a list of windows for each output, this has
    // to be initialzied on every monitor, which is what this function
    // does.
    output.metadata.ensure<xdg_window_list_t>();

    // We also have to attach our `repaint` listener, that actually draws the windows
    output.events.on_repaint[XDG_SHELL_PAINT_LAYER].connect(
      std::bind(&xdg_shell_t::paint, this, std::placeholders::_1));
    return signal_action_t::eOk;
  }

  signal_action_t
  xdg_shell_t::paint(output_t &output) {
    auto renderer = &output.renderer();

    auto &windows = output.metadata.get<xdg_window_list_t>();
    for (auto &xdg_surface : windows) {
      if (auto surface = xdg_surface->surface.lock(); surface) {
        auto position = output.to<output_t::eWorkspace, output_t::eScreenspace>(
          xdg_surface->position - xdg_surface->offset);

        renderer->draw(*surface, position);
      }
    }
    return signal_action_t::eOk;
  }

  void
  xdg_shell_t::bind(wl_client *client, void *ud, uint32_t version, uint32_t id) {
    xdg_shell_t        *shell    = reinterpret_cast<xdg_shell_t *>(ud);
    struct wl_resource *resource = wl_resource_create(client, &xdg_wm_base_interface, version, id);

    wl_resource_set_implementation(resource, &xdg_wm_base_impl, shell, NULL);
  }

  void
  xdg_shell_t::deactivate(const shared_t<xdg_surface_t> &xdg_surface) {
    switch (xdg_surface->role) {
      case xdg_role_t::eToplevel: {
        auto     toplevel = shared_cast<resource_t<xdg_toplevel_t>>(xdg_surface->role_impl);
        wl_array state;
        wl_array_init(&state);
        xdg_toplevel_send_configure(
          toplevel->resource(), xdg_surface->size.x, xdg_surface->size.y, &state);
        wl_array_release(&state);
        break;
      }
      default: {
        ERROR("Tried to deactivate role {}; not implemented!", (int)xdg_surface->role);
        assert(false && "Unhandled xdg_surface role in xdg_shell_t#deactivate!");
      }
    }
  }

  void
  xdg_shell_t::activate(const shared_t<xdg_surface_t> &xdg_surface) {

    // First move the xdg_surface in `windows` from position X to the
    // last one (we draw the top-most surface last, also makes it
    // easier to loop over them in a consistent manner.)

    auto begin = xdg_surface->shell.windows.begin();
    auto end   = xdg_surface->shell.windows.end();
    auto it    = std::find(begin, end, xdg_surface);
    if (it != end) {
      xdg_surface->shell.windows.erase(it);
      xdg_surface->shell.windows.insert(xdg_surface->shell.windows.begin(), xdg_surface);
    } else {
      // TODO: This is just for debugging, but i think the assert here
      // is bad, as the compositor, we should just not move the
      // xdg_surface, if it's not part of the windows list.
      assert(false && "Activated a window that is not a top-level xdg surface.");
    }

    // Then send the configure event
    switch (xdg_surface->role) {
      case xdg_role_t::eToplevel: {
        auto     toplevel = shared_cast<resource_t<xdg_toplevel_t>>(xdg_surface->role_impl);
        wl_array state;
        wl_array_init(&state);
        void *p                    = wl_array_add(&state, sizeof(xdg_toplevel_state));
        *((xdg_toplevel_state *)p) = XDG_TOPLEVEL_STATE_ACTIVATED;
        xdg_toplevel_send_configure(
          toplevel->resource(), xdg_surface->size.x, xdg_surface->size.y, &state);
        wl_array_release(&state);
        break;
      }
      default: {
        ERROR("Tried to activate role {}; not implemented!", (int)xdg_surface->role);
        assert(false && "Unhandled xdg_surface role in xdg_shell_t#activate!");
      }
    }
  }
}

void
xdg_wm_base_destroy(wl_client *client, wl_resource *res) {
  wl_resource_destroy(res);
}

void
xdg_wm_base_get_xdg_surface(wl_client   *client,
                            wl_resource *xdg_wm_base,
                            uint32_t     id,
                            wl_resource *wl_surface) {
  xdg_shell_t *shell = reinterpret_cast<xdg_shell_t *>(wl_resource_get_user_data(xdg_wm_base));

  // Check whether client created a surface on our wl_compositor yet.
  if (!wl_surface) {
    wl_client_post_no_memory(client);
    return;
  }

  shared_t<resource_t<surface_t>> surface = from_wl_resource<surface_t>(wl_surface);

  auto xdg_surface = make_resource<xdg_surface_t>(client,
                                                  xdg_surface_interface,
                                                  xdg_surface_impl,
                                                  wl_resource_get_version(xdg_wm_base),
                                                  id,
                                                  *shell,
                                                  surface);

  surface->role = xdg_surface.get();

  // Insert at the beginning, this will be the last rendered surface
  // (thus the top-most one.)
  shell->windows.insert(shell->windows.begin(), xdg_surface);

  // Send the configure event
  xdg_surface_send_configure(xdg_surface->resource(), wl_display_next_serial(shell->display_));
}
