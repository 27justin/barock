#include "barock/fbo.hpp"
#include <GLES2/gl2.h>

using namespace barock;

fbo_t::fbo_t()
  : handle(0)
  , texture(0)
  , width(-1)
  , height(-1)
  , format(0) {}

fbo_t::~fbo_t() {
  if (texture != 0)
    glDeleteTextures(1, &texture);
  if (handle != 0)
    glDeleteFramebuffers(1, &handle);
}

fbo_t::fbo_t(int32_t width, int32_t height, GLenum format)
  : width(width)
  , height(height)
  , format(format) {
  glGenFramebuffers(1, &handle);

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);

  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, nullptr);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glBindFramebuffer(GL_FRAMEBUFFER, handle);

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

  if (auto err = glCheckFramebufferStatus(GL_FRAMEBUFFER); err != GL_FRAMEBUFFER_COMPLETE) {
    throw invalid_fbo_t{};
  }

  // Unbind texture & framebuffer
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

fbo_t::fbo_t(barock::fbo_t &&other)
  : handle(other.handle)
  , texture(other.texture)
  , width(other.width)
  , height(other.height)
  , format(other.format) {
  other.handle  = 0;
  other.texture = 0;
}

void
fbo_t::bind() {
  if (!valid())
    throw invalid_fbo_t{};

  glBindFramebuffer(GL_FRAMEBUFFER, handle);
}

bool
fbo_t::valid() const {
  return handle != 0;
}

fbo_t::
operator bool() const {
  return valid();
}

fbo_t &
fbo_t::operator=(fbo_t &&other) {
  // Deinitialize and free texture & framebuffer, if they are set.
  if (valid())
    this->~fbo_t();

  handle  = other.handle;
  texture = other.texture;
  width   = other.width;
  height  = other.height;
  format  = other.format;

  other.handle  = 0;
  other.texture = 0;

  return *this;
}
