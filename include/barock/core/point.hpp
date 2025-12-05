#pragma once

namespace barock {

  template<typename _Ty>
  struct point_t {
    using type = _Ty;
    _Ty x, y;

    template<typename _Conversion>
    _Conversion
    to() {
      return _Conversion{ static_cast<_Conversion::type>(x), static_cast<_Conversion::type>(y) };
    };
  };

  using fpoint_t = point_t<float>;
  using dpoint_t = point_t<double>;
  using ipoint_t = point_t<int>;
}
