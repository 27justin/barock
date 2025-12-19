#include "barock/render/opengl.hpp"
#include "../log.hpp"
#include "barock/core/renderer.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/singleton.hpp"
#include "barock/util.hpp"
#include "wl/wayland-protocol.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdexcept>

extern "C" {
#include <X11/Xcursor/Xcursor.h>
}

#define GL_CHECK                                                                                   \
  do {                                                                                             \
    GLenum err = glGetError();                                                                     \
    if (err != GL_NO_ERROR) {                                                                      \
      ERROR("OpenGL Error ({}:{}): {}", __FILE__, __LINE__, err);                                  \
      printf("\n");                                                                                \
      fflush(stdout);                                                                              \
      throw std::runtime_error{ "OpenGL Error" };                                                  \
    }                                                                                              \
  } while (0)

using namespace barock;

static GLuint
compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(shader, 512, nullptr, log);
    ERROR("Shader compile error ({}):\n{}", glGetError(), log);
    std::exit(1);
  }

  return shader;
}

static gl_shader_t
create_program(const char *vs_src, const char *fs_src) {
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);

  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetProgramInfoLog(program, 512, nullptr, log);
    ERROR("Program link error: {}", log);
    std::exit(1);
  }

  glDeleteShader(vs);
  glDeleteShader(fs);
  return gl_shader_t{ program };
}

static void
initialize_egl() {
  static bool init = false;
  if (init == true)
    return;

  auto &storage = singleton_t<gl_shader_storage_t>::ensure();

  static const char *vs = R"(
        precision mediump float;

        attribute vec2 a_position;
        attribute vec2 a_texcoord;
        varying vec2 uv;

        uniform vec2 u_screen_size;
        uniform vec2 u_surface_size;
        uniform vec2 u_surface_position;

        vec2 to_ndc(vec2 screenspace) {
          return (screenspace / u_screen_size * 2.0 - 1.0)
            // Flip Y position
            * vec2(1, -1);
        }

        void main() {
          uv = a_texcoord;
          gl_Position = vec4(to_ndc(u_surface_position + a_position * u_surface_size), 0.0, 1.0);
        }
    )";

  static const char *fs = R"(
precision mediump float;

varying vec2 uv;
uniform sampler2D u_texture;

void main() {
    vec4 color = texture2D(u_texture, uv);
    gl_FragColor = color;
}
)";

  storage.add("quad shader", create_program(vs, fs));

  init = true;
}

const gl_shader_t &
gl_shader_storage_t::by_name(const std::string &name) const {
  if (!shaders_.contains(name))
    throw std::runtime_error{ "No such shader" };
  return shaders_.at(name);
}

void
gl_shader_storage_t::add(const std::string &name, const gl_shader_t &shader) {
  shaders_.emplace(name, shader);
}

gl_shader_t::gl_shader_t(GLuint shader)
  : handle_(shader) {}

void
gl_shader_t::bind() {
  glUseProgram(handle_);
}

void
gl_shader_t::uniform(const std::string &name, float v) const {
  glUniform1f(glGetUniformLocation(handle_, name.c_str()), v);
  GL_CHECK;
}

void
gl_shader_t::uniform(const std::string &name, float v0, float v1) const {
  glUniform2f(glGetUniformLocation(handle_, name.c_str()), v0, v1);
  GL_CHECK;
}

void
gl_shader_t::uniform(const std::string &name, float v0, float v1, float v2) const {
  glUniform3f(glGetUniformLocation(handle_, name.c_str()), v0, v1, v2);
  GL_CHECK;
}

void
gl_shader_t::uniform(const std::string &name, float v0, float v1, float v2, float v3) const {
  glUniform4f(glGetUniformLocation(handle_, name.c_str()), v0, v1, v2, v3);
  GL_CHECK;
}

gl_shader_t::
operator GLuint() const {
  return handle_;
}

gl_renderer_t::gl_renderer_t(const minidrm::drm::mode_t &mode, minidrm::framebuffer::egl_t &&egl)
  : mode_(mode)
  , handle_(std::move(egl)) {
  initialize_egl();
}

gl_renderer_t::gl_renderer_t(gl_renderer_t &&other)
  : mode_(other.mode_)
  , handle_(std::move(other.handle_)) {}

gl_renderer_t::~gl_renderer_t() {}

void
gl_renderer_t::bind() {
  frontbuffer_ = handle_.acquire();
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  GL_CHECK;

  glViewport(0, 0, mode_.width(), mode_.height());
  GL_CHECK;
}

void
gl_renderer_t::commit() {
  handle_.present(frontbuffer_);
}

void
gl_renderer_t::clear(float r, float g, float b, float a) {
  glClearColor(r, g, b, a);
  glClear(GL_COLOR_BUFFER_BIT);
  GL_CHECK;
}

GLuint
upload_texture(barock::shm_buffer_t &buffer) {
  GLuint texture;
  glGenTextures(1, &texture);
  GL_CHECK;

  glBindTexture(GL_TEXTURE_2D, texture);
  GL_CHECK;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  GL_CHECK;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GL_CHECK;

  glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, buffer.stride / 4);
  GL_CHECK;

  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA,
               buffer.width,
               buffer.height,
               0,
               GL_BGRA_EXT,
               GL_UNSIGNED_BYTE,
               buffer.data());
  GL_CHECK;

  glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
  GL_CHECK;

  return texture;
}

GLuint
upload_texture(int width, int height, void *data) {
  GLuint texture;
  glGenTextures(1, &texture);
  GL_CHECK;

  glBindTexture(GL_TEXTURE_2D, texture);
  GL_CHECK;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  GL_CHECK;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  GL_CHECK;

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, data);
  GL_CHECK;

  return texture;
}

void
quad(const gl_shader_t &shader, GLuint texture) {
  static const GLfloat vertices[] = { // X,  Y,   U,  V
                                      0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
                                      0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
  };

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  GLuint attr_pos = glGetAttribLocation(shader, "a_position");
  GLuint attr_tex = glGetAttribLocation(shader, "a_texcoord");
  GLuint u_tex    = glGetUniformLocation(shader, "u_texture");

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glUniform1i(u_tex, 0);

  glEnableVertexAttribArray(attr_pos);
  glEnableVertexAttribArray(attr_tex);

  // CPU pointer source, valid because VBO=0
  glVertexAttribPointer(
    attr_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *)(&vertices[0]));
  glVertexAttribPointer(
    attr_tex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (const void *)(&vertices[2]));

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glDisableVertexAttribArray(attr_pos);
  glDisableVertexAttribArray(attr_tex);
}

void
gl_renderer_t::draw(surface_t &surface, const fpoint_t &screen_position) {
  glViewport(0, 0, mode_.width(), mode_.height());
  if (surface.state.buffer) {
    GLuint texture = upload_texture(*surface.state.buffer);
    GL_CHECK;

    auto quad_shader = singleton_t<gl_shader_storage_t>::get().by_name("quad shader");
    quad_shader.bind();
    GL_CHECK;

    quad_shader.uniform("u_surface_position", screen_position.x, screen_position.y);
    quad_shader.uniform("u_surface_size", surface.extent().x, surface.extent().y);
    quad_shader.uniform("u_screen_size", mode_.width(), mode_.height());

    quad(quad_shader, texture);

    glDeleteTextures(1, &texture);
    GL_CHECK;

    // Send the frame callback, and release the buffer back to the
    // client.
    if (surface.state.pending) {
      wl_callback_send_done(surface.state.pending, current_time_msec());
      wl_resource_destroy(surface.state.pending);
      wl_buffer_send_release(surface.state.buffer->resource());
      surface.state.pending = nullptr;
    }
  }

  for (auto &subsurface_dao : surface.state.children) {
    if (auto subsurface = subsurface_dao->surface.lock(); subsurface) {
      draw(*subsurface,
           { screen_position.x + subsurface_dao->position.x,
             screen_position.y + subsurface_dao->position.y });
    }
  }
}

void
gl_renderer_t::draw(_XcursorImage *cursor, const fpoint_t &screen_position) {
  assert(cursor != nullptr);
  GLuint texture = upload_texture(cursor->width, cursor->height, cursor->pixels);

  auto quad_shader = singleton_t<gl_shader_storage_t>::get().by_name("quad shader");
  quad_shader.bind();
  GL_CHECK;

  quad_shader.uniform(
    "u_surface_position", screen_position.x - cursor->xhot, screen_position.y - cursor->yhot);
  quad_shader.uniform("u_surface_position", screen_position.x, screen_position.y);
  quad_shader.uniform("u_surface_size", cursor->width, cursor->height);
  quad_shader.uniform("u_screen_size", mode_.width(), mode_.height());

  quad(quad_shader, texture);
  GL_CHECK;

  glDeleteTextures(1, &texture);
  GL_CHECK;
}
