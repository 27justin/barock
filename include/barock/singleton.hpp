#include <utility>
#pragma

namespace barock {

  template<typename _Ty>
  class singleton_t {
    static _Ty *instance;

    public:
    singleton_t()  = delete;
    ~singleton_t() = default;

    template<typename... Args>
    static _Ty &
    ensure(Args &&...args) {
      if (instance == nullptr) {
        instance = new _Ty(std::forward<Args>(args)...);
      }
      return *instance;
    }

    static _Ty &
    get() {
      return *instance;
    }

    static bool
    valid() {
      return instance != nullptr;
    }
  };

  template<typename _Ty>
  _Ty *singleton_t<_Ty>::instance = nullptr;
}
