#pragma once

#include "barock/core/point.hpp"
#include "barock/core/surface.hpp"

struct _XcursorImage;
namespace barock {

  class renderer_t {
    public:
    virtual ~renderer_t() = default;
    /**
     * @brief Bind the renderer to prepare rendering, this has to be
     * called before any other member function.
     */
    virtual void
    bind() = 0;

    /**
     * @brief Commit the current backbuffer to be displayed to the
     * user.
     */
    virtual void
    commit() = 0;

    virtual void
    clear(float r, float g, float b, float a) = 0;

    /**
     * @brief Draw a surface at given screen position.
     */
    virtual void
    draw(surface_t &surface, const fpoint_t &screen_position) = 0;

    virtual void
    draw(_XcursorImage *, const fpoint_t &screen_position) = 0;
  };
};
