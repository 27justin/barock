#pragma once

#include "barock/core/point.hpp"
#include "barock/script/janet.hpp"
#include <type_traits>

namespace barock {
  template<>
  struct janet_converter_t<point_t<float>> {
    /**
     * @brief Convert `fpoint_t' into a `JanetTuple'
     */
    Janet
    operator()(const point_t<float> &f) {
      auto tuple = janet_tuple_begin(2);
      tuple[0]   = janet_wrap_number(f.x);
      tuple[1]   = janet_wrap_number(f.y);
      return janet_wrap_tuple(tuple);
    }
  };

  template<>
  struct janet_converter_t<point_t<int>> {
    /**
     * @brief Convert `fpoint_t' into a `JanetTuple'
     */
    Janet
    operator()(const point_t<int> &f) {
      auto tuple = janet_tuple_begin(2);
      tuple[0]   = janet_wrap_integer(f.x);
      tuple[1]   = janet_wrap_integer(f.y);
      return janet_wrap_tuple(tuple);
    }
  };

  /**
   * @brief Retrieve a point_t<_Ty> from the argv vector.
   * If `allow_spread' is set to true, the function will allow for both:
   * `(function [x y])', and `(function x y)'
   */
  template<typename _Ty>
  point_t<_Ty>
  janet_getpoint(Janet *argv, int32_t &n, bool allow_spread) {
    point_t<_Ty> result;
    if (janet_type(argv[n]) == JANET_TUPLE) {
      JanetTuple tuple = janet_gettuple(argv, n);
      if constexpr (std::is_floating_point_v<_Ty>) {
        // double
        result.x = static_cast<_Ty>(janet_getnumber(tuple, 0));
        result.y = static_cast<_Ty>(janet_getnumber(tuple, 1));
      } else {
        // int
        result.x = static_cast<_Ty>(janet_getinteger(tuple, 0));
        result.y = static_cast<_Ty>(janet_getinteger(tuple, 1));
      }
      n += 1;
    } else {
      if constexpr (std::is_floating_point_v<_Ty>) {
        // double
        result.x = static_cast<_Ty>(janet_getnumber(argv, n++));
        result.y = static_cast<_Ty>(janet_getnumber(argv, n++));
      } else {
        // int
        result.x = static_cast<_Ty>(janet_getinteger(argv, n++));
        result.y = static_cast<_Ty>(janet_getinteger(argv, n++));
      }
      // Skip two arguments
      n += 2;
    }
    return result;
  }
}
