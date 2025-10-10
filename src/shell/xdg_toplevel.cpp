#include "barock/input.hpp"
#include "barock/resource.hpp"

#include "barock/core/shm_pool.hpp"

#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"

#include <linux/input.h>
#include <wayland-util.h>

#include "../log.hpp"

using namespace barock;

namespace barock {
  xdg_toplevel_t::xdg_toplevel_t(shared_t<resource_t<xdg_surface_t>> base,
                                 const xdg_toplevel_data_t          &prop_data)
    : xdg_surface(base)
    , data(prop_data) {

    // Attach on_buffer_attach listener to resize the window
    on_buffer_attach = base->surface.lock()->on_buffer_attach.connect(
      [&data = this->data, surface = this->xdg_surface,
       &on_buffer_attach = this->on_buffer_attach](const shm_buffer_t &buf) mutable {
        // TODO: We should only do this once, right now it auto resizes
        // to always match the buffer contents.
        data.width  = buf.width;
        data.height = buf.height;

        // Immediately disconnect, we only resize once to fit.
        if (auto xdg_surface = surface.lock()) {
          if (auto wl_surface = xdg_surface->surface.lock()) {
            wl_surface->on_buffer_attach.disconnect(on_buffer_attach);
          }
        }
      });
  }

  xdg_toplevel_t::~xdg_toplevel_t() {
    if (auto surf = xdg_surface.lock(); surf) {
      surf->surface.lock()->on_buffer_attach.disconnect(on_buffer_attach);
    }
  }
}

void
xdg_toplevel_set_title(wl_client *client, wl_resource *xdg_toplevel, const char *title) {
  auto surface        = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
  surface->data.title = title;
}

void
xdg_toplevel_set_app_id(wl_client *client, wl_resource *xdg_toplevel, const char *app_id) {
  auto surface         = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
  surface->data.app_id = app_id;
}

void
xdg_toplevel_set_max_size(wl_client   *client,
                          wl_resource *xdg_toplevel,
                          int32_t      width,
                          int32_t      height) {
  INFO("set max size: {}x{}", width, height);
  auto surface = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
};

void
xdg_toplevel_set_min_size(wl_client   *client,
                          wl_resource *xdg_toplevel,
                          int32_t      width,
                          int32_t      height) {
  INFO("set min size: {}x{}", width, height);
  auto surface = from_wl_resource<xdg_toplevel_t>(xdg_toplevel);
};

void
xdg_toplevel_destroy(wl_client *client, wl_resource *wl_xdg_toplevel);

void
xdg_toplevel_move(wl_client   *client,
                  wl_resource *wl_xdg_toplevel,
                  wl_resource *seat,
                  uint32_t     serial);

void
xdg_toplevel_resize(wl_client   *client,
                    wl_resource *wl_xdg_toplevel,
                    wl_resource *seat,
                    uint32_t     serial,
                    uint32_t     edges);

struct xdg_toplevel_interface xdg_toplevel_impl = { .destroy          = xdg_toplevel_destroy,
                                                    .set_parent       = nullptr,
                                                    .set_title        = xdg_toplevel_set_title,
                                                    .set_app_id       = xdg_toplevel_set_app_id,
                                                    .show_window_menu = nullptr,
                                                    .move             = xdg_toplevel_move,
                                                    .resize           = xdg_toplevel_resize,
                                                    .set_max_size     = xdg_toplevel_set_max_size,
                                                    .set_min_size     = xdg_toplevel_set_min_size,
                                                    .set_maximized    = nullptr,
                                                    .unset_maximized  = nullptr,
                                                    .set_fullscreen   = nullptr,
                                                    .unset_fullscreen = nullptr,
                                                    .set_minimized    = nullptr };

void
xdg_toplevel_move(wl_client   *client,
                  wl_resource *wl_xdg_toplevel,
                  wl_resource *seat,
                  uint32_t     serial) {
  auto toplevel = from_wl_resource<xdg_toplevel_t>(wl_xdg_toplevel);

  shared_t<resource_t<xdg_surface_t>> xdg_surface;
  if (xdg_surface = toplevel->xdg_surface.lock(); !xdg_surface) {
    ERROR("xdg_toplevel wants to be moved, but attached xdg_surface is not valid!");
    return;
  }

  shared_t<resource_t<surface_t>> wl_surface;
  if (wl_surface = xdg_surface->surface.lock(); !wl_surface) {
    ERROR("xdg_toplevel wants to be moved, but attached wl_surface is not valid!");
    return;
  }

  auto compositor = wl_surface->compositor;

  struct _data {
    signal_token_t on_mouse_move, on_mouse_button;
    double         start_x{}, start_y{};
    int32_t        win_x{}, win_y{};
  };
  auto subscriptions     = shared_t(new _data{});
  subscriptions->start_x = compositor->cursor.x;
  subscriptions->start_y = compositor->cursor.y;
  subscriptions->win_x   = toplevel->data.x;
  subscriptions->win_y   = toplevel->data.y;

  // If triggered, the surface will lose the focus of the device
  // (wl_pointer, wl_touch, etc) used for the move. It is up to
  // the compositor to visually indicate that the move is taking
  // place, such as updating a pointer cursor, during the
  // move. There is no guarantee that the device focus will return
  // when the move is completed.
  compositor->pointer.set_focus(nullptr);
  compositor->keyboard.set_focus(nullptr);

  subscriptions->on_mouse_move = compositor->input->on_mouse_move.connect(
    [toplevel, compositor, subscriptions](const auto &event) mutable {
      // This is guaranteed to be processed after the compositor
      // itself used this event to set the cursor struct, therefore
      // we can just move according to the compositor cursor
      double dx = compositor->cursor.x - subscriptions->start_x;
      double dy = compositor->cursor.y - subscriptions->start_y;

      toplevel->data.x = subscriptions->win_x + static_cast<int>(dx);
      toplevel->data.y = subscriptions->win_y + static_cast<int>(dy);
    });

  subscriptions->on_mouse_button = compositor->input->on_mouse_button.connect(
    [compositor, subscriptions, wl_surface](const auto &event) {
      // Left mouse released
      if (event.button == BTN_LEFT && event.state == 0) {
        // Refocus the surface
        compositor->pointer.set_focus(wl_surface);

        compositor->input->on_mouse_move.disconnect(subscriptions->on_mouse_move);
        compositor->input->on_mouse_button.disconnect(subscriptions->on_mouse_button);
      }
    });
}

void
xdg_toplevel_resize(wl_client   *client,
                    wl_resource *wl_xdg_toplevel,
                    wl_resource *seat,
                    uint32_t     serial,
                    uint32_t     edges) {
  auto toplevel = from_wl_resource<xdg_toplevel_t>(wl_xdg_toplevel);

  shared_t<resource_t<xdg_surface_t>> xdg_surface;
  if (xdg_surface = toplevel->xdg_surface.lock(); !xdg_surface) {
    ERROR("xdg_toplevel wants to be resized, but attached xdg_surface is not valid!");
    return;
  }

  shared_t<resource_t<surface_t>> wl_surface;
  if (wl_surface = xdg_surface->surface.lock(); !wl_surface) {
    ERROR("xdg_toplevel wants to be resized, but attached wl_surface is not valid!");
    return;
  }

  auto compositor = wl_surface->compositor;

  struct _data {
    signal_token_t    on_mouse_move, on_mouse_button;
    double            start_x{}, start_y{};
    int32_t           win_x{}, win_y{}, win_w{}, win_h{};
    uint32_t          edges{};
    std::atomic<bool> active{ true };
  };

  auto subscriptions     = shared_t(new _data{});
  subscriptions->start_x = compositor->cursor.x;
  subscriptions->start_y = compositor->cursor.y;
  subscriptions->win_x   = toplevel->data.x;
  subscriptions->win_y   = toplevel->data.y;
  subscriptions->win_w   = toplevel->data.width;
  subscriptions->win_h   = toplevel->data.height;
  subscriptions->edges   = edges;

  // Same behavior as move: lose focus
  compositor->pointer.set_focus(nullptr);
  compositor->keyboard.set_focus(nullptr);

  subscriptions->on_mouse_move = compositor->input->on_mouse_move.connect(
    [wl_surface, toplevel, compositor, subscriptions](const auto &event) mutable {
      if (!subscriptions->active.load())
        return;

      double dx = compositor->cursor.x - subscriptions->start_x;
      double dy = compositor->cursor.y - subscriptions->start_y;

      int32_t new_x = subscriptions->win_x;
      int32_t new_y = subscriptions->win_y;
      int32_t new_w = subscriptions->win_w;
      int32_t new_h = subscriptions->win_h;

      // Adjust horizontal size and/or position
      if (subscriptions->edges & XDG_TOPLEVEL_RESIZE_EDGE_LEFT) {
        new_x += static_cast<int>(dx);
        new_w -= static_cast<int>(dx);
      } else if (subscriptions->edges & XDG_TOPLEVEL_RESIZE_EDGE_RIGHT) {
        new_w += static_cast<int>(dx);
      }

      // Adjust vertical size and/or position
      if (subscriptions->edges & XDG_TOPLEVEL_RESIZE_EDGE_TOP) {
        new_y += static_cast<int>(dy);
        new_h -= static_cast<int>(dy);
      } else if (subscriptions->edges & XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM) {
        new_h += static_cast<int>(dy);
      }

      // Enforce minimum size constraints
      new_w = std::max(1, new_w);
      new_h = std::max(1, new_h);

      toplevel->data.x      = new_x;
      toplevel->data.y      = new_y;
      toplevel->data.width  = new_w;
      toplevel->data.height = new_h;

      // Send configure event
      wl_array state;
      wl_array_init(&state);
      *reinterpret_cast<xdg_toplevel_state *>(wl_array_add(&state, sizeof(xdg_toplevel_state))) =
        XDG_TOPLEVEL_STATE_RESIZING;
      xdg_toplevel_send_configure(toplevel->resource(), new_w, new_h, &state);
      wl_array_release(&state);
    });

  subscriptions->on_mouse_button = compositor->input->on_mouse_button.connect(
    [compositor, subscriptions, toplevel, wl_surface](const auto &event) mutable {
      if (event.button == BTN_LEFT && event.state == 0) {
        subscriptions->active.store(false);

        wl_array state;
        wl_array_init(&state);
        *reinterpret_cast<xdg_toplevel_state *>(wl_array_add(&state, sizeof(xdg_toplevel_state))) =
          XDG_TOPLEVEL_STATE_RESIZING;
        xdg_toplevel_send_configure(toplevel->resource(), toplevel->data.width,
                                    toplevel->data.height, &state);
        wl_array_release(&state);

        // Send final empty configure event.
        wl_array_init(&state);
        xdg_toplevel_send_configure(toplevel->resource(), toplevel->data.width,
                                    toplevel->data.height, &state);
        wl_array_release(&state);

        // Refocus the surface
        compositor->pointer.set_focus(wl_surface);

        compositor->input->on_mouse_move.disconnect(subscriptions->on_mouse_move);
        compositor->input->on_mouse_button.disconnect(subscriptions->on_mouse_button);
      }
    });
}

void
xdg_toplevel_destroy(wl_client *client, wl_resource *wl_xdg_toplevel) {
  wl_resource_destroy(wl_xdg_toplevel);
}
