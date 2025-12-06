#pragma once

#include "barock/core/renderer.hpp"
#include "minidrm.hpp"
#include <GLES2/gl2.h>
#include <string_view>

namespace barock {

  class gl_shader_t {
    private:
    GLuint handle_;

    public:
    gl_shader_t(GLuint);

    void
    bind();

    void
    uniform(const std::string &name, float) const;

    void
    uniform(const std::string &name, float, float) const;

    void
    uniform(const std::string &name, float, float, float) const;

    void
    uniform(const std::string &name, float, float, float, float) const;

    void
    uniform(const std::string &name, int) const;

    operator GLuint() const;
  };

  class gl_shader_storage_t {
    public:
    const gl_shader_t &
    by_name(const std::string &) const;
    void
    add(const std::string &, const gl_shader_t &);

    private:
    std::map<std::string, gl_shader_t> shaders_;
  };

  class gl_renderer_t : public renderer_t {
    private:
    minidrm::framebuffer::egl_t               handle_;
    minidrm::drm::mode_t                      mode_;
    minidrm::framebuffer::egl_t::egl_buffer_t frontbuffer_;

    public:
    gl_renderer_t(const minidrm::drm::mode_t &, minidrm::framebuffer::egl_t &&);
    gl_renderer_t(gl_renderer_t &&);
    gl_renderer_t(const gl_renderer_t &) = delete;

    ~gl_renderer_t();

    void
    bind() override;

    void
    commit() override;

    void
    clear(float r, float g, float b, float a);

    void
    draw(surface_t &surface, const fpoint_t &screen_position) override;

    void
    draw(_XcursorImage *pointer, const fpoint_t &screen_position) override;
  };
}
