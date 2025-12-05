#pragma once

#include <jsl/int_types.hpp>
#include <jsl/move.hpp>
#include <jsl/type_traits.hpp>
#include <stdlib.h>
#include <string.h>

#include <cstdio>

#pragma GCC diagnostic push
// We need this because the GCC analyzer incorrectly infers that some
// parts of our code may be uninitialized, which they can't be, since
// we have runtime initialization checks.
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"

namespace jsl {
  template<typename _Ty>
  struct optional_t;

  struct nullopt_t {};

  static constexpr nullopt_t nullopt = nullopt_t{};

  template<typename _Derived, typename _Ty>
  struct basic_optional_t {
    public:
    _Ty &
    value() {
      return static_cast<_Derived *>(this)->value();
    };
    const _Ty &
    value() const {
      return static_cast<_Derived *>(const_cast<basic_optional_t<_Derived, _Ty> *>(this))->value();
    }

    _Ty &
    operator*() {
      return value();
    }

    const _Ty &
    operator*() const {
      return value();
    }

    jsl::remove_reference_t<_Ty> *
    operator->() {
      return &value();
    }

    const jsl::remove_reference_t<_Ty> *
    operator->() const {
      return &value();
    }

    bool
    valid() const {
      return is_nil_ == false;
    }

    operator bool() const { return this->valid(); }

    bool
    operator==(const basic_optional_t<_Derived, _Ty> &other) const {
      // If both options are uninitialized, we are equal.
      if (other.is_nil_ && this->is_nil_)
        return true;

      // If only one of us is initialized, we are not equal.
      if (this->is_nil_ || other.is_nil_)
        return false;

      // Since we both are initialized, now we can hand the equality
      // operation down to our `_Ty`
      return value() == other.value();
    }

    bool
    operator==(const jsl::remove_reference_t<_Ty> &other) const {
      if (is_nil_)
        return false;
      return value() == other;
    }

    template<typename _PTy>
    _Ty
    value_or(_PTy &&init) {
      if (valid()) {
        return value();
      }
      return jsl::forward<_PTy>(init);
    }

    template<typename _Predicate>
    _Ty
    value_or_else(_Predicate &&functor) {
      if (valid()) {
        return value();
      }
      return jsl::forward<_Ty>(functor());
    }

    template<typename _Predicate>
    optional_t<typename invoke_result<_Predicate, _Ty>::type>
    map(_Predicate &&pred) const {
      using _Return = typename invoke_result<_Predicate, _Ty>::type;
      if (valid()) {
        return optional_t<_Return>{ pred(value()) };
      }
      return optional_t<_Return>{};
    }

    template<typename _Predicate>
    optional_t<typename invoke_result<_Predicate, _Ty>::type>
    map_or(_Ty init, _Predicate &&pred) const {
      using _Return = typename invoke_result<_Predicate, _Ty>::type;
      if (valid()) {
        return optional_t<_Return>{ pred(value()) };
      } else {
        return optional_t<_Return>{ pred(init) };
      }
    }

    template<typename _Predicate>
    optional_t<_Ty>
    or_else(_Predicate &&pred) const {
      if (!valid()) {
        return optional_t<_Ty>{ pred() };
      }
      return *this;
    }

    template<typename _Predicate>
    optional_t<_Ty> &
    apply(_Predicate &&pred) {
      if (valid()) {
        pred(value());
      }
      return *static_cast<_Derived *>(this);
    }

    template<typename _Predicate>
    optional_t<_Ty>
    and_then(_Predicate &&pred) const {
      if (valid()) {
        return optional_t<_Ty>{ pred(value()) };
      }
      return optional_t<_Ty>{};
    }

    template<typename _Predicate>
    optional_t<_Ty>
    filter(_Predicate &&pred) const {
      if (valid() && pred(value())) {
        return *this;
      }
      return optional_t<_Ty>{};
    }

    template<typename _Predicate>
    const optional_t<_Ty> &
    inspect(_Predicate &&pred) const {
      if (valid()) {
        pred(value());
      }
      return *static_cast<const _Derived *>(this);
    }

    protected:
    explicit basic_optional_t(bool is_nil)
      : is_nil_(is_nil) {}
    bool is_nil_;
  };

  template<typename _Ty>
  struct optional_t : basic_optional_t<optional_t<_Ty>, _Ty> {
    // Nil constructor
    optional_t()
      : basic_optional_t<optional_t<_Ty>, _Ty>(true) {};

    optional_t(const nullopt_t &)
      : basic_optional_t<optional_t<_Ty>, _Ty>(true) {};

    template<typename _PTy>
    optional_t(_PTy &&value)
      : basic_optional_t<optional_t<_Ty>, _Ty>(false) {
      new (value_) _Ty(jsl::forward<_PTy>(value));
    }

    optional_t(const optional_t<_Ty> &other)
      : basic_optional_t<optional_t<_Ty>, _Ty>(other.is_nil_) {

      // Use placement new to invoke the copy constructor in-place
      if (!this->is_nil_) {
        new (value_) _Ty(other.value());
      }
    }

    optional_t(optional_t<_Ty> &&other)
      : basic_optional_t<optional_t<_Ty>, _Ty>(other.is_nil_) {
      if (!this->is_nil_) {
        new (value_) _Ty(jsl::move(other.value()));
      }
      other.is_nil_ = true;
      memset(other.value_, 0, sizeof(_Ty));
    }

    ~optional_t() {
      if (!this->is_nil_)
        value().~_Ty();
    }

    _Ty &
    value() {
      return *reinterpret_cast<_Ty *>(value_);
    }

    const _Ty &
    value() const {
      return *reinterpret_cast<const _Ty *>(value_);
    }

    template<typename _PTy>
    optional_t<_Ty> &
    emplace(_PTy &&value) {
      if (!this->is_nil_) {
        this->value().~_Ty();
      }

      new (value_) _Ty(jsl::forward<_PTy>(value));
      this->is_nil_ = false;
      return *this;
    }

    optional_t<_Ty> &
    invalidate() {
      if (!this->is_nil_) {
        value().~_Ty();
      }
      this->is_nil_ = true;
      return *this;
    }

    optional_t<_Ty> &
    operator=(optional_t<_Ty> &&other) {
      // Guard against same object assignment
      if (this == &other) {
        return *this;
      }

      emplace(jsl::move(other.value()));
      return *this;
    }

    template<typename _PTy>
    optional_t<_Ty> &
    operator=(_PTy &&value) {
      if (!this->valid()) {
        new (value_) _Ty(jsl::forward<_PTy>(value));
        this->is_nil_ = false;
      } else {
        this->value() = value;
      }
      return *this;
    }

    optional_t<_Ty> &
    operator=(const optional_t<_Ty> &) = delete;

    private:
    alignas(_Ty) char value_[sizeof(_Ty)];
    friend class basic_optional_t<optional_t<_Ty>, _Ty>;
    friend class basic_optional_t<optional_t<_Ty &>, _Ty &>;
  };

  template<typename _Ty>
  struct optional_t<_Ty &> : basic_optional_t<optional_t<_Ty &>, _Ty &> {
    // Nil constructor
    optional_t()
      : basic_optional_t<optional_t<_Ty &>, _Ty &>(true) {};

    optional_t(const nullopt_t &)
      : basic_optional_t<optional_t<_Ty &>, _Ty &>(true) {};

    optional_t(_Ty &value)
      : basic_optional_t<optional_t<_Ty &>, _Ty &>(false) {
      reference_ = &value;
    }

    optional_t(optional_t<_Ty> &other)
      : basic_optional_t<optional_t<_Ty &>, _Ty &>(!other.valid())
      , reference_(other.valid() ? &other.value() : nullptr) {}

    optional_t(const optional_t<_Ty &> &other)
      : basic_optional_t<optional_t<_Ty &>, _Ty &>(other.is_nil_)
      , reference_(nullptr) {
      reference_ = other.reference_;
    }

    optional_t(optional_t<_Ty &> &&other)
      : basic_optional_t<optional_t<_Ty &>, _Ty &>(other.is_nil_)
      , reference_(other.valid() ? &other.value() : nullptr) {
      other.is_nil_    = true;
      other.reference_ = nullptr;
    }

    ~optional_t() {
      // Reference specialization, we do not free references.
    }

    _Ty &
    value() {
      return *reference_;
    }

    const _Ty &
    value() const {
      return *reference_;
    }

    template<typename _PTy>
    optional_t<_Ty &> &
    emplace(_PTy &value) {
      reference_    = &value;
      this->is_nil_ = false;
      return *this;
    }

    optional_t<_Ty &> &
    invalidate() {
      this->is_nil_    = true;
      this->reference_ = nullptr;
      return *this;
    }

    bool
    operator==(const _Ty &other) const {
      if (!this->valid())
        return false;
      return value() == other;
    }

    template<typename _PTy>
    optional_t<_Ty &> &
    operator=(_PTy &&value) {
      this->value() = value;
      return *this;
    }

    optional_t<_Ty &> &
    operator=(const optional_t<_Ty &> &) = delete;

    private:
    _Ty *reference_;
    friend class basic_optional_t<optional_t<_Ty>, _Ty>;
  };

  template<typename _Ty>
  using option_t = optional_t<_Ty>;
}
