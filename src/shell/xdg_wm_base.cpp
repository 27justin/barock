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

  xdg_shell_t::xdg_shell_t(wl_display *display, service_registry_t &registry)
    : display_(display)
    , registry(registry) {
    wl_global_create(display, &xdg_wm_base_interface, 1, this, bind);

    for (auto &out : registry.output->outputs()) {
      on_output_new(*out);
    }
    registry.output->events.on_output_new.connect(
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
    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
      auto &xdg_surface = *it;
      if (auto surface = xdg_surface->surface.lock(); surface) {
        // Cull windows that are not visible
        if (output.is_visible({ xdg_surface->position, xdg_surface->size }) == false) {
          WARN("Window is not visible. Culling.");
          continue;
        }

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
    // Deactivate the currently active window (if any)
    if (auto surf = activated_.lock(); surf)
      deactivate(surf);

    // Send the configure event
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

    // Track the surface as the currently active one. Active surface
    // are unique, we can only ever have one active one.
    activated_ = shared_cast<resource_t<xdg_surface_t>>(xdg_surface);
  }

  shared_t<resource_t<xdg_surface_t>>
  xdg_shell_t::by_position(output_t &output, const fpoint_t &position) {
    auto &windows = output.metadata.get<xdg_window_list_t>();

    for (auto it = windows.begin(); it != windows.end(); ++it) {
      if (position >= (*it)->position && position < ((*it)->position + (*it)->size)) {
        return shared_cast<resource_t<xdg_surface_t>>(*it);
      }
    }
    return nullptr;
  }

  shared_t<xdg_toplevel_t>
  xdg_shell_t::by_app_id(std::string_view app_id, jsl::optional_t<const output_t &> output) const {
    auto search = +[](const output_t &output, std::string_view app_id) -> shared_t<xdg_toplevel_t> {
      const xdg_window_list_t &windows = output.metadata.get<xdg_window_list_t>();

      // Look through all windows on this output
      for (auto const &window : windows) {
        // We can ignore surfaces that aren't toplevels
        if (window->role != xdg_role_t::eToplevel)
          continue;
        auto const &toplevel = shared_cast<xdg_toplevel_t>(window->role_impl);

        // Return this surface, if it's what we're looking for.
        if (toplevel->app_id == app_id)
          return toplevel;
      }
      return nullptr;
    };

    // When called with default argument (no output), search for the
    // window on all outputs.
    if (output.valid() == false) {
      auto outputs = registry.output->outputs();
      for (auto &output : outputs) {
        auto surface = search(*output, app_id);
        // If not `nullptr', we can return it.
        if (surface != nullptr)
          return surface;
      }
    } else {
      // On specific output we return whatever we find.
      return search(output.value(), app_id);
    }
    return nullptr;
  }

  void
  xdg_shell_t::raise_to_top(shared_t<xdg_surface_t> surface, jsl::optional_t<output_t &> output) {
    auto raise = +[](output_t &output, shared_t<xdg_surface_t> surface) {
      xdg_window_list_t &windows = output.metadata.get<xdg_window_list_t>();

      auto it = std::find(windows.begin(), windows.end(), surface);
      if (it != windows.end()) {
        windows.erase(it);
        windows.insert(windows.begin(), surface);
      }
    };

    // When called with default argument (no output), raise the window
    // to the top on all connected outputs.
    if (output.valid() == false) {
      // We support rendering one client on multiple outputs, therefore
      // iterate all outputs for their window list, and move the
      // `surface' to the front of the window list.
      auto outputs = registry.output->outputs();
      for (auto &output : outputs) {
        raise(*output, surface);
      }
    } else {
      // Raise window on specific output to the top.
      raise(output.value(), surface);
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

  surface->role = xdg_surface;

  // Send the configure event
  xdg_surface_send_configure(xdg_surface->resource(), wl_display_next_serial(shell->display_));
}
