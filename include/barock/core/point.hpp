#pragma once

#include <cfloat>
#include <cstdlib>
#include <type_traits>

namespace barock {

  template<typename _Ty>
    requires std::is_scalar_v<_Ty>
  struct point_t {
    using type = _Ty;
    _Ty x, y;

    template<typename _Conversion>
    point_t<_Conversion>
    to() {
      return point_t<_Conversion>{ static_cast<_Conversion>(x), static_cast<_Conversion>(y) };
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

    template<typename _PTy>
    bool
    operator>(const point_t<_PTy> &other) const {
      return x > other.x && y > other.y;
    }

    template<typename _PTy>
    bool
    operator>=(const point_t<_PTy> &other) const {
      return x >= other.x && y >= other.y;
    }

    template<typename _PTy>
    bool
    operator<(const point_t<_PTy> &other) const {
      return x < other.x && y < other.y;
    }

    template<typename _PTy>
    bool
    operator<=(const point_t<_PTy> &other) const {
      return x <= other.x && y <= other.y;
    }

    template<typename _PTy>
    bool
    operator==(const point_t<_PTy> &other) const {
      if constexpr (std::is_floating_point_v<_PTy> || std::is_floating_point_v<_Ty>) {
        // Equality through `FLT_EPSILON`
        return (std::abs(x - other.x) <= FLT_EPSILON && std::abs(y - other.y));
      } else {
        // Equality through `==`
        return x == other.x && y == other.y;
      }
    }

    template<typename _PTy>
    bool
    operator!=(const point_t<_PTy> &other) const {
      return !(*this == other);
    }

    point_t<_Ty>
    operator*(_Ty scalar) const {
      return point_t<_Ty>{ x * scalar, y * scalar };
    }

    point_t<_Ty>
    operator/(_Ty scalar) const {
      return point_t<_Ty>{ x / scalar, y / scalar };
    }
  };

  using fpoint_t = point_t<float>;
  using dpoint_t = point_t<double>;
  using ipoint_t = point_t<int>;
}
