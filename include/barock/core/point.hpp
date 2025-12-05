#pragma once

#include <type_traits>

namespace barock {

  template<typename _Ty>
    requires std::is_scalar_v<_Ty>
  struct point_t {
    using type = _Ty;
    _Ty x, y;

    template<typename _Conversion>
    _Conversion
    to() {
      return _Conversion{ static_cast<_Conversion::type>(x), static_cast<_Conversion::type>(y) };
    };

    template<typename _PTy>
    point_t<_Ty>
    operator+(const point_t<_PTy> &other) const {
      return point_t<_Ty>{ x + other.x, y + other.y };
    }

    template<typename _PTy>
    point_t<_Ty>
    operator-(const point_t<_PTy> &other) const {
      return point_t<_Ty>{ x - other.x, y - other.y };
    }

    template<typename _PTy>
    point_t<_Ty> &
    operator+=(const point_t<_PTy> &other) {
      x += other.x;
      y += other.y;
      return *this;
    }

    template<typename _PTy>
    point_t<_Ty> &
    operator-=(const point_t<_PTy> &other) {
      x -= other.x;
      y -= other.y;
      return *this;
    }
  };

  using fpoint_t = point_t<float>;
  using dpoint_t = point_t<double>;
  using ipoint_t = point_t<int>;
}
