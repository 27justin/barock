#pragma once

#include "barock/core/output_manager.hpp"
#include "barock/core/point.hpp"
#include "barock/core/surface.hpp"
#include "barock/fbo.hpp"
#include "barock/resource.hpp"

#include <any>
#include <cstdint>
#include <map>
#include <wayland-server-core.h>

namespace barock {
  struct xdg_shell_t;
  struct xdg_toplevel_t;
  struct service_registry_t;

  enum class xdg_role_t { eToplevel, ePopup, eNone };

  struct xdg_base_role_t {};

  struct xdg_surface_t : public surface_role_t<xdg_surface_t> {
    xdg_shell_t                  &shell;
    weak_t<resource_t<surface_t>> surface;

    xdg_role_t                role;
    shared_t<xdg_base_role_t> role_impl;

    fpoint_t offset;         ///< Logical offset (CSD, etc.), that has to be
                             ///< accounted for.

    fpoint_t position, size; ///< Position & size of the surface

    output_t *output;

    struct {
      signal_t<void> on_geometry_change;
    } events;

    ~xdg_surface_t();
    xdg_surface_t(xdg_shell_t &parent, shared_t<resource_t<surface_t>> base);

    template<typename Cast>
    shared_t<Cast>
    get_role() {
      return shared_cast<Cast>(role_impl);
    }

    template<typename Cast>
    shared_t<Cast>
    get_role() const {
      return shared_cast<Cast>(*const_cast<decltype(role_impl) *>(&role_impl));
    }
  };

  using xdg_window_list_t = std::vector<shared_t<xdg_surface_t>>;

  class xdg_shell_t {
    public:
    static constexpr size_t XDG_SHELL_PAINT_LAYER = 100;
    service_registry_t     &registry;

    wl_display *display_;
    wl_global  *global;

    struct {
      signal_t<shared_t<xdg_surface_t>> on_surface_new;
      signal_t<xdg_toplevel_t &>        on_toplevel_new;
    } events;

    xdg_shell_t(wl_display *display, service_registry_t &registry);
    ~xdg_shell_t();

    /**
     * @brief Activate a surface.  This method sends a `configure`
     * event, with the ACTIVATED flag set.
     */
    void
    activate(const shared_t<xdg_surface_t> &);

    void
    deactivate(const shared_t<xdg_surface_t> &);

    /**
     * @brief Query the surface as `position' on output `output',
     * returns the top-most surface, if found.  otherwise a nullptr.
     *
     * NOTE: `position' has to be in workspace local coordinates.
     */
    shared_t<resource_t<xdg_surface_t>>
    by_position(output_t &, const fpoint_t &);

    private:
    static void
    bind(wl_client *, void *, uint32_t, uint32_t);

    signal_action_t
    on_output_new(output_t &);

    signal_action_t
    paint(output_t &);
  };
}
