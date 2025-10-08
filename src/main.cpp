#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <iostream>
#include <libudev.h>
#include <memory>

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

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, buffer.width, buffer.height, 0, GL_BGRA_EXT,
               GL_UNSIGNED_BYTE, buffer.data());
  GL_CHECK;

  return texture;
}

pid_t
run_command(std::string_view cmd) {
  pid_t pid;
  // /bin/sh -c "cmd"
  const char *argv[] = { (char *)"sh", (char *)"-c", nullptr, nullptr };
  std::string cmdstr(cmd);
  argv[2] = const_cast<char *>(cmdstr.c_str());
  if (posix_spawnp(&pid, "sh", NULL, NULL, const_cast<char *const *>(argv), environ) != 0) {
    perror("posix_spawnp");
    return -1;
  }
  return pid;
}

void
draw_surface(barock::compositor_t        &compositor,
             GLuint                       program,
             barock::surface_t           &surface,
             minidrm::framebuffer::egl_t &screen,
             int32_t                      parent_x,
             int32_t                      parent_y) {
  // We only render surfaces that have a role attached.
  int32_t x = parent_x, y = parent_y, width = 0, height = 0;
  GLuint  texture = 0;

  // Check the role, we have to render different things accordingly
  if (surface.role) {
    if (surface.role->type_id() == barock::xdg_surface_t::id()) {
      // XDG Surfaces, either a xdg_toplevel (window) or a xdg_popup
      auto &xdg_surface = *reinterpret_cast<barock::xdg_surface_t *>(surface.role);
      switch (xdg_surface.role) {
        case barock::xdg_role_t::eToplevel: {
          auto &role = xdg_surface.as.toplevel->data;
          x += role.x;
          y += role.y;
          width  = role.width;
          height = role.height;
          break;
        }
        case barock::xdg_role_t::ePopup: {
          ERROR("[draw_surface] xdg_popup rendering is not yet implemented!");
          return;
        }
        case barock::xdg_role_t::eNone: {
          WARN("[draw_surface] got passed a xdg_surface with no role attached!");
          return;
        }
      }
    } else {
      ERROR("[draw_surface] unknown surface role");
      return;
    }
  } else if (surface.state.buffer) {
    // No role, but has buffer (likely a wl_subsurface)
    auto *buffer = (barock::shm_buffer_t *)wl_resource_get_user_data(surface.state.buffer);
    width        = buffer->width;
    height       = buffer->height;
  } else {
    // WARN("[draw_surface] surface has no role and no buffer");
    goto render_subsurfaces;
    return;
  }

  // Window is improperly configured, likely that the client hasn't
  // attached a buffer yet.
  if (width <= 0 || height <= 0)
    return;

  glViewport(x, screen.mode.height() - (y + height), width, height);
  GL_CHECK;

  if (surface.state.buffer) {
    barock::shm_buffer_t *buffer =
      (barock::shm_buffer_t *)wl_resource_get_user_data(surface.state.buffer);

    texture = upload_texture(*buffer);
    GL_CHECK;

    draw_quad(program, texture);
    GL_CHECK;

    compositor.schedule_frame_done(&surface, barock::current_time_msec());
    glDeleteTextures(1, &texture);
    GL_CHECK;
  }

render_subsurfaces:
  for (auto &surf : surface.state.subsurfaces) {
    draw_surface(compositor, program, *surf->surface->get(), screen, surf->x + x, surf->y + y);
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

    // if (compositor.active_surface != nullptr) {
    //   wl_client *owner = wl_resource_get_client(compositor.active_surface->wl_surface);
    //   if (!compositor.wl_seat->seats.contains(owner)) {
    //     return;
    //   }

    //   auto &seat = compositor.wl_seat->seats[owner];
    //   if (!seat->keyboard)
    //     return;

    //   wl_keyboard_send_key(seat->keyboard, wl_display_next_serial(compositor.display()),
    //                        barock::current_time_msec(), scancode, key_state);
    //   return;
    // }

    if (scancode == KEY_ENTER && key_state == LIBINPUT_KEY_STATE_RELEASED) {
      INFO("Starting terminal");
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

    if (ty == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
      uint32_t sx = 0, sy = 0;
      for (auto &s : monitors) {
        sx += s->mode.width();
        sy = std::max(sy, s->mode.height());
      }

      compositor.cursor.x = libinput_event_pointer_get_absolute_x_transformed(move.pointer, sx);
      compositor.cursor.y = libinput_event_pointer_get_absolute_y_transformed(move.pointer, sy);
    } else {
      // Relative (delta) movement, just add this onto our position.
      compositor.cursor.x += libinput_event_pointer_get_dx(move.pointer);
      compositor.cursor.y += libinput_event_pointer_get_dy(move.pointer);
      compositor.cursor.y = std::max(compositor.cursor.y, 0.);
      compositor.cursor.x = std::max(compositor.cursor.x, 0.);
    }

    // INFO("cursor:\n  x = {}\n  y = {}", compositor.cursor.x,compositor.cursor.y);

    // Do the hit test against surfaces.
    // bool hit = false;
    // for (auto &surf : compositor.wl_compositor->surfaces) {
    //   if (!surf->role)
    //     continue;

    //   int32_t bounds_x = 0, bounds_y = 0, w = 0, h = 0;

    //   if (surf->role->type_id() == barock::xdg_surface_t::id()) {
    //     auto &xdg_surface = *reinterpret_cast<barock::xdg_surface_t *>(surf->role);

    //     switch (xdg_surface.role) {
    //       case barock::xdg_role_t::eToplevel: {
    //         auto &role = xdg_surface.as.toplevel->data;

    //         // Compute bounds of window's drawable content
    //         bounds_x = role.x; // + xdg_surface.x;
    //         bounds_y = role.y; // + xdg_surface.y;
    //         w        = role.width;
    //         h        = role.height;
    //         break;
    //       }
    //       case barock::xdg_role_t::ePopup: {
    //         WARN("xdg_popup hit test not implemented.");
    //         continue;
    //       }
    //       default:
    //         continue;
    //     }
    //   }
    //   // INFO("surface:\n  x = {}\n  y = {}\n  w = {}\n  h = {}",bounds_x,bounds_y,w,h);

    //   // Check that our cursor hover over the space the client takes up.
    //   if (compositor.cursor.x >= bounds_x && compositor.cursor.y >= bounds_y &&
    //       compositor.cursor.x < (bounds_x + w) && compositor.cursor.y < (bounds_y + h)) {

    //     wl_client *activated = wl_resource_get_client(surf.resource());

    //     int surface_x = compositor.cursor.x - bounds_x;
    //     int surface_y = compositor.cursor.y - bounds_y;

    //     auto &seat_map = compositor.wl_seat->seats;
    //     if (!seat_map.contains(activated)) {
    //       WARN("No seat attached for interacting surface");
    //       continue;
    //     }

    //     auto &seat = seat_map[activated];
    //     if (compositor.active_surface == surf.get()) {
    //       wl_pointer_send_motion(seat->pointer, barock::current_time_msec(),
    //       wl_fixed_from_int(surface_x),
    //                              wl_fixed_from_int(surface_y));
    //     } else {

    //       if (compositor.active_surface != nullptr) {
    //         wl_client *previous = wl_resource_get_client(compositor.active_surface->wl_surface);
    //         if (seat_map.contains(previous)) {
    //           wl_pointer_send_leave(seat_map[previous]->pointer,
    //                                 wl_display_next_serial(compositor.display()),
    //                                 compositor.active_surface->wl_surface);

    //           if (auto keyboard = seat_map[previous]->keyboard; keyboard) {
    //             WARN("wl_keyboard#leave");
    //             wl_keyboard_send_leave(keyboard, wl_display_next_serial(compositor.display()),
    //                                    compositor.active_surface->wl_surface);
    //           }
    //         }
    //       }

    //       seat->pointer_focus       = surf.get();
    //       compositor.active_surface = surf.get();
    //       wl_pointer_send_enter(seat->pointer, wl_display_next_serial(compositor.display()),
    //                             surf.resource(), wl_fixed_from_int(surface_x),
    //                             wl_fixed_from_int(surface_y));

    //       if (seat->keyboard) {
    //         wl_array keys;
    //         wl_array_init(&keys);
    //         WARN("wl_keyboard#enter");
    //         wl_keyboard_send_enter(seat->keyboard, wl_display_next_serial(compositor.display()),
    //                                surf.resource(), &keys);
    //         wl_array_release(&keys);
    //         wl_keyboard_send_modifiers(seat->keyboard,
    //         wl_display_next_serial(compositor.display()),
    //                                    0, 0, 0, 0);
    //       }
    //     }
    //     hit = true;
    //     break;
    //   }
    // }

    // if (!hit && compositor.active_surface) {
    //   // We are not hovering any surface, if we had one active before,
    //   // we send the leave event on that one.
    //   wl_client *previous = wl_resource_get_client(compositor.active_surface->wl_surface);
    //   if (compositor.wl_seat->seats.contains(previous)) {
    //     wl_pointer_send_leave(compositor.wl_seat->seats[previous]->pointer,
    //                           wl_display_next_serial(compositor.display()),
    //                           compositor.active_surface->wl_surface);
    //     if (auto keyboard = compositor.wl_seat->seats[previous]->keyboard; keyboard) {
    //       WARN("wl_keyboard#leave");
    //       wl_keyboard_send_leave(keyboard, wl_display_next_serial(compositor.display()),
    //                              compositor.active_surface->wl_surface);
    //     }
    //     compositor.active_surface = nullptr;
    //   }
    // }
  });

  compositor.input->on_mouse_button.connect([&](const auto &btn) {
    if (auto resource = compositor.focus.pointer.lock(); resource) {
      INFO("Pointer focus surface is still alive!");
    }
    // if (compositor.active_surface) {
    //   auto &seats = compositor.wl_seat->seats;
    //   wl_client *client = wl_resource_get_client(compositor.active_surface->wl_surface);
    //   if (seats.contains(client) && seats[client]->pointer) {
    //     wl_pointer_send_button(seats[client]->pointer,
    //     wl_display_next_serial(compositor.display()), barock::barock::current_time_msec(),
    //     btn.button, btn.state);
    //   }
    // }
  });

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

  std::thread([&] {
    for (;;) {
      compositor.input->poll(-1);
    }
  }).detach();

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

  auto   quad_program = init_quad_program();
  GLuint texture      = 0;
  for (;;) {
    for (auto &screen : monitors) {
      auto front = screen->acquire();

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

      glViewport(0, 0, screen->mode.width(), screen->mode.height());
      glClearColor(0.08f, 0.08f, 0.10f, 1.f);
      glClear(GL_COLOR_BUFFER_BIT);

      // Iterate all surfaces...
      for (auto surface : compositor.wl_compositor->surfaces) {
        if (!surface->get()->role)
          continue;
        if (surface->get()->role->type_id() != barock::xdg_surface_t::id())
          continue;

        draw_surface(compositor, quad_program, *surface->get(), *screen, 0, 0);
      }

      // Draw cursor
      if (!compositor.cursor.surface) {
        glEnable(GL_SCISSOR_TEST);
        glScissor((int)compositor.cursor.x, (int)screen->mode.height() - (compositor.cursor.y + 16),
                  16, 16);
        glClearColor(1.0, 0., 0., 1.);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        GL_CHECK;
      } else {
        draw_surface(compositor, quad_program, *compositor.cursor.surface, *screen,
                     compositor.cursor.x + compositor.cursor.hotspot.x,
                     compositor.cursor.y + compositor.cursor.hotspot.y);
      }

      screen->present(front);
      GL_CHECK;
    }
  }

  return 0;
}
