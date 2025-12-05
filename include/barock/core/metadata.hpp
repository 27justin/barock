#pragma once

#include <functional>
#include <map>
#include <memory>
#include <type_traits>
#include <typeindex>

namespace barock {

  class metadata_t {
    using GenericDeleter = std::function<void(void *)>;

    std::map<std::type_index, std::unique_ptr<void, GenericDeleter>> data;

    public:
    template<typename _Ty, typename... _Args>
    std::remove_cvref_t<_Ty> &
    ensure(_Args &&...args) {
      if (!data.contains(typeid(std::remove_cvref_t<_Ty>))) {
        void *ud =
          reinterpret_cast<void *>(new std::remove_cvref_t<_Ty>(std::forward<_Args>(args)...));

        data.emplace(typeid(std::remove_cvref_t<_Ty>),
                     std::unique_ptr<void, GenericDeleter>{ ud, [](void *ud) {
                                                             delete reinterpret_cast<_Ty *>(ud);
                                                           } });
      }
      return *reinterpret_cast<_Ty *>(data.at(typeid(_Ty)).get());
    }

    template<typename _Ty>
    std::remove_cvref_t<_Ty> &
    get() {
      return *reinterpret_cast<_Ty *>(data.at(typeid(std::remove_cvref_t<_Ty>)).get());
    }

    template<typename _Ty>
    void
    remove() {
      data.erase(typeid(std::remove_cvref_t<_Ty>));
    }
  };

}
