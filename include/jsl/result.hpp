#pragma once

#include <jsl/int_types.hpp>
#include <jsl/move.hpp>
#include <jsl/type_traits.hpp>

namespace jsl {
  template<typename, typename>
  struct result_t;

  namespace __detail {
    struct tag_t {};
  }

  template<typename _Ok, typename _E>
  struct result_t {
    private:
    public:
    result_t(result_t<_Ok, _E> &&) = delete;

    result_t(const result_t<_Ok, _E> &other) {
      is_ok_ = other.is_ok_;
      if (is_ok_) {
        new (&value_.ok) _Ok(jsl::move(other.value_.ok));
      } else {
        new (&value_.err) _E(jsl::move(other.value_.err));
      }
    }

    ~result_t() {
      if (is_ok_)
        value_.ok.~_Ok();
      else
        value_.err.~_E();
    }

    template<typename _POk>
    static result_t<_Ok, _E>
    ok(_POk &&value) {
      return result_t<_Ok, _E>{ jsl::forward<_POk>(value), __detail::tag_t{} };
    }

    template<typename _PE>
    static result_t<_Ok, _E>
    err(_PE &&error) {
      return result_t<_Ok, _E>{ __detail::tag_t{}, jsl::forward<_PE>(error) };
    }

    bool
    valid() const {
      return is_ok_;
    }

    operator bool() const { return valid(); }

    bool
    operator==(const result_t<_Ok, _E> &other) const {
      if (is_ok_ != other.is_ok_) {
        return false; // one is Ok, one is Err
      }

      if (is_ok_) {
        return value_.ok == other.value_.ok;   // both Ok
      } else {
        return value_.err == other.value_.err; // both Err
      }
    }

    template<typename _PT>
    bool
    operator==(const _PT &value) const {
      if constexpr (jsl::is_same<_PT, _Ok>::value) {
        if (!is_ok_)
          return false;

        return value_.ok == value;
      } else if constexpr (jsl::is_same<_PT, _E>::value) {
        if (is_ok_)
          return false;

        return value_.err == value;
      } else {
        static_assert(false, "Incompatible type for comparison with result_t");
      }
    }

    template<typename _POk>
    result_t<_Ok, _E> &
    emplace(_POk &&ok) {
      if (is_ok_)
        value_.ok.~_Ok();
      else
        value_.err.~_E();

      new (&value_.ok) _Ok(jsl::forward<_POk>(ok));
      is_ok_ = true;
      return *this;
    }

    template<typename _PE>
    result_t<_Ok, _E> &
    emplace_err(_PE &&error) {
      if (is_ok_)
        value_.ok.~_Ok();
      else
        value_.err.~_E();

      new (&value_.err) _E(jsl::forward<_PE>(error));
      is_ok_ = false;
      return *this;
    }

    // ======================
    //   Functional Helpers
    // ======================
    template<typename _Predicate>
    result_t<typename invoke_result<_Predicate, _Ok>::type, _E>
    map(_Predicate &&pred) const {
      if (is_ok_) {
        return result_t<_Ok, _E>{ pred(value_.ok), __detail::tag_t{} };
      } else {
        return result_t<_Ok, _E>{ __detail::tag_t{}, value_.err };
      }
    }

    template<typename _Predicate>
    result_t<_Ok, typename invoke_result<_Predicate, _E>::type>
    map_err(_Predicate &&pred) const {
      if (!is_ok_) {
        return result_t<_Ok, _E>{ __detail::tag_t{}, pred(value_.err) };
      } else {
        return result_t<_Ok, _E>{ value_.ok, __detail::tag_t{} };
      }
    }

    template<typename _Predicate>
    typename invoke_result<_Predicate, _Ok>::type
    and_then(_Predicate &&pred) const {
      using _Return = typename invoke_result<_Predicate, _Ok>::type;
      if (is_ok_) {
        return pred(value_.ok);
      } else {
        return _Return{ __detail::tag_t{}, value_.err };
      }
    }

    template<typename _Predicate>
    result_t<typename invoke_result<_Predicate, _E>::type, _E>
    or_else(_Predicate &&pred) const {
      using _Return = result_t<typename invoke_result<_Predicate, _E>::type, _E>;
      if (!is_ok_) {
        return _Return{ pred(value_.err), __detail::tag_t{} };
      } else {
        return _Return{ value_.ok, __detail::tag_t{} };
      }
    }

    _Ok &
    value() {
      return value_.ok;
    }

    const _Ok &
    value() const {
      return value_.ok;
    }

    _E &
    error() {
      return value_.err;
    }

    const _E &
    error() const {
      return value_.err;
    }

    template<typename _POk>
    _Ok
    value_or(_POk &&init) {
      if (is_ok_)
        return value_.ok;
      return _Ok(jsl::forward<_POk>(init));
    }

    template<typename _Predicate>
    _Ok
    value_or_else(_Predicate &&pred) {
      if (is_ok_)
        return value_.ok;
      return _Ok(pred());
    }

    template<typename _Predicate>
    const result_t<_Ok, _E> &
    inspect(_Predicate &&pred) const {
      if (is_ok_)
        pred(value_.ok);
      return *this;
    }

    template<typename _Predicate>
    result_t<_Ok, _E> &
    inspect(_Predicate &&pred) {
      if (is_ok_)
        pred(value_.ok);
      return *this;
    }

    template<typename _Predicate>
    const result_t<_Ok, _E> &
    inspect_err(_Predicate &&pred) const {
      if (!is_ok_)
        pred(value_.err);
      return *this;
    }

    template<typename _Predicate>
    result_t<_Ok, _E> &
    inspect_err(_Predicate &&pred) {
      if (!is_ok_)
        pred(value_.err);
      return *this;
    }

    private:
    template<typename _POk>
    explicit result_t(_POk &&ok, __detail::tag_t) {
      is_ok_ = true;
      new (&value_.ok) _Ok(jsl::forward<_POk>(ok));
    }

    template<typename _PE>
    explicit result_t(__detail::tag_t, _PE &&ok) {
      is_ok_ = false;
      new (&value_.err) _E(jsl::forward<_PE>(ok));
    }

    explicit result_t()
      : is_ok_(false) {}

    struct storage_t {
      union {
        _Ok ok;
        _E  err;
      };

      // Handled by result_t itself
      storage_t() {}
      ~storage_t() {}
    } value_;
    bool is_ok_;

    template<typename, typename>
    friend struct result_t;
  };

  template<typename _Ok, typename _E>
  using expected_t = result_t<_Ok, _E>;
}
