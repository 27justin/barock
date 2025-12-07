#pragma once

#include <algorithm>
#include <cfloat>
namespace barock {
  using ease_function_t = float (*)(float);

  template<typename _Ty>
  class animation_t {
    float           progress, duration;
    _Ty             start, end;
    ease_function_t ease;

    public:
    animation_t(_Ty start, _Ty end, float duration, ease_function_t easing)
      : progress(0.f)
      , duration(duration)
      , start(start)
      , end(end)
      , ease(easing) {}

    _Ty
    sample() const {
      float t = std::clamp(progress / duration, 0.f, 1.f);
      return start + (end - start) * ease(t);
    }

    void
    update(float dt /*ms*/) {
      if (progress < duration)
        progress += dt;
    }

    animation_t<_Ty> &
    operator=(animation_t<_Ty> &&other) {
      start    = other.start;
      end      = other.end;
      progress = other.progress;
      duration = other.duration;
      ease     = other.ease;
      return *this;
    }

    bool
    is_done() const {
      return duration - progress <= FLT_EPSILON;
    }
  };
};
