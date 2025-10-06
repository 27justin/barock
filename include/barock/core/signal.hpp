#pragma once

#include <functional>
#include <map>

// -----------------
//  signal_t<T>
// This header defines a simple signal system to attach events to objects.
// -----------------

namespace barock {
  using signal_token_t = int;

  template<typename... Args>
  struct signal_t {
    private:
    std::map<signal_token_t, std::function<void(Args...)>> listeners;

    public:
    signal_t() {};
    signal_t(signal_t &&other)
      : listeners(std::move(other.listeners)) {};
    // Copying signals is not allowed, this would break assumptions on
    // the listeners.
    signal_t(const signal_t &) = delete;

    signal_token_t
    connect(std::function<void(Args...)> cb) {
      signal_token_t tok = 0;
      if (listeners.size() > 0)
        tok = listeners.rbegin()->first;
      listeners.insert(std::pair<int, decltype(cb)>(tok, cb));
      return tok;
    }

    void
    disconnect(signal_token_t token) {
      listeners.erase(token);
    }

    void
    emit(Args... args) {
      for (auto const &[_, cb] : listeners) {
        cb(args...);
      }
    }

    void
    operator=(signal_t &&other) {
      this->listeners = std::move(other.listeners);
    }

    void
    operator=(const signal_t &) = delete; // Same as the copy-ctor
  };

  // Specialization for void (empty signals with no arguments)
  template<>
  struct signal_t<void> {
    private:
    using _Listener = std::function<void()>;
    std::map<signal_token_t, _Listener> listeners;

    public:
    signal_t() = default;
    signal_t(signal_t &&other) noexcept
      : listeners(std::move(other.listeners)) {}
    signal_t &
    operator=(signal_t &&other) noexcept {
      listeners = std::move(other.listeners);
      return *this;
    }

    signal_t(const signal_t &) = delete;
    signal_t &
    operator=(const signal_t &) = delete;

    signal_token_t
    connect(_Listener cb) {
      signal_token_t tok = 0;
      if (!listeners.empty())
        tok = listeners.rbegin()->first + 1;
      listeners.emplace(tok, std::move(cb));
      return tok;
    }

    void
    disconnect(signal_token_t token) {
      listeners.erase(token);
    }

    void
    emit() {
      for (const auto &[_, cb] : listeners)
        cb();
    }
  };

}
