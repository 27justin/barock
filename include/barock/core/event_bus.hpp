#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <typeindex>
#include <vector>

namespace barock {
  using token_t = size_t;

  class base_subscriber_list_t {
    public:
    virtual ~base_subscriber_list_t() = default;
  };

  template<typename... _Parameters>
  class subscriber_list_t {
    public:
    std::vector<std::function<void(_Parameters...)>> listeners;
  };

  class event_bus_t {
    private:
    std::map<std::type_index, std::unique_ptr<base_subscriber_list_t>> bus_;

    public:
    event_bus_t() {};
    event_bus_t(const event_bus_t &) = delete;
    event_bus_t(event_bus_t &&);

    template<typename... _Event>
      requires(sizeof...(_Event) > 0)
    token_t
    subscribe(std::function<void(_Event...)> cb) {
      if (!bus_.contains(typeid(std::tuple<_Event...>))) {
        bus_.emplace(std::make_unique<subscriber_list_t<_Event...>>(), cb);
      } else {
        bus_.emplace(std::make_unique<subscriber_list_t<_Event...>>(), cb);
      }
      return 0;
    }

    template<typename... _Parameters>
    void
    emit(_Parameters &&...params) const {
      if (bus_.contains(typeid(std::tuple<_Parameters...>))) {
        subscriber_list_t<_Parameters...> *list =
          reinterpret_cast<subscriber_list_t<_Parameters...> *>(
            bus_.at(typeid(std::tuple<_Parameters...>)).get());
        for (auto &listener : list->listeners) {
          listener(params...);
        }
      }
    }
  };
}
