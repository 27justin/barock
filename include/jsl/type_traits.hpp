#pragma once

namespace jsl {

  template<typename A, typename B>
  struct is_same {
    static constexpr bool value = false;
  };

  template<typename T>
  struct is_same<T, T> {
    static constexpr bool value = true;
  };

  template<typename T>
  struct remove_reference {
    using type = T;
  };

  template<typename T>
  struct remove_reference<T &> {
    using type = T;
  };

  template<typename T>
  struct remove_reference<T &&> {
    using type = T;
  };

  template<typename T>
  using remove_reference_t = typename remove_reference<T>::type;

  template<typename T>
  constexpr T &&
  forward(remove_reference_t<T> &t) noexcept {
    return static_cast<T &&>(t);
  }

  template<typename T>
  constexpr T &&
  forward(remove_reference_t<T> &&t) noexcept {
    return static_cast<T &&>(t);
  }

  template<typename T>
  struct is_integral {
    static constexpr bool value = false;
  };

  template<>
  struct is_integral<bool> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<char> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<signed char> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<unsigned char> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<wchar_t> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<char16_t> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<char32_t> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<short> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<unsigned short> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<int> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<unsigned int> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<long> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<unsigned long> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<long long> {
    static constexpr bool value = true;
  };
  template<>
  struct is_integral<unsigned long long> {
    static constexpr bool value = true;
  };

  template<typename _Ty>
  concept is_real = is_same<_Ty, float>::value || is_same<_Ty, double>::value;

  template<typename T>
  T &&
  declval() noexcept;

  template<typename...>
  using void_t = void;

  template<typename _Functor, typename... _Args>
  auto
  invoke(_Functor &&fn, _Args &&...args)
    -> decltype(static_cast<_Functor &&>(fn)(static_cast<_Args &&>(args)...)) {
    return static_cast<_Functor &&>(fn)(static_cast<_Args &&>(args)...);
  }

  template<typename Func, typename... Args>
  struct invoke_result {
    using type = decltype(declval<Func>()(declval<Args>()...));
  };

  template<typename From, typename To>
  concept convertible_to = requires(From f) { static_cast<To>(f); };

  template<typename T>
  concept cloneable = requires(T t) { t.clone(); };

  template<bool _Cond, typename _Ty>
  struct enable_if {};

  template<typename _Ty>
  struct enable_if<true, _Ty> {
    using type = _Ty;
  };

  template<typename _Ty>
  struct is_unsigned_type {
    static constexpr bool value = false;
  };

  // clang-format off
  template<> struct is_unsigned_type<unsigned char> { static constexpr bool value = true; };
  template<> struct is_unsigned_type<unsigned short> { static constexpr bool value = true; };
  template<> struct is_unsigned_type<unsigned int> { static constexpr bool value = true; };
  template<> struct is_unsigned_type<unsigned long> { static constexpr bool value = true; };
  template<> struct is_unsigned_type<unsigned long long> { static constexpr bool value = true; };
  // clang-format on

  template<typename _Ty>
  struct unsigned_t {};

  // clang-format off
  template<> struct unsigned_t<signed char> { using type = unsigned char; };
  template<> struct unsigned_t<char> { using type = unsigned char; };
  template<> struct unsigned_t<short> { using type = unsigned short; };
  template<> struct unsigned_t<int> { using type = unsigned int; };
  template<> struct unsigned_t<long> { using type = unsigned long; };
  template<> struct unsigned_t<long long> { using type = unsigned long long; };

  template<> struct unsigned_t<unsigned char> { using type = unsigned char; };
  template<> struct unsigned_t<unsigned short> { using type = unsigned short; };
  template<> struct unsigned_t<unsigned int> { using type = unsigned int; };
  template<> struct unsigned_t<unsigned long> { using type = unsigned long; };
  template<> struct unsigned_t<unsigned long long> { using type = unsigned long long; };

  // clang-format on

  template<typename _Ty>
  struct is_signed_type {
    static constexpr bool value = false;
  };

  // clang-format off
  template<> struct is_signed_type<char> { static constexpr bool value = true; };
  template<> struct is_signed_type<short> { static constexpr bool value = true; };
  template<> struct is_signed_type<int> { static constexpr bool value = true; };
  template<> struct is_signed_type<long> { static constexpr bool value = true; };
  template<> struct is_signed_type<long long> { static constexpr bool value = true; };
  // clang-format on

  template<typename _Ty>
  concept is_unsigned = is_unsigned_type<_Ty>::value;

  template<typename _Ty>
  concept is_signed = is_signed_type<_Ty>::value;

  template<typename _Ty>
  struct limits_t {
    public:
    static constexpr _Ty
    max() {
      if constexpr (is_unsigned<_Ty>)
        return static_cast<_Ty>(~_Ty(0));
      else {
        using U = typename unsigned_t<_Ty>::type;
        return static_cast<_Ty>(U(~0) >> 1);
      }
    }

    static constexpr _Ty
    min() {
      if constexpr (is_unsigned<_Ty>)
        return 0;
      else
        return (_Ty)(_Ty(1) << ((sizeof(_Ty) * 8) - 1));
    }
  };

  template<typename>
  struct hash_t;
  template<typename T>
  concept hashable = requires(T x) {
    { hash_t<T>::hash(x) };
  };

  template<typename _Ty>
  concept is_move_constructible = requires(_Ty t) {
    { _Ty(static_cast<_Ty &&>(t)) };
  };

  template<typename _Ty>
  concept is_copy_constructible = requires(_Ty t) {
    { _Ty(static_cast<const _Ty &>(t)) };
  };

  template<typename _Ty>
  concept is_trivially_copyable = __is_trivially_copyable(_Ty);

  constexpr bool
  is_constant_evaluated() {
    return __builtin_is_constant_evaluated();
  }

  template<typename _Ty>
  concept comparable = requires(_Ty l) {
    l < l;
    l > l;
    l == l;
  };

  template<typename _Ty>
  concept xorable = requires(_Ty l) { l ^ l; };

  template<typename _Ty>
  concept bitshiftable = requires(_Ty l) { l << l; };

  template<typename _Ty>
  concept bitwise_or = requires(_Ty l) { l | l; };

  template<typename _Ty>
  concept bitwise_and = requires(_Ty l) { l & l; };

  template<typename _From, typename _To>
  concept is_convertible = requires(_From f, _To t) { f.operator _To(); };

}
