#include "../log.hpp"

#include "barock/compositor.hpp"
#include "barock/core/signal.hpp"
#include "barock/script/interop.hpp"
#include "barock/script/janet.hpp"
#include "barock/shell/xdg_surface.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "barock/singleton.hpp"

namespace barock {
  JANET_MODULE(xdg_shell_t);

  template<>
  struct janet_converter_t<xdg_toplevel_t> {
    Janet
    operator()(const xdg_toplevel_t &window) {
      auto surface = window.xdg_surface.lock();
      if (!surface)
        return janet_wrap_nil();

      JanetTable *table = janet_table(1);

      janet_table_put(table, janet_ckeywordv("app-id"), janet_cstringv(window.app_id.c_str()));
      janet_table_put(table, janet_ckeywordv("title"), janet_cstringv(window.title.c_str()));
      janet_table_put(table,
                      janet_ckeywordv("output"),
                      janet_ckeywordv(surface->output->connector().name().c_str()));
      janet_table_put(table, janet_ckeywordv("type"), janet_ckeywordv("xdg-toplevel"));

      Janet *position = janet_tuple_begin(2);
      position[0]     = janet_wrap_number(surface->position.x);
      position[1]     = janet_wrap_number(surface->position.y);

      Janet *size = janet_tuple_begin(2);
      size[0]     = janet_wrap_number(surface->size.x);
      size[1]     = janet_wrap_number(surface->size.y);

      Janet *offset = janet_tuple_begin(2);
      offset[0]     = janet_wrap_number(surface->offset.x);
      offset[1]     = janet_wrap_number(surface->offset.y);

      janet_table_put(
        table, janet_ckeywordv("position"), janet_wrap_tuple(janet_tuple_end(position)));
      janet_table_put(table, janet_ckeywordv("size"), janet_wrap_tuple(janet_tuple_end(size)));
      janet_table_put(table, janet_ckeywordv("offset"), janet_wrap_tuple(janet_tuple_end(offset)));

      return janet_wrap_table(table);
    }
  };
};

using namespace barock;

JANET_CFUN(cfun_xdg_set_position) {
  janet_arity(argc, 2, 3);

  int  arg   = 0;
  auto table = janet_gettable(argv, arg++);
  auto point = janet_getpoint<float>(argv, arg, true);

  auto connector = janet_table_get(table, janet_ckeywordv("output"));
  auto app_id    = janet_table_get(table, janet_ckeywordv("app-id"));

  // Find the window on (table :output)
  auto &compositor = singleton_t<compositor_t>::get();
  auto  output = compositor.registry_.output->by_name((const char *)janet_unwrap_string(connector));
  if (output.valid() == false) {
    ERROR("Tried to (xdg/set-position) on connector {}, which is not connected.",
          (const char *)janet_unwrap_string(connector));
    return janet_wrap_false();
  }

  auto &window_list = output->metadata.get<xdg_window_list_t>();
  auto  window = std::find_if(window_list.begin(), window_list.end(), [app_id](auto xdg_surface) {
    if (xdg_surface->role != xdg_role_t::eToplevel)
      return false;
    return shared_cast<xdg_toplevel_t>(xdg_surface->role_impl)->app_id ==
           (const char *)janet_unwrap_string(app_id);
  });

  if (window == window_list.end()) {
    ERROR("Tried to set position on window that couldn't be found.");
    return janet_wrap_false();
  }

  (*window)->position.x = point.x;
  (*window)->position.y = point.y;
  return janet_wrap_true();
}

JANET_CFUN(cfun_raise_to_top) {
  // (xdg/raise-to-top window-table &opt :output)
  janet_arity(argc, 1, 2);
  auto &compositor = singleton_t<compositor_t>::get();

  auto window = janet_gettable(argv, 0);
  auto app_id = janet_unwrap_string(janet_table_get(window, janet_ckeywordv("app-id")));

  auto xdg_toplevel = compositor.registry_.xdg_shell->by_app_id((const char *)app_id);

  if (!xdg_toplevel) {
    ERROR("(xdg-raise-to-top window) couldn't find window with :app-id '{}'", (const char *)app_id);
    return janet_wrap_false();
  }

  auto output_name = janet_optkeyword(argv, argc, 1, janet_cstring("all"));
  if ("all" == std::string((const char *)output_name)) {
    // Raise to top on all outputs
    compositor.registry_.xdg_shell->raise_to_top(xdg_toplevel->xdg_surface.lock());
  } else {
    auto output = compositor.registry_.output->by_name((const char *)output_name);
    compositor.registry_.xdg_shell->raise_to_top(xdg_toplevel->xdg_surface.lock(), output);
  }

  return janet_wrap_true();
}

JANET_CFUN(cfun_activate) {
  return janet_wrap_true();
}

JANET_CFUN(cfun_deactivate) {
  return janet_wrap_true();
}

signal_action_t
dispatch_xdg_window_new(xdg_toplevel_t &toplevel) {
  auto &compositor = singleton_t<compositor_t>::get();
  TRACE("(janet module xdg_toplevel_t) Dispatching `xdg-new-window-hook'");

  Janet value;
  janet_resolve(compositor.context_, janet_csymbol("xdg-new-window-hook"), &value);

  if (janet_type(value) != JANET_ARRAY) {
    // TODO: Gracefully handle failure
    throw std::runtime_error{ "Expected type to be array" };
  }

  Janet window = janet_converter_t<xdg_toplevel_t>{}(toplevel);

  JanetArray *array = janet_unwrap_array(value);
  for (int32_t i = 0; i < array->count; ++i) {
    assert(janet_type(array->data[i]) == JANET_FUNCTION);

    JanetFunction *cb    = janet_unwrap_function(array->data[i]);
    JanetFiber    *fiber = janet_fiber(cb, 1, 1, &window);
    Janet          value;
    janet_continue(fiber, janet_wrap_nil(), &value);
  }
  return signal_action_t::eOk;
}

void
janet_module_t<xdg_shell_t>::import(JanetTable *env) {
  constexpr static JanetReg xdg_fns[] = {
    { "xdg/set-position",
     cfun_xdg_set_position,       "(xdg/set-position window-table)\n\nSet the position of the window on the workspace (in "
       "workspace local coordinates.)"            },
    { "xdg/raise-to-top",
     cfun_raise_to_top, "(xdg/raise-to-top window-table &opt output)\n\nRaise the window to the top (z-order) on the "
 "`output', or all outputs, if unset."                },
    {            nullptr, nullptr,                                    nullptr }
  };

  janet_cfuns(env, "barock", xdg_fns);

  // Hooks
  janet_def(env, "xdg-new-window-hook", janet_wrap_array(janet_array(0)), "Event list");

  auto &compositor = singleton_t<compositor_t>::get();
  compositor.registry_.xdg_shell->events.on_toplevel_new.connect(&dispatch_xdg_window_new);
}
