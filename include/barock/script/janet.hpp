#pragma once

#include <functional>
#include <janet.h>
#include <string>
#include <vector>

namespace barock {
  template<typename>
  class janet_module_t;

  struct compositor_t;
  struct janet_interop_t {
    JanetTable   *env;
    compositor_t *compositor;
  };

  class janet_module_loader_t {
    public:
    static void
    register_module(std::function<void(JanetTable *)> fn) {
      get_modules().push_back(std::move(fn));
    }

    static std::vector<std::function<void(JanetTable *)>> &
    get_modules() {
      // Guaranteed static initialization before main execution.
      static std::vector<std::function<void(JanetTable *)>> modules;
      return modules;
    }

    static void
    run_all_imports(JanetTable *env) {
      for (const auto &register_fn : get_modules()) {
        register_fn(env);
      }
    }
  };

  struct janet_registrar {
    janet_registrar(std::function<void(JanetTable *)> fn) {
      janet_module_loader_t::register_module(std::move(fn));
    }
  };

  template<typename Derived>
  class janet_autoload_t {
    private:
    static void
    call_import(JanetTable *env) {
      Derived::import(env);
    }

    static janet_registrar s_registrar;

    public:
    janet_autoload_t() = default;
  };

  template<typename Derived>
  janet_registrar janet_autoload_t<Derived>::s_registrar(janet_autoload_t<Derived>::call_import);

  template<typename _Ty>
  struct janet_converter_t {
    Janet
    operator()(const _Ty &ty);
  };

  template<typename... Args>
  int
  dispatch_hook(JanetTable *env, const std::string &symbol, Args &&...args) {
    Janet list;
    janet_resolve(env, janet_csymbol(symbol.c_str()), &list);

    auto arg_list = janet_array(sizeof...(args));
    int  i        = 0;
    ((arg_list->data[i++] = janet_converter_t<Args>{}(args)), ...);

    JanetArray *callbacks = janet_unwrap_array(list);
    for (int32_t i = 0; i < callbacks->count; ++i) {
      if (janet_type(callbacks->data[i]) != JANET_FUNCTION) {
        // TODO: Trace this case.
        continue;
      }

      JanetFunction *cb    = janet_unwrap_function(callbacks->data[i]);
      JanetFiber    *fiber = janet_fiber(cb, sizeof...(args), sizeof...(args), arg_list->data);
      Janet          value;
      janet_continue(fiber, janet_wrap_nil(), &value);
    }

    return callbacks->count;
  }

#define JANET_MODULE(type_name)                                                                    \
  template<>                                                                                       \
  class janet_module_t<type_name> : public janet_autoload_t<janet_module_t<type_name>> {           \
    public:                                                                                        \
    static void                                                                                    \
    import(JanetTable *env);                                                                       \
  };                                                                                               \
  template janet_registrar janet_autoload_t<janet_module_t<type_name>>::s_registrar
}
