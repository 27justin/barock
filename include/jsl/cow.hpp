#pragma once

#include <jsl/type_traits.hpp>

namespace jsl {

  template<typename _Ty>
  struct cow_t {
    struct proxy_t {
      cow_t<_Ty> *cow;

      operator const _Ty &() const & { return cow->ref(); }

      // Access through non-const lvalue yields mutable clone
      operator _Ty &() & { return cow->mut(); }

      // Access through lvalue or rvalue yields const-ref
      const _Ty &
      operator*() const & {
        return cow->ref();
      }

      // Access through lvalue may yields mutable clone
      _Ty &
      operator*() & {
        return cow->mut();
      }

      // Access through lvalues yields a const reference
      const _Ty *
      operator->() const & {
        return &cow->ref();
      }

      // Access through prvalues yields a mutable clone
      _Ty *
      operator->() && {
        return &cow->mut();
      }

      template<typename _PTy>
      proxy_t &
      operator=(_PTy &&value) {
        cow->mut() = jsl::forward<_PTy>(value);
        return *this;
      }
    };

    cow_t(const _Ty &value) {
      as.ref = &value;
      owned_ = false;
    }

    cow_t(_Ty &&value) {
      new (&as.owned) _Ty(jsl::forward<_Ty>(value));
      owned_ = true;
    }

    ~cow_t() {
      // Delete the value, if we own it.
      if (owned_) {
        as.owned.~_Ty();
      }
    }

    proxy_t
    operator*() {
      return proxy_t{ this };
    }

    const _Ty &
    operator*() const {
      return ref();
    }

    proxy_t
    operator->() {
      return proxy_t{ this };
    }
    const proxy_t
    operator->() const {
      return proxy_t{ const_cast<cow_t *>(this) };
    }

    bool
    operator==(const _Ty &other) const {
      return owned_ ? as.owned == other : *as.ref == other;
    }

    bool
    operator<(const _Ty &other) const
      requires jsl::comparable<_Ty>
    {
      return owned_ ? as.owned < other : *as.ref < other;
    }

    bool
    operator>(const _Ty &other) const
      requires jsl::comparable<_Ty>
    {
      return owned_ ? as.owned > other : *as.ref > other;
    }

    _Ty
    operator^(const _Ty &other) const
      requires jsl::xorable<_Ty>
    {
      return owned_ ? as.owned ^ other : *as.ref ^ other;
    }

    _Ty
    operator<<(const _Ty &other) const
      requires jsl::bitshiftable<_Ty>
    {
      return owned_ ? as.owned << other : *as.ref << other;
    }

    _Ty
    operator|(const _Ty &other) const
      requires jsl::bitwise_or<_Ty>
    {
      return owned_ ? as.owned | other : *as.ref | other;
    }

    _Ty
    operator&(const _Ty &other) const
      requires jsl::bitwise_or<_Ty>
    {
      return owned_ ? as.owned & other : *as.ref & other;
    }

    const _Ty &
    ref() const {
      return owned_ ? as.owned : *as.ref;
    }

    _Ty &
    mut() {
      if (!owned_) {
        new (&as.owned) _Ty(*as.ref);
        owned_ = true;
      }
      return as.owned;
    }

    private:
    alignas(_Ty) struct _as {
      union {
        const _Ty *ref;
        _Ty        owned;
      };
      _as() {}
      ~_as() {}
    } as;
    bool owned_;
    friend struct proxy_t;
  };

}
