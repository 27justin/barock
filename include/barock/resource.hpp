#pragma once

#include "barock/core/signal.hpp"
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

  /**
   * @brief A simplified implementation of a shared smart pointer.
   *
   * Manages reference-counted ownership of dynamically allocated objects,
   * allowing multiple shared_t instances to share the same object.
   *
   * @tparam _Ty Type of the managed object.
   */
  template<typename _Ty>
  struct shared_t {
    private:
    /**
     * @brief Internal control block that keeps track of reference counts.
     *
     * Contains two atomic counters:
     * - strong: Number of shared_t instances sharing ownership.
     * - weak: Number of weak_t instances observing the object.
     *
     * Also holds a pointer to the actual managed object.
     */
    struct control_t {
      std::atomic<intmax_t> strong{ 1 }; ///< Strong reference count
      std::atomic<intmax_t> weak{ 0 };   ///< Weak reference count
      _Ty                  *data;        ///< Pointer to the managed object
    };

    control_t *control;                  ///< Pointer to the control block

    /**
     * @brief Private constructor from a control block pointer.
     *
     * Increments the strong reference count if the control block is valid.
     *
     * @param ctrl Pointer to an existing control block.
     */
    explicit shared_t(control_t *ctrl)
      : control(ctrl) {
      if (control)
        control->strong.fetch_add(1);
    }

    public:
    /**
     * @brief Constructs a shared_t from a raw pointer.
     *
     * Initializes a new control block and assumes ownership of the object.
     *
     * @param ptr Raw pointer to the object to manage.
     */
    shared_t(_Ty *ptr) { control = new control_t{ .strong = 1, .weak = 0, .data = ptr }; }

    /**
     * @brief Constructs an empty shared_t.
     *
     * This object will allocate no heap memory, and will be left in
     * an uninitialized invalid state.
     *
     */
    shared_t()
      : control(nullptr) {}

    /**
     * @brief Copy constructor.
     *
     * Increments the strong reference count.
     *
     * @param other Another shared_t instance to copy from.
     */
    shared_t(const shared_t<_Ty> &other)
      : control(other.control) {
      if (control)
        control->strong.fetch_add(1);
    }

    /**
     * @brief Move constructor.
     *
     * Transfers ownership from another shared_t instance.
     *
     * @param other Another shared_t instance to move from.
     */
    shared_t(shared_t &&other) noexcept
      : control(other.control) {
      other.control = nullptr;
    }

    /**
     * @brief Destructor.
     *
     * Decrements the strong reference count.
     * Deletes the managed object if the count reaches zero.
     * If no weak references exist, also deletes the control block.
     */
    ~shared_t() {
      if (!control)
        return;

      auto old_count = control->strong.fetch_sub(1, std::memory_order_acq_rel);

      if (old_count == 1) {
        delete control->data;
        control->data = nullptr;

        if (control->weak.load(std::memory_order_acquire) == 0) {
          delete control;
        }
      }
    }

    _Ty *
    operator->() {
      return control->data;
    }

    const _Ty *
    operator->() const {
      return control->data;
    }

    _Ty &
    operator*() {
      return *control->data;
    }

    const _Ty &
    operator*() const {
      return *control->data;
    }

    /**
     * @brief Checks whether the shared pointer owns an object.
     *
     * @return true if the control block and managed object are valid.
     */
    explicit
    operator bool() const {
      return control != nullptr && control->data != nullptr;
    }

    /**
     * @brief Equality comparison.
     *
     * Compares whether two shared_t instances share the same control block.
     *
     * @param other Another shared_t instance.
     * @return true if both share ownership of the same object.
     */
    bool
    operator==(const shared_t<_Ty> &other) {
      return control == other.control;
    }

    /**
     * @brief Copy assignment operator.
     *
     * Decrements the current strong reference count and cleans up if needed.
     * Then copies the control block from another shared_t.
     *
     * @param other The shared_t to copy from.
     * @return Reference to this shared_t.
     */
    shared_t<_Ty> &
    operator=(const shared_t<_Ty> &other) {
      if (this == &other)
        return *this;

      // Decrement existing reference
      if (control && control->strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete control->data;
        control->data = nullptr;

        if (control->weak.load(std::memory_order_acquire) == 0) {
          delete control;
        }
      }

      // Copy new reference
      control = other.control;
      if (control)
        control->strong.fetch_add(1, std::memory_order_relaxed);

      return *this;
    }

    /**
     * @brief Move assignment operator.
     *
     * Releases the current control block and transfers ownership from another shared_t.
     *
     * @param other The shared_t to move from.
     * @return Reference to this shared_t.
     */
    shared_t &
    operator=(shared_t &&other) noexcept {
      if (this != &other) {
        this->~shared_t();
        control       = other.control;
        other.control = nullptr;
      }
      return *this;
    }

    /**
     * @brief Grant weak_t<_Ty> access to private members.
     *
     * Needed for constructing weak references.
     */
    friend class weak_t<_Ty>;
  };

  /**
   * @brief A RAII object managing a wayland `wl_resource` and accompanying user data.
   *
   * @tparam _Ty Type of the managed object.
   */
  template<typename _Ty>
  struct resource_t {
    public:
    _Ty         *data_;
    wl_resource *resource_;

    signal_t<resource_t<_Ty> &>
      on_destruct; ///< Signal emitted when the entire resource_t<> is removed from memory
    signal_t<wl_resource *> on_destroy; ///< Signal emitted, when the wl_resource gets destroyed

    public:
    /**
     * @brief Create a new resource from for objects with default constructors.
     * @note You likely want to use `make_resource<_Ty>`
     */
    resource_t()
      : data_(new _Ty{})
      , resource_(nullptr) {}

    ~resource_t() {
      on_destruct.emit(*this);
      delete data_;
    }

    /**
     * @brief Create a new resource from for objects with arbitrary arguments.
     * @note You likely want to use `make_resource<_Ty>`
     */
    template<typename... Args>
    resource_t(Args &&...args)
      : data_(new _Ty(std::forward<Args>(args)...))
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

    wl_client *
    owner() const {
      return wl_resource_get_client(resource_);
    }

    bool
    operator==(const resource_t<_Ty> &other) const {
      return this->data_ == other.data_;
    }
  };

  /**
   * @brief A weak reference to an object managed by shared_t.
   *
   * Does not contribute to the strong reference count, and therefore does not
   * prevent the managed object from being deleted. Can be promoted to a shared_t
   * via the lock() method if the object is still alive.
   *
   * @tparam _Ty Type of the managed object.
   */
  template<typename _Ty>
  struct weak_t {
    private:
    protected:
    /**
     * @brief Pointer to the shared control block.
     *
     * This is shared with associated shared_t instances.
     */
    typename shared_t<_Ty>::control_t *control;

    public:
    /**
     * @brief Constructs a weak_t from a shared_t.
     *
     * Increments the weak reference count. Does not affect the strong count.
     *
     * @param parent A shared_t instance to observe.
     */
    weak_t(const shared_t<_Ty> &parent)
      : control(parent.control) {
      // Increment weak references to ensure control block is retained
      control->weak.fetch_add(1);
    }

    /**
     * @brief Deleted copy constructor.
     *
     * Disabled to prevent accidental copying of weak references.
     */
    weak_t(const weak_t &) = delete;

    /**
     * @brief Default constructor.
     *
     * Constructs an empty (null) weak_t.
     */
    explicit weak_t()
      : control(nullptr) {}

    /**
     * @brief Destructor.
     *
     * Decrements the weak reference count. If no strong or weak references remain,
     * deletes the control block.
     */
    ~weak_t() {
      if (!control)
        return;

      // Decrement weak count, check if both counts are zero
      if (control->weak.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        if (control->strong.load(std::memory_order_acquire) == 0) {
          delete control;
        }
      }
    }

    /**
     * @brief Assignment from shared_t.
     *
     * Allows re-binding this weak_t to another shared_t, adjusting reference counts.
     *
     * @param parent The shared_t instance to observe.
     * @return Reference to this weak_t.
     */
    weak_t<_Ty> &
    operator=(const shared_t<_Ty> &parent) noexcept {
      if (control == parent.control)
        return *this;

      // Release old control block
      if (control) {
        intmax_t old_weak = control->weak.fetch_sub(1);
        if (old_weak == 1 && control->strong.load() == 0) {
          delete control;
        }
      }

      // Assign new control block
      control = parent.control;
      if (control)
        control->weak.fetch_add(1);

      return *this;
    }

    /**
     * @brief Equality operator.
     *
     * Compares if this weak_t observes the same control block as the given shared_t.
     *
     * @param shared A shared_t to compare against.
     * @return true if both share the same control block.
     */
    bool
    operator==(const shared_t<_Ty> &shared) const {
      return control == shared.control;
    }

    /**
     * @brief Attempts to promote this weak_t to a shared_t.
     *
     * If the managed object still exists (i.e. strong count > 0), returns a new shared_t.
     * Otherwise, returns a null shared_t.
     *
     * @return A shared_t instance or a null shared_t if expired.
     */
    shared_t<_Ty>
    lock() {
      if (control && control->strong.load() > 0) {
        return shared_t<_Ty>(control); // Uses private constructor
      } else {
        return nullptr;
      }
    }
  };

  template<typename _Ty>
  shared_t<resource_t<_Ty>>
  from_wl_resource(wl_resource *resource) {
    if (resource == nullptr) {
      return nullptr;
    }
    return *reinterpret_cast<shared_t<resource_t<_Ty>> *>(wl_resource_get_user_data(resource));
  }

  template<typename _Ty, typename _Interface, typename... Args>
  shared_t<resource_t<_Ty>>
  make_resource(wl_client          *client,
                const wl_interface &interface,
                const _Interface   &implementation,
                uint32_t            version,
                uint32_t            id,
                Args &&...args) {
    auto *wl_resource = wl_resource_create(client, &interface, version, id);

    shared_t<resource_t<_Ty>> *resource =
      new shared_t<resource_t<_Ty>>(new resource_t<_Ty>(std::forward<Args>(args)...));
    (*resource)->resource_ = wl_resource;

    wl_resource_set_implementation(
      wl_resource, &implementation, resource, [](struct wl_resource *res) {
        delete static_cast<shared_t<resource_t<_Ty>> *>(wl_resource_get_user_data(res));
      });

    return *resource;
  }

  template<typename _Ty, typename _Interface>
  shared_t<resource_t<_Ty>>
  make_resource(wl_client          *client,
                const wl_interface &interface,
                const _Interface   &implementation,
                uint32_t            version,
                uint32_t            id) {
    auto *wl_resource = wl_resource_create(client, &interface, version, id);

    shared_t<resource_t<_Ty>> *resource = new shared_t<resource_t<_Ty>>(new resource_t<_Ty>());
    (*resource)->resource_              = wl_resource;

    wl_resource_set_implementation(
      wl_resource, &implementation, resource, [](struct wl_resource *res) {
        auto shared = static_cast<shared_t<resource_t<_Ty>> *>(wl_resource_get_user_data(res));
        (*shared)->on_destroy.emit(res);
        delete shared;
      });

    return *resource;
  }

}
