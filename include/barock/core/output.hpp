#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "barock/core/metadata.hpp"
#include "barock/core/point.hpp"
#include "barock/core/quad_tree.hpp"
#include "barock/core/signal.hpp"
#include "barock/core/surface.hpp"

#include "jsl/optional.hpp"

#include "minidrm.hpp"

namespace barock {
  class renderer_t;
  class output_manager_t;

  enum class coordinate_space_t { eWorkspace, eScreenspace };

  enum class direction_t {
    eNone  = 0,
    eNorth = 1 << 0,
    eEast  = 1 << 1,
    eSouth = 1 << 2,
    eWest  = 1 << 3,

    eNorthWest = eNorth | eWest,
    eNorthEast = eNorth | eEast,
    eSouthEast = eSouth | eEast,
    eSouthWest = eSouth | eWest,
  };

  inline direction_t
  operator|(direction_t a, direction_t b) {
    return static_cast<direction_t>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
  }

  inline direction_t &
  operator|=(direction_t &a, direction_t b) {
    a = a | b;
    return a;
  }

  inline direction_t
  operator&(direction_t a, direction_t b) {
    return static_cast<direction_t>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
  }

  inline direction_t &
  operator&=(direction_t &a, direction_t b) {
    a = a & b;
    return a;
  }

  inline direction_t
  operator~(direction_t dir) {
    return static_cast<direction_t>(~static_cast<int>(dir));
  }

  struct mode_set_allocator_t {
    uint32_t                                taken_;
    minidrm::drm::handle_t                  handle_;
    std::unordered_map<std::string, size_t> plan_;

    public:
    mode_set_allocator_t(minidrm::drm::handle_t);

    void
    adopt(const minidrm::drm::connector_t &connector);

    minidrm::framebuffer::egl_t
    mode_set(const minidrm::drm::connector_t &connector, const minidrm::drm::mode_t &mode);
  };

  struct output_t {
    private:
    friend class output_manager_t;

    // quad_tree_t damage_;
    bool dirty_;
    // mat4x4 transform;

    float x_, y_; ///< Pan
    float zoom_;  ///< Zoom

    output_t *top_, *right_, *bottom_, *left_;

    std::vector<shared_t<surface_t>> surfaces_; ///< Top-level surfaces that should be drawn.

    minidrm::drm::connector_t   connector_;
    minidrm::drm::mode_t        mode_;
    std::unique_ptr<renderer_t> renderer_; ///< DRM specific stuff is hidden into this

    public:
    static constexpr coordinate_space_t eWorkspace   = coordinate_space_t::eWorkspace;
    static constexpr coordinate_space_t eScreenspace = coordinate_space_t::eScreenspace;

    struct {
      std::map<size_t, signal_t<output_t &>> on_repaint;
    } events;

    // Generic RTTI data store
    metadata_t metadata;

    output_t(const minidrm::drm::connector_t &connector, const minidrm::drm::mode_t &mode);

    ~output_t();

    const minidrm::drm::connector_t &
    connector() const;

    const minidrm::drm::mode_t &
    mode() const;

    /**
     * @brief Convert a point from one coordinate system, into another.
     *
     * @param point The coordinates to convert
     *
     * @tparam _From The source coordinate system.
     * @tparam _To The target coordinate system. Conversion implies
     * that _From is the diametric opposite of _To, i.e. eWorkspace ->
     * eScreen and vice-versa.
     *
     *
     * Should _To and _From match, the point is returned as is.
     *
     * @code{.cpp}
     * output_t screen = ...;
     * fpoint_t screenspace = { 453.f, 1027.f };
     * fpoint_t workspace = screen.to<coordinate_system_t::eWorkspace>(screenspace);
     * @endcode
     */
    template<coordinate_space_t _From, coordinate_space_t _To>
    fpoint_t
    to(const fpoint_t &point) const;

    /**
     * @brief Get an adjacent output device, using the cardinal direction
     *
     * @param direction The cardinal direction to look up.  Supports
     * composed directions, such as eNorthEast, etc.
     */
    jsl::optional_t<output_t &>
    adjacent(direction_t direction);

    /**
     * @brief Get the renderer associated with this display.  Throws
     * an exception, if this output has no renderer associated.
     */
    renderer_t &
    renderer();

    template<typename _Backend>
    renderer_t &
    renderer(_Backend &&backend)
      requires(std::is_base_of<renderer_t, _Backend>::value)
    {
      renderer_ = std::make_unique<_Backend>(std::move(backend));
      return *renderer_;
    }
  };
}
