#pragma once

#include <GLES2/gl2.h>
namespace barock {

  struct invalid_fbo_t {};

  struct fbo_t {
    GLuint  handle, texture;
    int32_t width, height;
    GLenum  format;

    fbo_t();
    fbo_t(int32_t width, int32_t height, GLenum format);
    fbo_t(const fbo_t &) = delete;
    fbo_t(fbo_t &&);
    ~fbo_t();

    void
    bind();

    ///< Return whether the framebuffer is in a valid state, and can be bound.
    bool
    valid() const;

    operator bool() const;

    fbo_t &
    operator=(fbo_t &&);

    fbo_t &
    operator=(const fbo_t &) = delete;
  };

};
