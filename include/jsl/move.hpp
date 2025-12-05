#pragma once

#include <jsl/type_traits.hpp>

namespace jsl {

  template<class _Ty>
  constexpr typename jsl::remove_reference<_Ty>::type &&
  move(_Ty &&value) noexcept {
    return static_cast<typename jsl::remove_reference<_Ty>::type &&>(value);
  }

}
