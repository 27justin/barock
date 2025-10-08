#pragma once

#include "wl/wayland-protocol.h"
#include <atomic>
#include <wayland-server-core.h>

/**
 * \defgroup Memory management
 */

namespace barock {
  template<typename _Ty>
  struct weak_t;

  template<typename _Ty>
  struct shared_t;

  template<typename _Ty>
  struct resource_t;

  template<typename _Ty>
  struct shared_t {
    private:
    struct control_t {
      std::atomic<intmax_t> strong{ 1 }, weak{ 0 };
      _Ty                  *data;
    };

    control_t *control;

    explicit shared_t(control_t *ctrl)
      : control(ctrl) {}

    public:
    shared_t(_Ty *ptr) { control = new control_t{ .strong = 1, .weak = 0, .data = ptr }; }

    shared_t(const shared_t<_Ty> &other)
      : control(other.control) {
      control->strong.fetch_add(1);
    }

    ~shared_t() {
      if (control->strong.fetch_sub(1, std::memory_order_acq_rel) == 0) {
        delete control->data;
        if (control->weak.load(std::memory_order_acquire) == 0) {
          delete control;
        }
      }
    }

    void
    ref() {
      control->strong.fetch_add(1);
    }
    void
    unref() {
      control->strong.fetch_sub(1);
    }

    _Ty *
    operator->() {
      return control->data;
    }

    _Ty &
    operator*() {
      return *control->data;
    }

    operator bool() const { return control && control->data != nullptr; }

    bool
    operator==(const shared_t<_Ty> &other) {
      return control == other.control;
    }

    void
    operator=(const shared_t<_Ty> &other) {
      if (control) {
        if (control->strong.fetch_sub(1) == 0) {
          delete control->data;
        }
        if (control->weak.load() == 0) {
          delete control;
        }
      }

      control = other.control;
    }

    friend class weak_t<_Ty>;
  };

  template<typename _Ty>
  struct resource_t {
    public:
    _Ty         *data_;
    wl_resource *resource_;

    public:
    resource_t()
      : data_(new _Ty{})
      , resource_(nullptr) {}

    resource_t(const resource_t<_Ty> &other)
      : data_(other.data_)
      , resource_(other.resource_) {}

    _Ty *
    operator->() {
      return this->data_;
    }

    const _Ty *
    operator->() const {
      return this->data_;
    }

    wl_resource *
    resource() {
      return this->resource_;
    }

    const wl_resource *
    resource() const {
      return this->resource_;
    }

    _Ty *
    get() {
      return this->data_;
    }

    const _Ty *
    get() const {
      return this->data_;
    }

    bool
    operator==(const resource_t<_Ty> &other) const {
      return this->data_ == other.data_;
    }
  };

  template<typename _Ty>
  struct weak_t {
    private:
    protected:
    typename shared_t<_Ty>::control_t *control;

    public:
    weak_t(const shared_t<_Ty> &parent)
      : control(parent.control) {
      // Increase weak references (to prevent `strong` from being deallocated should the parent go
      // out of scope)
      control->weak.fetch_add(1);
    }

    weak_t()
      : control(nullptr) {}

    ~weak_t() {
      // Decrement our weak references, and check against strong
      // references.  If both are at 0, this object can be fully
      // freed, `data_` will be freed by the `shared_t` itself..
      control->weak.fetch_sub(1);
      if (control->weak.load() == 0 && control->strong.load() == 0) {
        delete control;
      }
    }

    shared_t<_Ty>
    lock() {
      // `strong_` may already be NULL
      if (control && control->strong.load() > 0) {
        return shared_t<_Ty>(control);
      } else {
        return nullptr;
      }
    }
  };

  template<typename _Ty, typename... Args>
  resource_t<_Ty>
  create_resource(wl_client *, const wl_interface &, uint32_t, uint32_t, Args &&...args);

  template<typename _Ty, typename _Interface>
  shared_t<resource_t<_Ty>>
  create_resource(wl_client          *client,
                  const wl_interface &interface,
                  const _Interface   &implementation,
                  uint32_t            version,
                  uint32_t            id) {
    auto *wl_resource = wl_resource_create(client, &interface, version, id);

    shared_t<resource_t<_Ty>> *resource = new shared_t<resource_t<_Ty>>(new resource_t<_Ty>());
    (*resource)->resource_              = wl_resource;

    wl_resource_set_implementation(
      wl_resource, &implementation, resource, [](struct wl_resource *res) {
        delete static_cast<shared_t<resource_t<_Ty>> *>(wl_resource_get_user_data(res));
      });

    return *resource;
  }
}

// #include "resource.tpp"
