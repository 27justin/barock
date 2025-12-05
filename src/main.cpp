#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cassert>
#include <iostream>
#include <libudev.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <memory>
#include <optional>
#include <signal.h>
#include <sys/ioctl.h>
#include <xf86drmMode.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon-names.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <fcntl.h>
#include <gbm.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <numeric>
#include <spawn.h>
#include <thread>
#include <unistd.h>

#include "barock/compositor.hpp"
#include "barock/core/config.hpp"
#include "barock/core/cursor_manager.hpp"
#include "barock/core/input.hpp"
#include "barock/core/output.hpp"
#include "barock/core/region.hpp"
#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/core/wl_seat.hpp"
#include "barock/fbo.hpp"
#include "barock/hotkey.hpp"
#include "barock/render/opengl.hpp"
#include "barock/resource.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "barock/singleton.hpp"
#include "barock/util.hpp"
#include "log.hpp"

#include <janet.h>
#include <jsl/optional.hpp>

#include <wayland-server-core.h>
#include <wayland-util.h>

#include "drm/minidrm.hpp"

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

GLint quad_program, fbo_program;

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

  glDeleteShader(vs);
  glDeleteShader(fs);
  return program;
}

GLuint
init_fbo_program() {
  static const char *vs = R"(
        precision mediump float;

        attribute vec2 a_position;
        attribute vec2 a_texcoord;
        varying vec2 uv;

        uniform float u_zoom;
        uniform vec2 u_screen_size;
        uniform vec2 u_surface_size;
        uniform vec2 u_surface_position;
        uniform bool u_flip_y;

        void main() {
          uv = vec2(a_texcoord.x, 1.0 - a_texcoord.y);
          vec2 ws = (u_surface_position + a_position * u_surface_size) * u_zoom;
          vec2 ndc = ws / u_screen_size * 2.0 - 1.0;

          ndc.y = -ndc.y; // flip y-axis

          gl_Position = vec4(ndc, 0.0, 1.0);
        }
    )";

  static const char *fs = R"(
precision mediump float;

varying vec2 uv;
uniform sampler2D u_texture;

uniform float u_corner_radius;
uniform vec2 u_surface_size;
uniform vec2 u_surface_position;
uniform float u_zoom;

void main() {
    float alpha = 1.0;

    vec4 color = texture2D(u_texture, uv);
    gl_FragColor = vec4(color.rgb, color.a * alpha);
}
)";

  return create_program(vs, fs);
}

GLuint
init_quad_program() {
  static const char *vs = R"(
        precision mediump float;

        attribute vec2 a_position;
        attribute vec2 a_texcoord;
        varying vec2 uv;

        uniform vec2 u_screen_size;
        uniform vec2 u_surface_size;
        uniform vec2 u_surface_position;
        uniform bool u_flip_y;

        void main() {
          uv = a_texcoord;
          vec2 ws = (u_surface_position + a_position * u_surface_size);
          vec2 ndc = ws / u_screen_size * 2.0 - 1.0;

          if(u_flip_y)
            ndc.y = -ndc.y; // flip y-axis

          gl_Position = vec4(ndc, 0.0, 1.0);
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

  return create_program(vs, fs);
}

// void
// draw_quad(GLuint program, GLuint texture) {
//   static const GLfloat vertices[] = { // X  Y  U  V
//                                       0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f,
//                                       0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f
//   };

//   GLuint attr_pos = glGetAttribLocation(program, "a_position");
//   GL_CHECK;

//   GLuint attr_tex = glGetAttribLocation(program, "a_texcoord");
//   GL_CHECK;

//   GLuint u_tex = glGetUniformLocation(program, "u_texture");
//   GL_CHECK;

//   glActiveTexture(GL_TEXTURE0);
//   GL_CHECK;

//   glBindTexture(GL_TEXTURE_2D, texture);
//   GL_CHECK;
//   glUniform1i(u_tex, 0); // texture unit 0
//   GL_CHECK;

//   glEnableVertexAttribArray(attr_pos);
//   GL_CHECK;
//   glEnableVertexAttribArray(attr_tex);
//   GL_CHECK;

//   glVertexAttribPointer(attr_pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), &vertices[0]);
//   GL_CHECK;
//   glVertexAttribPointer(attr_tex, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), &vertices[2]);
//   GL_CHECK;

//   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
//   GL_CHECK;

//   glDisableVertexAttribArray(attr_pos);
//   GL_CHECK;
//   glDisableVertexAttribArray(attr_tex);
//   GL_CHECK;
// }

// GLuint
// upload_texture(barock::shm_buffer_t &buffer) {
//   GLuint texture;
//   glGenTextures(1, &texture);
//   GL_CHECK;

//   glBindTexture(GL_TEXTURE_2D, texture);
//   GL_CHECK;

//   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
//   GL_CHECK;

//   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
//   GL_CHECK;

//   glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, buffer.stride / 4);
//   GL_CHECK;

//   glTexImage2D(GL_TEXTURE_2D,
//                0,
//                GL_RGBA,
//                buffer.width,
//                buffer.height,
//                0,
//                GL_BGRA_EXT,
//                GL_UNSIGNED_BYTE,
//                buffer.data());
//   GL_CHECK;

//   return texture;
// }

// struct render_options {
//   float zoom = 0.0;
// };

// void
// draw(barock::compositor_t                                   &compositor,
//      barock::shared_t<barock::resource_t<barock::surface_t>> surface,
//      const barock::region_t                                 &screen_region,
//      int32_t                                                 parent_x,
//      int32_t                                                 parent_y,
//      std::optional<render_options>                           override = std::nullopt) {
//   // We only render surfaces that have a role attached.
//   int32_t x = parent_x, y = parent_y, width = 0, height = 0;
//   GLuint  texture = 0;

//   if (surface->state.buffer) {
//     auto &shm = surface->state.buffer;
//     width     = shm->width;
//     height    = shm->height;
//   }

//   auto position = surface->position();
//   position.w    = width;
//   position.h    = height;

//   // Window is improperly configured, likely that the client hasn't
//   // attached a buffer yet.
//   if (width <= 0 || height <= 0) {
//     // WARN("surface has no width, or height, rendering just the subsurfaces");
//     goto render_subsurfaces;
//   }

//   if (surface->state.buffer) {
//     glUseProgram(quad_program);
//     GL_CHECK;

//     texture = upload_texture(*surface->state.buffer);
//     GL_CHECK;

//     auto zoom = override.and_then([](auto opt) { return std::optional<float>(opt.zoom); })
//                   .value_or(compositor.zoom);

//     glUniform2f(glGetUniformLocation(quad_program, "u_screen_size"),
//                 static_cast<float>(screen_region.w),
//                 static_cast<float>(screen_region.h));
//     GL_CHECK;

//     glUniform2f(glGetUniformLocation(quad_program, "u_surface_size"), width, height);
//     GL_CHECK;

//     glUniform2f(glGetUniformLocation(quad_program, "u_surface_position"), x, y);
//     GL_CHECK;

//     glUniform1i(glGetUniformLocation(quad_program, "u_flip_y"), true);

//     draw_quad(quad_program, texture);

//     compositor.schedule_frame_done(surface, barock::current_time_msec());
//     glDeleteTextures(1, &texture);
//     GL_CHECK;
//   }

// render_subsurfaces:
//   // Similar to the normal drawing loop, to have proper z-indices, we
//   // render from back to front (0 being top-most surface)
//   for (auto it = surface->state.children.rbegin(); it != surface->state.children.rend(); ++it) {
//     auto &subsurface = *it;
//     auto  surf       = subsurface->surface.lock();

//     int32_t child_x = x + surf->x;
//     int32_t child_y = y + surf->y;

//     draw(compositor, surf, screen_region, child_x, child_y);
//   }
// }

// Draw a XDG surface into the FBO of the surface
// void
// draw(barock::compositor_t   &compositor,
//      barock::xdg_surface_t  &xdg_surface,
//      const barock::region_t &screen_size) {

//   auto &fbo = xdg_surface.framebuffer;
//   if (fbo.width != xdg_surface.width || fbo.height != xdg_surface.height) {
//     fbo = barock::fbo_t(xdg_surface.width, xdg_surface.height, GL_RGBA);
//   }

//   fbo.bind();
//   barock::region_t window_region = { .x = 0, .y = 0, .w = fbo.width, .h = fbo.height };

//   // Set the viewport to be the exact window size.
//   glViewport(0, 0, fbo.width, fbo.height);

//   // Clear the FBO to be transparent.
//   glClearColor(0., 0., 0., 0.);
//   GL_CHECK;

//   glClear(GL_COLOR_BUFFER_BIT);
//   GL_CHECK;

//   int32_t x{}, y{};
//   // Draw our surfaces to the FBO
//   if (auto surface = xdg_surface.surface.lock(); surface) {
//     auto position = surface->position();
//     x             = position.x;
//     y             = position.y;

//     draw(compositor, surface, window_region, -xdg_surface.x, -xdg_surface.y);
//   }

//   // Use the FBO to blit onto our screen

//   glUseProgram(fbo_program);
//   GL_CHECK;

//   glUniform1f(glGetUniformLocation(fbo_program, "u_zoom"), compositor.zoom);
//   GL_CHECK;

//   glUniform2f(glGetUniformLocation(fbo_program, "u_screen_size"),
//               static_cast<float>(screen_size.w),
//               static_cast<float>(screen_size.h));
//   GL_CHECK;

//   glUniform2f(
//     glGetUniformLocation(fbo_program, "u_surface_size"), xdg_surface.width, xdg_surface.height);
//   GL_CHECK;

//   // Turn the workspace coordinates into screen space coordinates.
//   float screen_x = (x - compositor.x);
//   float screen_y = (y - compositor.y);

//   glUniform2f(glGetUniformLocation(fbo_program, "u_surface_position"), screen_x, screen_y);
//   GL_CHECK;

//   // Unbind the framebuffer, we now want to draw to our actual swapchain.
//   glBindFramebuffer(GL_FRAMEBUFFER, 0);

//   // Set the viewport to be the exact screen size.
//   glViewport(0, 0, screen_size.w, screen_size.h);

//   draw_quad(fbo_program, fbo.texture);
//   GL_CHECK;
// }

// void
// draw(barock::compositor_t                                      &compositor,
//      std::vector<std::unique_ptr<minidrm::framebuffer::egl_t>> &monitors,
//      GLint                                                      quad_program) {
//   GLuint texture = 0;
//   for (auto &screen : monitors) {
//     barock::region_t screen_region{ .x = static_cast<int32_t>(compositor.x),
//                                     .y = static_cast<int32_t>(compositor.y),
//                                     .w = static_cast<int32_t>(screen->mode.width()),
//                                     .h = static_cast<int32_t>(screen->mode.height()) };

//     barock::region_t visible_region{ static_cast<int32_t>(screen_region.x / compositor.zoom),
//                                      static_cast<int32_t>(screen_region.y / compositor.zoom),
//                                      static_cast<int32_t>(screen_region.w / compositor.zoom),
//                                      static_cast<int32_t>(screen_region.h / compositor.zoom) };

//     // Check if our current region isn't stale, i.e. no window changed, then we can skip
//     rendering

//     // Grab the back buffer
//     auto front = screen->acquire();

//     // Enable blending
//     glEnable(GL_BLEND);
//     glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

//     // Set our viewport
//     glViewport(0, 0, screen->mode.width(), screen->mode.height());

//     // Clear the buffer with our color
//     //
//     // TODO: We currently only do "dumb" damage tracking, re-rendering
//     // everything inside a damage region, this includes re-rendering
//     // our background and generally having to clear the buffer,
//     // unfortunately.
//     glClearColor(0.08f, 0.08f, 0.10f, 1.f);
//     glClear(GL_COLOR_BUFFER_BIT);

//     // NOTE: The z-index of the xdg_surfaces
//     // is implicitly designated by array index, 0 is the top-most, and
//     // N the bottom-most.
//     //
//     // Therefore to render properly, we have to iterate from the back,
//     // such that index 0 is the last draw call (on top of all others.)
//     for (auto it = compositor.xdg_shell->windows.rbegin();
//          it != compositor.xdg_shell->windows.rend();
//          ++it) {
//       auto xdg_surface = *it;

//       if (auto surface = (*it)->surface.lock()) {
//         barock::region_t local_space{};
//         local_space.x = surface->x + xdg_surface->x;
//         local_space.y = surface->y + xdg_surface->y;
//         local_space.w = xdg_surface->width;
//         local_space.h = xdg_surface->height;

//         // Cull windows outside our panning radius.
//         if (!visible_region.intersects(local_space)) {
//           WARN("{} was culled as it is not visible.",
//                xdg_surface->get_role<barock::xdg_toplevel_t>()->data.app_id);
//           continue;
//         }

//         if (xdg_surface->width <= 0 || xdg_surface->height <= 0)
//           continue;

//         draw(compositor, *xdg_surface, screen_region);
//       }
//     }

//     // Draw cursor
//     if (auto pointer = compositor.cursor.surface.lock(); pointer) {
//       double screen_cursor_x = (compositor.cursor.x - compositor.x) * compositor.zoom;
//       double screen_cursor_y = (compositor.cursor.y - compositor.y) * compositor.zoom;

//       draw(compositor,
//            pointer,
//            screen_region,
//            screen_cursor_x - compositor.cursor.hotspot.x,
//            screen_cursor_y - compositor.cursor.hotspot.y,
//            render_options{ .zoom = 1.0 });
//     } else {
//       glEnable(GL_SCISSOR_TEST);
//       glScissor(
//         (int)compositor.cursor.x, (int)screen->mode.height() - (compositor.cursor.y + 16), 16,
//         16);
//       glClearColor(0.0, 1., 0., 1.);
//       glClear(GL_COLOR_BUFFER_BIT);
//       glDisable(GL_SCISSOR_TEST);
//       GL_CHECK;
//     }

//     screen->present(front);
//     GL_CHECK;
//   }
// }

using namespace minidrm;
using namespace barock;

int
main() {
  if (!getenv("XDG_SEAT")) {
    ERROR("No XDG_SEAT environment variable set. Exitting.");
    return 1;
  }

  auto card = drm::cards()[0];
  auto hdl  = card.open();

  auto compositor = compositor_t(hdl, getenv("XDG_SEAT"));
  compositor.load_file("config.janet");

  // Perform the mode set, after this we can initialize EGL
  compositor.output->mode_set();
  initialize_egl();

  wl_display    *display  = compositor.display();
  wl_event_loop *loop     = wl_display_get_event_loop(display);
  static int     throttle = 0;

  while (1) {
    wl_event_loop_dispatch(loop, -1); // 0 = non-blocking, -1 = blocking
    for (auto &screen : compositor.output->outputs()) {
      screen->renderer().bind();
      screen->renderer().clear(0.08f, 0.08f, 0.15f, 1.f);

      for (auto &[_, signal] : screen->events.on_repaint) {
        signal.emit(*screen);
      }

      screen->renderer().commit();
    }

    wl_display_flush_clients(compositor.display());
  }

  return 1;
}
