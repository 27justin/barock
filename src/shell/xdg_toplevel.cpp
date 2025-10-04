#include "barock/shell/xdg_toplevel.hpp"
#include "../log.hpp"

void
set_title(wl_client *client, wl_resource *res, const char *title) {
  INFO("set_title \"{}\"", title);
}

void
set_app_id(wl_client *client, wl_resource *res, const char *title) {
  INFO("set_app_id \"{}\"", title);
}

struct xdg_toplevel_interface xdg_toplevel_impl = { .destroy          = nullptr,
                                                    .set_parent       = nullptr,
                                                    .set_title        = set_title,
                                                    .set_app_id       = set_app_id,
                                                    .show_window_menu = nullptr,
                                                    .move             = nullptr,
                                                    .resize           = nullptr,
                                                    .set_max_size     = nullptr,
                                                    .set_min_size     = nullptr,
                                                    .set_maximized    = nullptr,
                                                    .unset_maximized  = nullptr,
                                                    .set_fullscreen   = nullptr,
                                                    .unset_fullscreen = nullptr,
                                                    .set_minimized    = nullptr };
