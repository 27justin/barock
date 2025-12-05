#pragma once

#include <janet.h>

namespace barock {
  struct compositor_t;
  struct janet_interop_t {
    JanetTable   *env;
    compositor_t *compositor;
  };
}
