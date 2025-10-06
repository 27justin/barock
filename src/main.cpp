#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <iostream>
#include <memory>

#include <cstdlib>
#include <fcntl.h>
#include <gbm.h>
#include <thread>
#include <unistd.h>

#include "barock/compositor.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "log.hpp"

#include <wayland-server-core.h>

#include "drm/minidrm.hpp"

uint32_t
current_time_msec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#define GL_CHECK                                                                                   \
  do {                                                                                             \
    GLenum err = glGetError();                                                                     \
    if (err != GL_NO_ERROR) {                                                                      \
      ERROR("OpenGL Error ({}:{}): {}", __FILE__, __LINE__, err);                                  \
      printf("\n");                                                                                \
      fflush(stdout);                                                                              \
      std::exit(0);                                                                                \
    }                                                                                              \
  } while (0)

void
check_opengl_error() {
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    ERROR("OpenGL Error: {}", error);
    std::exit(1);
  }
}

// Simple helper: compile shader
GLuint
compile_shader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(shader, 512, nullptr, log);
    std::cerr << "Shader compile error: " << log << "\n";
    std::exit(1);
  }

  return shader;
}

// Simple helper: create program from vertex + fragment shader
GLuint
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

  // glDeleteShader(vs);
  // glDeleteShader(fs);
  return program;
}

// Call once during init
GLuint
init_quad_program() {
  static const char *vs = R"(
        attribute vec2 a_position;
        attribute vec2 a_texcoord;
        varying vec2 v_texcoord;

        void main() {
            gl_Position = vec4(a_position, 0.0, 1.0);
            v_texcoord = a_texcoord;
        }
    )";

  static const char *fs = R"(
        precision mediump float;
        varying vec2 v_texcoord;
        uniform sampler2D u_texture;

        void main() {
            vec4 color = texture2D(u_texture, vec2(v_texcoord.x, 1.0 - v_texcoord.y));
            gl_FragColor = color;
        }
    )";

  return create_program(vs, fs);
}

void
draw_quad(GLuint program, GLuint texture) {
  GL_CHECK;

  glUseProgram(program);
  GL_CHECK;

  static const GLfloat vertices[] = {
    // X     Y     U     V
    -1.0f, -1.0f, 0.0f, 0.0f, // bottom-left
    1.0f,  -1.0f, 1.0f, 0.0f, // bottom-right
    -1.0f, 1.0f,  0.0f, 1.0f, // top-left
    1.0f,  1.0f,  1.0f, 1.0f  // top-right
  };

  GLuint attr_pos = glGetAttribLocation(program, "a_position");
  GL_CHECK;
  GLuint attr_tex = glGetAttribLocation(program, "a_texcoord");
  GL_CHECK;
  GLuint u_tex = glGetUniformLocation(program, "u_texture");
  GL_CHECK;

  glActiveTexture(GL_TEXTURE0);
  GL_CHECK;
  glBindTexture(GL_TEXTURE_2D, texture);
  GL_CHECK;
  glUniform1i(u_tex, 0); // texture unit 0
  GL_CHECK;

  glEnableVertexAttribArray(attr_pos);
  GL_CHECK;
  glEnableVertexAttribArray(attr_tex);
  GL_CHECK;

  glVertexAttribPointer(attr_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), &vertices[0]);
  GL_CHECK;
  glVertexAttribPointer(attr_tex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), &vertices[2]);
  GL_CHECK;

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  GL_CHECK;

  glDisableVertexAttribArray(attr_pos);
  GL_CHECK;
  glDisableVertexAttribArray(attr_tex);
  GL_CHECK;
}

using namespace minidrm;
int
main() {
  auto card = drm::cards()[0];
  auto hdl  = card.open();

  barock::compositor_t compositor(hdl);

  std::thread([&] {
    wl_display    *display  = compositor.display();
    wl_event_loop *loop     = wl_display_get_event_loop(display);
    static int     throttle = 0;

    while (1) {
      // Dispatch events (fd handlers, client requests, etc.)
      wl_event_loop_dispatch(loop, 0); // 0 = non-blocking, -1 = blocking

      // Dispatch frame callbacks
      compositor.frame_done_flush_callback(&compositor);

      // Flush any pending Wayland client events
      wl_display_flush_clients(display);
    }
    std::exit(1);
  }).detach();

  std::vector<std::unique_ptr<framebuffer::egl_t>> monitors;

  auto connectors = hdl.connectors();

  // Perform a mode set
  uint32_t taken_crtcs = 0;
  for (auto const &con : connectors) {
    if (con.connection() != DRM_MODE_CONNECTED)
      continue;
    std::cout << "Connector " << con.type() << "\n";

    auto mode = con.modes()[0];
    for (auto i = 0; i < con->count_encoders; ++i) {
      drmModeEncoder *enc = drmModeGetEncoder(hdl.fd, con->encoders[i]);
      if (!enc)
        continue;

      auto crtcs = hdl.crtcs();
      for (int i = 0; i < crtcs.size(); ++i) {
        uint32_t bit = 1 << i;
        // Not compatible
        if ((enc->possible_crtcs & bit) == 0)
          continue;

        // Already taken
        if (taken_crtcs & bit)
          continue;

        drmModeFreeEncoder(enc);
        taken_crtcs |= bit;

        auto &crtc = crtcs[i];
        INFO("{}\n  {}x{} @ {} Hz", con.type(), mode.width(), mode.height(), mode.refresh_rate());
        INFO("Creating EGL framebuffer on {}", (void *)&hdl);
        auto &ref = monitors.emplace_back(new framebuffer::egl_t(hdl, con, crtc, mode, 2));
        ref->mode_set();
        break;
      }
    }
  }

  std::thread([] {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::exit(0);
  }).detach();

  auto   quad_program = init_quad_program();
  GLuint texture      = 0;
  for (;;) {
    for (auto &screen : monitors) {
      auto front = screen->acquire();

      glViewport(0, 0, screen->mode.width(), screen->mode.height());
      glClearColor(0.08f, 0.08f, 0.10f, 1.f);
      glClear(GL_COLOR_BUFFER_BIT);

      // Iterate all surfaces...
      for (auto const surface : compositor.wl_compositor->surfaces) {
        if (!surface->buffer)
          continue;
        if (!surface->role)
          continue;

        if (surface->role->type_id() == barock::xdg_toplevel_t::id()) {
          auto &role = reinterpret_cast<barock::xdg_toplevel_t *>(surface->role)->data;
          glViewport(role.x, role.y, role.width, role.height);
        }

        // INFO("Rendering surface...");
        barock::shm_buffer_t *buffer =
          (barock::shm_buffer_t *)wl_resource_get_user_data(surface->buffer);

        if (texture != 0) {
          glDeleteTextures(1, &texture);
          GL_CHECK;
        }

        glGenTextures(1, &texture);
        GL_CHECK;

        glBindTexture(GL_TEXTURE_2D, texture);
        GL_CHECK;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        GL_CHECK;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GL_CHECK;

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, buffer->width, buffer->height, 0, GL_BGRA_EXT,
                     GL_UNSIGNED_BYTE, buffer->data());
        GL_CHECK;

        draw_quad(quad_program, texture);
        GL_CHECK;

        compositor.schedule_frame_done(surface, current_time_msec());
      }

      screen->present(front);
      GL_CHECK;
    }
  }

  return 0;
}
