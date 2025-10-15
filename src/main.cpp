#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <iostream>
#include <libudev.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <memory>
#include <signal.h>
#include <sys/ioctl.h>
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
#include "barock/core/shm_pool.hpp"
#include "barock/core/surface.hpp"
#include "barock/core/wl_compositor.hpp"
#include "barock/core/wl_seat.hpp"
#include "barock/input.hpp"
#include "barock/shell/xdg_toplevel.hpp"
#include "barock/shell/xdg_wm_base.hpp"
#include "barock/util.hpp"
#include "log.hpp"

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

GLint debug_program, quad_program;

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

GLuint
init_quad_program_color() {
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
        uniform vec3 u_color;

        void main() {
            gl_FragColor = vec4(u_color, 0.5);
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

void
draw_quad(GLuint program, float color[3]) {
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
  GLuint u_color = glGetUniformLocation(program, "u_color");
  GL_CHECK;

  glActiveTexture(GL_TEXTURE0);
  GL_CHECK;

  glUniform3fv(u_color, 1, color);
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

GLuint
upload_texture(barock::shm_buffer_t &buffer) {
  GLuint texture;
  glGenTextures(1, &texture);
  GL_CHECK;

  glBindTexture(GL_TEXTURE_2D, texture);
  GL_CHECK;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  GL_CHECK;

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
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

  return texture;
}

pid_t
run_command(std::string_view cmd) {
  pid_t             pid;
  posix_spawnattr_t attr;

  // Initialize spawn attributes
  if (posix_spawnattr_init(&attr) != 0) {
    perror("posix_spawnattr_init");
    return -1;
  }

  // Set the POSIX_SPAWN_SETSID flag to detach from the current session
  short flags = POSIX_SPAWN_SETSID;
  if (posix_spawnattr_setflags(&attr, flags) != 0) {
    perror("posix_spawnattr_setflags");
    posix_spawnattr_destroy(&attr);
    return -1;
  }

  const char *argv[] = { (char *)"sh", (char *)"-c", nullptr, nullptr };
  std::string cmdstr(cmd);
  argv[2]    = const_cast<char *>(cmdstr.c_str());
  int result = posix_spawnp(&pid, "sh", NULL, NULL, const_cast<char *const *>(argv), environ);

  posix_spawnattr_destroy(&attr);

  if (result != 0) {
    perror("posix_spawnp");
    return -1;
  }

  return pid;
}

void
draw_surface(barock::compositor_t                                    &compositor,
             GLuint                                                   program,
             barock::shared_t<barock::resource_t<barock::surface_t>> &surface,
             minidrm::framebuffer::egl_t                             &screen,
             int32_t                                                  parent_x,
             int32_t                                                  parent_y) {
  // We only render surfaces that have a role attached.
  int32_t x = parent_x, y = parent_y, width = 0, height = 0;
  GLuint  texture = 0;

  // Check the role, we have to render different things accordingly
  barock::region_t bounds = surface->extent();
  x += bounds.x;
  y += bounds.y;

  if (surface->state.buffer) {
    auto &shm = surface->state.buffer;
    width     = shm->width;
    height    = shm->height;
  }

  // if (surface->role && surface->role->type_id() == barock::xdg_surface_t::id()) {
  //   auto xdg_surface = shared_cast<barock::xdg_surface_t>(surface->role);
  //   if (xdg_surface) {
  //     x -= xdg_surface->x;
  //     y -= xdg_surface->y;
  //   }
  // }

  // Window is improperly configured, likely that the client hasn't
  // attached a buffer yet.
  if (width <= 0 || height <= 0) {
    // WARN("surface has no width, or height, rendering just the subsurfaces");
    goto render_subsurfaces;
  }

  glViewport(x, screen.mode.height() - (y + height), width, height);
  GL_CHECK;

  if (surface->state.buffer) {
    texture = upload_texture(*surface->state.buffer);
    GL_CHECK;

    draw_quad(program, texture);
    GL_CHECK;

    compositor.schedule_frame_done(surface, barock::current_time_msec());
    glDeleteTextures(1, &texture);
    GL_CHECK;
  }

render_subsurfaces:
  // Similar to the normal drawing loop, to have proper z-indices, we
  // render from back to front (0 being top-most surface)
  for (auto it = surface->state.children.rbegin(); it != surface->state.children.rend(); ++it) {
    auto &subsurface = *it;
    auto  surf       = subsurface->surface.lock();

    int32_t child_x = x + surf->x;
    int32_t child_y = y + surf->y;

    draw_surface(compositor, program, surf, screen, child_x, child_y);
  }
}

void
draw(barock::compositor_t                                      &compositor,
     std::vector<std::unique_ptr<minidrm::framebuffer::egl_t>> &monitors,
     GLint                                                      quad_program) {
  GLuint texture = 0;
  for (auto &screen : monitors) {
    auto front = screen->acquire();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, screen->mode.width(), screen->mode.height());
    glClearColor(0.08f, 0.08f, 0.10f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // NOTE: The z-index of the xdg_surfaces
    // is implicitly designated by array index, 0 is the top-most, and
    // N the bottom-most.
    //
    // Therefore to render properly, we have to iterate from the back,
    // such that index 0 is the last draw call (on top of all others.)
    for (auto it = compositor.xdg_shell->windows.rbegin();
         it != compositor.xdg_shell->windows.rend();
         ++it) {
      auto xdg_surface = *it;
      if (auto surface = (*it)->surface.lock()) {
        barock::region_t compositor_space{};
        compositor_space.x = surface->x;
        compositor_space.y = surface->y;
        compositor_space.x -= xdg_surface->x;
        compositor_space.y -= xdg_surface->y;

        if (auto surface = xdg_surface->surface.lock()) {
          draw_surface(
            compositor, quad_program, surface, *screen, compositor_space.x, compositor_space.y);
        }
      }
    }

    // // Draw debug surface
    // if (auto surface = compositor.pointer.focus.lock()) {
    //   auto pos = surface->position();
    //   auto size = surface->full_extent();

    //   glViewport(pos.x, screen->mode.height() - (pos.y + 26 + size.h), size.w, size.h);
    //   float color[3] = {0xff, 0, 0};
    //   draw_quad(debug_program, color);
    // }

    // Draw cursor
    if (auto pointer = compositor.cursor.surface.lock(); pointer) {
      draw_surface(compositor,
                   quad_program,
                   pointer,
                   *screen,
                   compositor.cursor.x - compositor.cursor.hotspot.x,
                   compositor.cursor.y - compositor.cursor.hotspot.y);
    } else {
      glEnable(GL_SCISSOR_TEST);
      glScissor(
        (int)compositor.cursor.x, (int)screen->mode.height() - (compositor.cursor.y + 16), 16, 16);
      glClearColor(0.0, 1., 0., 1.);
      glClear(GL_COLOR_BUFFER_BIT);
      glDisable(GL_SCISSOR_TEST);
      GL_CHECK;
    }

    screen->present(front);
    GL_CHECK;
  }
}

using namespace minidrm;
int
main() {
  if (!getenv("XDG_SEAT")) {
    ERROR("No XDG_SEAT environment variable set. Exitting.");
    return 1;
  }

  std::vector<std::unique_ptr<framebuffer::egl_t>> monitors;

  auto card = drm::cards()[0];
  auto hdl  = card.open();

  barock::compositor_t compositor(hdl, getenv("XDG_SEAT"));

  compositor.input->on_keyboard_input.connect([&](const barock::keyboard_event_t &key) {
    uint32_t scancode  = libinput_event_keyboard_get_key(key.keyboard);
    uint32_t key_state = libinput_event_keyboard_get_key_state(key.keyboard);
    if (scancode == KEY_ESC) {
      std::exit(0);
    }

    xkb_state_update_key(compositor.keyboard.xkb.state,
                         scancode + 8, // +8: evdev -> xkb
                         key_state == LIBINPUT_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);

    if (auto surface = compositor.keyboard.focus.lock(); surface) {
      xkb_mod_mask_t depressed =
        xkb_state_serialize_mods(compositor.keyboard.xkb.state, XKB_STATE_MODS_DEPRESSED);
      xkb_mod_mask_t latched =
        xkb_state_serialize_mods(compositor.keyboard.xkb.state, XKB_STATE_MODS_LATCHED);
      xkb_mod_mask_t locked =
        xkb_state_serialize_mods(compositor.keyboard.xkb.state, XKB_STATE_MODS_LOCKED);
      xkb_layout_index_t group =
        xkb_state_serialize_layout(compositor.keyboard.xkb.state, XKB_STATE_LAYOUT_EFFECTIVE);

      // Send the modifiers to the focused client
      compositor.keyboard.send_modifiers(surface, depressed, latched, locked, group);
      compositor.keyboard.send_key(surface, scancode, key_state);
      return;
    }

    if (scancode == KEY_ENTER && key_state == LIBINPUT_KEY_STATE_RELEASED) {
      WARN("Starting terminal");
      run_command("foot");
    }
  });

  compositor.input->on_mouse_move.connect([&](const barock::mouse_event_t &move) {
    enum libinput_event_type ty = libinput_event_get_type(move.event);
    // Our `on_mouse_move` signal triggers on two separate libinput events:
    // - LIBINPUT_EVENT_POINTER_MOTION
    // - LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE
    //
    // The latter (MOTION_ABSOLUTE), has it's values based on the
    // absolute extent of the device (imagine a graphics tablet, or a
    // touch-screen).
    //
    // While the former reports just delta.
    //
    // Therefore to correctly handle these two cases, we query all
    // active monitors for their size, and from that compute a
    // relative cursor position.
    auto &cursor = compositor.cursor;

    if (ty == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
      uint32_t sx = 0, sy = 0;
      for (auto &s : monitors) {
        sx += s->mode.width();
        sy = std::max(sy, s->mode.height());
      }

      cursor.x = libinput_event_pointer_get_absolute_x_transformed(move.pointer, sx);
      cursor.y = libinput_event_pointer_get_absolute_y_transformed(move.pointer, sy);
    } else {
      // Relative (delta) movement, just add this onto our position.
      cursor.x += libinput_event_pointer_get_dx(move.pointer);
      cursor.y += libinput_event_pointer_get_dy(move.pointer);
      cursor.y = std::max(cursor.y, 0.);
      cursor.x = std::max(cursor.x, 0.);
    }

    // First figure out whether a surface currently has mouse focus.
    if (auto focus = compositor.pointer.focus.lock()) {
      // We have a surface that has mouse focus.

      // In Wayland, may be a XDG-Surface, in this case, we have to
      // correctly account for the offset of the client-side
      // decoration the client specified in
      // xdg_surface#set_window_geometry

      // This gives us the global space position of the window.
      barock::region_t position = focus->position();

      // Next we compute the size of the entire subtree (all
      // subsurfaces) of the surface.
      position += focus->full_extent();

      // Now `position` encompasses all subsurfaces this surface has,
      // at the correct x and y position.
      //
      // With this, we can now perform a bounding box check. For this
      // check, we have to account for the following:
      //
      //  1. The encompassing `position` we computed is at 0, 0 and
      //  fully encompasses all subsurfaces, however, this does not
      //  account for the subsurface offsets. Some clients position
      //  their CSD at negative offsets (e.g. foot's title bar being
      //  located at Y = -26).  For this reason, we have to take our
      //  position and offset it by the XDG-Surface position (CSD
      //  offset), such that the original (0, 0) would become (0, -26)
      //  in this case.

      barock::region_t offset{}, global_position{};

      {
        auto root = focus->find_parent([](auto &surface) {
          return surface->role && surface->role->type_id() == barock::xdg_surface_t::id();
        });
        if (!root)
          root = focus;

        // Populate the logical offset (XDG Surface geometry for
        // client-side decoration.)
        auto xdg_surface = barock::shared_cast<barock::xdg_surface_t>(root->role);
        offset.x         = xdg_surface->x;
        offset.y         = xdg_surface->y;

        // Set the position and full subtree size to `global_position`
        global_position   = focus->position();
        auto size         = focus->full_extent();
        global_position.w = size.w;
        global_position.h = size.h;

        // Now we subtract the CSD offset from the global position, to
        // get local coordinates.
        if (focus != root) {
          global_position.x -= offset.x;
          global_position.y -= offset.y;
        }
      }

      // Now we compute the local offset from the origin of the
      // surface.
      double local_x, local_y;
      local_x = cursor.x - global_position.x;
      local_y = cursor.y - global_position.y;

      // INFO("Global Position:\n  x = {}, y = {}, w = {}, h = {}\n"
      //      "Raw Position:\n  x = {}, y = {}\n"
      //      "Offset:\n  x = {}, y = {}\n"
      //      "Cursor: {}, {}\n"
      //      "Local: {}, {}",
      //      global_position.x, global_position.y, global_position.w, global_position.h,
      //      focus->position().x, focus->position().y,
      //      offset.x, offset.y,
      //      cursor.x,cursor.y,
      //      local_x, local_y);

      // With this CSD corrected cursor position, we can now perform
      // the intersection test.
      if (!global_position.intersects(cursor.x, cursor.y)) {
        // If we do not intersect, go find a new surface that the
        // mouse is over.
        // ERROR("Not intersecting anymore :/");
        compositor.pointer.set_focus(nullptr);
        goto focus_new_surface;
      }

      // Now we can perform the recursive test against subsurfaces. To
      // account for the CSD, we add our offset onto the cursor
      // position.
      if (auto child = focus->lookup_at(cursor.x + offset.x, cursor.y + offset.y)) {
        compositor.pointer.set_focus(
          barock::shared_cast<barock::resource_t<barock::surface_t>>(child));
        focus = child;
      }

      // Now we can send a motion event.
      compositor.pointer.send_motion(focus, local_x, local_y);
      return;
    }

  focus_new_surface:
    // No surface currently has mouse focus, check all xdg surfaces.
    for (auto &xdg_surface : compositor.xdg_shell->windows) {
      // Compute the position of the surface
      if (auto candidate = xdg_surface->surface.lock()) {
        auto position = candidate->position();
        auto size     = candidate->full_extent();
        position.w    = size.w;
        position.h    = size.h;

        // Check whether our cursor point intersects the surface.
        // TODO: Doesn't respect cursor hotspot yet!
        if (position.intersects(cursor.x, cursor.y)) {
          // Since we match the top window, we can now recursively
          // descent into the subsurfaces.  Should we find one that
          // matches our local coordinates, use that; otherwise focus
          // `candidate` itself.
          if (auto subsurface = candidate->lookup_at(cursor.x, cursor.y)) {
            compositor.pointer.set_focus(
              barock::shared_cast<barock::resource_t<barock::surface_t>>(subsurface));
          } else {
            compositor.pointer.set_focus(candidate);
          }
          break;
        }
      }
    }
  });

  compositor.input->on_mouse_button.connect([&](const auto &btn) {
    auto &cursor = compositor.cursor;

    if (auto surface = compositor.pointer.focus.lock(); surface) {
      compositor.pointer.send_button(surface, btn.button, btn.state);

      if (btn.button == BTN_LEFT) {
        if (auto active_surface = compositor.window.activated.lock()) {
          if (active_surface != surface) {
            // First deactivate whatever is currently activated.
            compositor.window.deactivate(active_surface);

            // Then activate the new window
            compositor.window.activate(surface);
          }
        } else {
          // We have no active window, immediately activate
          compositor.window.activate(surface);
        }

        if (compositor.keyboard.focus != surface) {
          compositor.keyboard.set_focus(surface);
        }
      }
      return;
    } else {
      //
      // Clicking outside of any window loses all focus
      //
      if (auto surface = compositor.window.activated.lock()) {
        compositor.window.deactivate(surface);
      }
      compositor.keyboard.set_focus(nullptr);
    }
  });

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

  std::thread([&] {
    for (;;) {
      compositor.input->poll(-1);
    }
  }).detach();

  wl_display    *display  = compositor.display();
  wl_event_loop *loop     = wl_display_get_event_loop(display);
  static int     throttle = 0;

  quad_program  = init_quad_program();
  debug_program = init_quad_program_color();

  while (1) {
    // Dispatch events (fd handlers, client requests, etc.)
    wl_event_loop_dispatch(loop, 0); // 0 = non-blocking, -1 = blocking

    draw(compositor, monitors, quad_program);

    // Dispatch frame callbacks
    compositor.frame_done_flush_callback(&compositor);

    // Flush any pending Wayland client events
    wl_display_flush_clients(display);
  }

  return 1;
}
