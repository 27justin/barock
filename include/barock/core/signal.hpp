#pragma once

#include <functional>
#include <map>

// -----------------
//  signal_t<T>
// This header defines a simple signal system to attach events to objects.
// -----------------

namespace barock {
  using signal_token_t = int;

  template<typename _Event>
  struct signal_t {
    private:
    std::map<signal_token_t, std::function<void(_Event)>> listeners;

    public:
    signal_t() {};
    signal_t(signal_t &&other)
      : listeners(std::move(other.listeners)) {};
    // Copying signals is not allowed, this would break assumptions on
    // the listeners.
    signal_t(const signal_t &) = delete;

    signal_token_t
    connect(std::function<void(const _Event &)> cb) {
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
    emit(_Event ev) {
      for (auto const &[_, cb] : listeners) {
        cb(ev);
      }
    }

    void
    operator=(signal_t &&other) {
      this->listeners = std::move(other.listeners);
    }

    void
    operator=(const signal_t &) = delete; // Same as the copy-ctor
  };
}
