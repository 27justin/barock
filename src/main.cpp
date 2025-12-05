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

bool
matches_config(const jsl::optional_t<const config_output_t &> &opt, const drm::mode_t &drm_mode) {
  // If we have no configuration for the output, return whether or not
  // the mode is preferred
  if (!opt.valid())
    return drm_mode.preferred;

  // Skip modes that do not match our dimensions
  if (opt->width != drm_mode.width() || opt->height != drm_mode.height())
    return false;

  // If we have a configured refresh rate, check that they are the
  // same.
  if (opt->refresh_rate.valid() && opt->refresh_rate != drm_mode.refresh_rate())
    return false;

  return true;
}

drm::mode_t
choose_mode(const jsl::optional_t<const config_output_t &> &preferred_mode,
            const std::vector<drm::mode_t>                 &modes) {
  if (modes.size() == 0)
    throw std::runtime_error("No modes");

  drm::mode_t const *selected = nullptr;

  // Find the mode that matches our config
  auto it = std::find_if(modes.begin(), modes.end(), [&preferred_mode](const auto &mode) {
    return matches_config(preferred_mode, mode);
  });

  if (it != modes.end())
    selected = &*it;

  if (it == modes.end()) {
    CRITICAL("Couldn't find a matching mode in a list of {} modes.", modes.size());
    std::exit(1);
  }

  return *selected;
}

#include <fstream>

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

  // compositor.hotkey->add({
  //   { XKB_KEY_Shift_L, XKB_KEY_C },
  //   { XKB_VMOD_NAME_ALT },
  //   [&] {
  //     if (auto pointer_surface = compositor.pointer.focus.lock(); pointer_surface) {
  //       barock::shared_t<barock::resource_t<barock::surface_t>> surface = pointer_surface;
  //       if (!surface->has_role() ||
  //           (surface->has_role() && surface->role->type_id() != barock::xdg_surface_t::id())) {
  //         surface = pointer_surface->find_parent([](auto &surface) {
  //           return surface->role && surface->role->type_id() == barock::xdg_surface_t::id();
  //         });
  //       }

  //       // Get its position
  //       auto position = surface->position();
  //       auto size     = surface->full_extent();

  //       double center_x = position.x + size.w / 2.;
  //       double center_y = position.y + size.h / 2.;

  //       compositor.x = center_x - (1280. / compositor.zoom) / 2.;
  //       compositor.y = center_y - (800. / compositor.zoom) / 2.;
  //     }
  //    }
  // });

  // compositor.hotkey->add({
  //   { MOUSE_PRESSED | MOUSE_HOTKEY_MASK, BTN_LEFT },
  //   { XKB_VMOD_NAME_ALT },
  //   [&]() {
  //     compositor.move_global_workspace = true;
  //    }
  // });

  // compositor.hotkey->add({
  //   { MOUSE_RELEASED | MOUSE_HOTKEY_MASK, BTN_LEFT },
  //   { XKB_VMOD_NAME_ALT },
  //   [&]() {
  //     compositor.move_global_workspace = false;
  //    }
  // });

  // compositor.hotkey->add({ { XKB_KEY_Return }, { XKB_VMOD_NAME_ALT }, [&]() {
  //                           run_command("foot");
  //                         } });

  // compositor.hotkey->add({ { XKB_KEY_Escape }, {}, [] {
  //                           std::exit(0);
  //                         } });

  // compositor.hotkey->add({ { XKB_KEY_r }, { XKB_VMOD_NAME_ALT }, [&] {
  //                           compositor.zoom = 1.0;
  //                         } });

  // auto do_scroll = [](barock::compositor_t &compositor, double zoom_delta) {
  //   double old_zoom = compositor.zoom;
  //   double new_zoom = std::max(old_zoom + zoom_delta, 0.025);

  //   // Current cursor position in workspace space
  //   double cursor_ws_x = compositor.cursor.x;
  //   double cursor_ws_y = compositor.cursor.y;

  //   // Screen space position of cursor before zoom
  //   double cursor_screen_x = (cursor_ws_x - compositor.x) * old_zoom;
  //   double cursor_screen_y = (cursor_ws_y - compositor.y) * old_zoom;

  //   compositor.zoom = new_zoom;

  //   // Adjust camera so that the cursor stays at same screen
  //   // position
  //   compositor.x = cursor_ws_x - cursor_screen_x / new_zoom;
  //   compositor.y = cursor_ws_y - cursor_screen_y / new_zoom;

  //   return;
  // };

  // compositor.hotkey->add({ { MOUSE_HOTKEY_MASK | MWHEEL_DOWN }, { XKB_VMOD_NAME_ALT }, [&] {
  //                           do_scroll(compositor, -0.02);
  //                         } });

  // compositor.hotkey->add({ { MOUSE_HOTKEY_MASK | MWHEEL_UP }, { XKB_VMOD_NAME_ALT }, [&] {
  //                           do_scroll(compositor, 0.02);
  //                         } });

  // compositor.input->on_keyboard_input.connect([&](const barock::keyboard_event_t &key) {
  //   uint32_t scancode  = libinput_event_keyboard_get_key(key.keyboard);
  //   uint32_t key_state = libinput_event_keyboard_get_key_state(key.keyboard);

  //   xkb_state_update_key(compositor.keyboard.xkb.state,
  //                        scancode + 8, // +8: evdev -> xkb
  //                        key_state == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);

  //   xkb_keysym_t sym = xkb_state_key_get_one_sym(compositor.keyboard.xkb.state, scancode + 8);
  //   if (key_state == LIBINPUT_KEY_STATE_PRESSED && compositor.hotkey->feed(sym)) {
  //     return;
  //   }

  //   if (auto surface = compositor.keyboard.focus.lock(); surface) {
  //     xkb_mod_mask_t depressed =
  //       xkb_state_serialize_mods(compositor.keyboard.xkb.state, XKB_STATE_MODS_DEPRESSED);
  //     xkb_mod_mask_t latched =
  //       xkb_state_serialize_mods(compositor.keyboard.xkb.state, XKB_STATE_MODS_LATCHED);
  //     xkb_mod_mask_t locked =
  //       xkb_state_serialize_mods(compositor.keyboard.xkb.state, XKB_STATE_MODS_LOCKED);
  //     xkb_layout_index_t group =
  //       xkb_state_serialize_layout(compositor.keyboard.xkb.state, XKB_STATE_LAYOUT_EFFECTIVE);

  //     // Send the modifiers to the focused client
  //     compositor.keyboard.send_modifiers(surface, depressed, latched, locked, group);
  //     compositor.keyboard.send_key(surface, scancode, key_state);
  //     return;
  //   }
  // });

  // compositor.input->on_mouse_button.connect([&](const auto &btn) {
  //   auto &cursor = compositor.cursor;

  //   auto mouse_designator = btn.state == barock::mouse_button_t::pressed
  //                           ? MOUSE_PRESSED | MOUSE_HOTKEY_MASK
  //                           : MOUSE_RELEASED | MOUSE_HOTKEY_MASK;
  //   compositor.hotkey->feed(mouse_designator);
  //   if (compositor.hotkey->feed(btn.button)) {
  //     return;
  //   }

  //   if (auto pointer_surface = compositor.pointer.focus.lock(); pointer_surface) {
  //     compositor.pointer.send_button(pointer_surface, btn.button, btn.state);

  //     // Our pointer may be focused on a subsurface, in which case we
  //     // first have to determine the actual `xdg_surface` to activate.
  //     barock::shared_t<barock::resource_t<barock::surface_t>> surface = pointer_surface;
  //     if (!surface->has_role() ||
  //         (surface->has_role() && surface->role->type_id() != barock::xdg_surface_t::id())) {
  //       surface = pointer_surface->find_parent([](auto &surface) {
  //         return surface->role && surface->role->type_id() == barock::xdg_surface_t::id();
  //       });
  //     }

  //     // On left mouse button, we try to activate the window, and move
  //     // the keyboard focus to `surface`
  //     if (btn.button == BTN_LEFT) {
  //       if (auto active_surface = compositor.window.activated.lock()) {
  //         // Active surface differs from pointer focus, move active
  //         // state to our `surface`
  //         if (active_surface != surface) {
  //           // First deactivate whatever is currently activated.
  //           compositor.window.deactivate(active_surface);

  //           // Then activate the new window
  //           compositor.window.activate(surface);
  //         }
  //       } else {
  //         // We have no active window, immediately activate
  //         compositor.window.activate(pointer_surface);
  //       }

  //       // Now also move the keyboard focus, if it was somewhere else
  //       // before.
  //       if (compositor.keyboard.focus != pointer_surface) {
  //         compositor.keyboard.set_focus(pointer_surface);
  //       }
  //     }
  //     return;
  //   } else {
  //     // Click event without pointer focus means we clicked outside of
  //     // any window, thus we clear keyboard focus, and remove the active state.
  //     if (auto surface = compositor.window.activated.lock()) {
  //       compositor.window.deactivate(surface);
  //     }

  //     // Clear the keyboard focus.
  //     compositor.keyboard.set_focus(nullptr);
  //   }
  // });

  // compositor.input->on_mouse_scroll.connect([&compositor](auto ev) {
  //   if (ev.vertical > 0.0) {
  //     if (compositor.hotkey->feed(MOUSE_HOTKEY_MASK | MWHEEL_DOWN))
  //       return;
  //   } else if (ev.vertical < 0.0) {
  //     if (compositor.hotkey->feed(MOUSE_HOTKEY_MASK | MWHEEL_UP))
  //       return;
  //   }

  //   if (auto surface = compositor.pointer.focus.lock(); surface) {
  //     compositor.pointer.send_axis(surface, 0, ev.vertical);
  //     compositor.pointer.send_axis(surface, 1, ev.horizontal);
  //   }
  // });

  // std::thread([&] {
  //   for (;;) {
  //     // compositor.input->poll(-1);
  //   }
  // }).detach();

  wl_display    *display  = compositor.display();
  wl_event_loop *loop     = wl_display_get_event_loop(display);
  static int     throttle = 0;

  while (1) {
    wl_event_loop_dispatch(loop, -1); // 0 = non-blocking, -1 = blocking
    for (auto &screen : compositor.outputs) {
      screen->renderer().bind();
      screen->renderer().clear(0.08f, 0.08f, 0.15f, 1.f);

      for (auto &[_, signal] : screen->on_repaint) {
        signal.emit(*screen);
      }

      screen->renderer().commit();
    }
  }

  return 1;
}
