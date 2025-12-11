// Copyright (c) 2024 Justin Andreas Lacoste (@27justin <me@justin.cx>)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the
// use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
//     1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software in a
//     product, an acknowledgment in the product documentation would be
//     appreciated but is not required.
//
//     2. Altered source versions must be plainly marked as such, and must not
//     be misrepresented as being the original software.
//
//     3. This notice may not be removed or altered from any source
//     distribution.
//
// SPDX-License-Identifier: Zlib

#pragma once

#include <cstring>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(MINIDRM_EGL) || defined(MINIDRM_VULKAN)
#include <gbm.h>
#endif

#ifdef MINIDRM_EGL
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#endif

namespace minidrm {
  namespace fs                   = std::filesystem;
  constexpr const char *DRI_PATH = "/dev/dri/";

  struct RGB {
    uint8_t b, g, r, _pad;
  };
  struct ARGB {
    uint8_t a, r, g, b;
  };

  namespace drm {
    struct handle_t;
    struct card_t;
    struct crtc_t;
    struct connector_t;
    struct mode_t;

    struct card_t {
      fs::path path;

      handle_t
      open() const;
    };

    struct handle_data_t {
#if defined(MINIDRM_EGL) || defined(MINIDRM_VULKAN)
      gbm_device *gbm;
#endif
#if defined(MINIDRM_EGL)
      struct {
        EGLDisplay display;
        EGLConfig  config;
        EGLContext context;
        bool       initialized = false;
      } egl;
#endif
    };

    struct handle_t {
      public:
      card_t                  card;
      int                     fd;
      std::atomic<uintmax_t> *references;

      handle_data_t *data;

#if defined(MINIDRM_EGL)
      void
      init_egl();
#endif

      std::vector<connector_t>
      connectors() const;

      std::vector<crtc_t>
      crtcs() const;

      handle_t(const handle_t &handle);
      ~handle_t();

      handle_t &
      operator=(const handle_t &);

      handle_data_t *
      operator->();

      private:
      handle_t(const card_t &card);
      friend class card_t;
    };

    struct connector_t {
      drmModeConnector       *connector;
      std::atomic<uintmax_t> *references;

      connector_t(drmModeConnector *conn);
      connector_t(const connector_t &);
      ~connector_t();

      /**
       * @brief Returns the type of the connector, e.g. `HDMI-A`, or `DP` for display port.
       */
      std::string
      type() const;

      /**
       * @brief Returns the type of the connector, e.g. `HDMI-A`, or `DP` suffixed by the connector
       * type id, resulting in unique connector names (e.g. `DP-1`, `DP-2`, etc.).
       */
      std::string
      name() const;

      drmModeConnection
      connection() const;

      std::vector<mode_t>
      modes() const;

      const drmModeConnector *
      operator->() const;
      drmModeConnector *
      operator->();
    };

    struct mode_t {
      drmModeModeInfo mode;
      bool            preferred;

      public:
      mode_t(drmModeModeInfo *info);

      uint32_t
      width() const;
      uint32_t
      height() const;

      /// Return mode refresh rate in Hz
      float
      refresh_rate() const;

      const drmModeModeInfo *
      operator->() const;
      drmModeModeInfo *
      operator->();
    };

    struct crtc_t {
      public:
      uint32_t     id;
      drmModeCrtc *crtc;

      crtc_t(const crtc_t &);
      ~crtc_t();

      private:
      crtc_t(uint32_t, drmModeCrtc *);
      friend class handle_t;
    };

    std::vector<card_t>
    cards();
  };

  namespace framebuffer {
    struct software_t {
      drm::handle_t drm;
      uint32_t      id, width, height, stride, handle, size;
      uint8_t      *data;

      software_t(const drm::handle_t &handle, uint32_t width, uint32_t height);
      void
      clear(RGB color);
      void
      mode_set(const drm::connector_t &conn, drm::crtc_t &crtc);
    };

#if defined(MINIDRM_EGL)
    struct egl_t {
      drm::handle_t    drm;
      drm::connector_t connector;
      drm::crtc_t      crtc;
      drm::mode_t      mode;

      struct gbm_surface *surface;
      EGLSurface          egl_surface;

      struct egl_buffer_t {
        struct gbm_bo *bo;
        uint32_t       fb;
      };
      uint32_t                               num_backbuffers;
      std::atomic<uint32_t>                  current_backbuffer;
      std::unordered_map<gbm_bo *, uint32_t> bo_to_fb;
      egl_buffer_t                          *backbuffers;
      gbm_bo                                *last_bo;

      egl_t(drm::handle_t          &handle,
            const drm::connector_t &conn,
            const drm::crtc_t      &crtc,
            const drm::mode_t      &mode,
            uint32_t                backbuffers = 2);

      egl_t(const egl_t &) = delete;
      egl_t(egl_t &&);

      ~egl_t();

      void
      mode_set();

      void
      bind();

      egl_buffer_t
      acquire();

      void
      present(const egl_buffer_t &buf);
    };
#endif
  };
}

#ifdef MINIDRM_IMPLEMENTATION
namespace minidrm::drm {

  handle_t::handle_t(const card_t &card)
    : card(card)
    , fd(open(card.path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK))
    , references(new std::atomic<uintmax_t>(1)) {
    if (fd < 0) {
      throw std::runtime_error("Failed to open DRM device.");
    }

    // if (drmSetMaster(fd)) {
    //   perror("drmSetMaster");
    // }

    data = new handle_data_t;
#if defined(MINIDRM_EGL) || defined(MINIDRM_VULKAN)
    data->gbm = gbm_create_device(fd);
#endif

#if defined(MINIDRM_EGL)
    data->egl.display = nullptr;
    data->egl.context = nullptr;
    data->egl.config  = nullptr;
#endif
  }

  handle_t &
  handle_t::operator=(const handle_t &other) {
    if (&other == this)
      return *this;

    (*references)--;
    card       = other.card;
    fd         = other.fd;
    references = other.references;
    data       = other.data;

    (*references)++;
    return *this;
  }

  handle_data_t *
  handle_t::operator->() {
    return data;
  }

  handle_t::handle_t(const handle_t &other)
    : card(other.card)
    , fd(other.fd)
    , references(other.references)
    , data(other.data) {
    // Increment the references
    (*references)++;
  }

  handle_t::~handle_t() {
    // Close the DRI device when no-one has any reference anymore.
    if (--(*references) <= 0) {
#if defined(MINIDRM_EGL) || defined(MINIDRM_VULKAN)
      gbm_device_destroy(data->gbm);
#endif
      close(fd);
      delete references;
      delete data;
    }
  }

  handle_t
  card_t::open() const {
    handle_t handle = handle_t(*this);
#if defined(MINIDRM_EGL)
    // We immediately initialize EGL when we open the card.
    //
    // TODO: I
    // am not quite satisfied with this, this /should/ be an
    // explicit operation
    handle.init_egl();
#endif
    return handle;
  }

  connector_t::connector_t(drmModeConnector *conn)
    : connector(conn)
    , references(new std::atomic<uintmax_t>(1)) {}

  connector_t::connector_t(const connector_t &other)
    : connector(other.connector)
    , references(other.references) {
    (*references)++;
  }

  connector_t::~connector_t() {
    if (--(*references) <= 0) {
      drmModeFreeConnector(connector);
      delete references;
    }
  }

  std::vector<connector_t>
  handle_t::connectors() const {
    std::vector<connector_t> result;
    drmModeRes              *resources = drmModeGetResources(fd);

    for (int i = 0; i < resources->count_connectors; ++i) {
      drmModeConnector *conn = drmModeGetConnector(fd, resources->connectors[i]);
      if (!conn)
        continue;

      result.emplace_back(conn);
    }

    drmModeFreeResources(resources);
    return result;
  }

  crtc_t::crtc_t(uint32_t id, drmModeCrtc *ptr)
    : id(id)
    , crtc(ptr) {}

  crtc_t::crtc_t(const crtc_t &other)
    : id(other.id)
    , crtc(reinterpret_cast<drmModeCrtc *>(malloc(sizeof(drmModeCrtc)))) {
    memcpy(crtc, other.crtc, sizeof(*crtc));
  }

  crtc_t::~crtc_t() {
    drmModeFreeCrtc(crtc);
  }

  std::vector<crtc_t>
  handle_t::crtcs() const {
    std::vector<crtc_t> result;
    drmModeRes         *resources = drmModeGetResources(fd);
    for (int i = 0; i < resources->count_crtcs; ++i) {
      result.emplace_back(crtc_t{ resources->crtcs[i], drmModeGetCrtc(fd, resources->crtcs[i]) });
    }

    drmModeFreeResources(resources);
    return result;
  }

  std::vector<mode_t>
  connector_t::modes() const {
    std::vector<mode_t> result;
    result.reserve(connector->count_modes);

    for (auto i = 0; i < connector->count_modes; ++i) {
      result.emplace_back(&connector->modes[i]);
    }

    return result;
  }

  mode_t::mode_t(drmModeModeInfo *info) {
    memcpy(&mode, info, sizeof(mode));
    preferred = (info->type & DRM_MODE_TYPE_PREFERRED) > 0;
  }

  const drmModeConnector *
  connector_t::operator->() const {
    return connector;
  }

  drmModeConnector *
  connector_t::operator->() {
    return connector;
  }

  drmModeConnection
  connector_t::connection() const {
    return connector->connection;
  }

  std::string
  connector_t::type() const {
    auto name = drmModeGetConnectorTypeName(connector->connector_type);
    if (!name) {
      return "Unknown";
    }
    return name;
  }

  std::string
  connector_t::name() const {
    auto name = type();
    name += '-';
    name += std::to_string(connector->connector_type_id);
    return name;
  }

  uint32_t
  mode_t::width() const {
    return mode.hdisplay;
  }

  uint32_t
  mode_t::height() const {
    return mode.vdisplay;
  }

  float
  mode_t::refresh_rate() const {
    int res = (mode.clock * 1000000LL / mode.htotal + mode.vtotal / 2) / mode.vtotal;

    if (mode.flags & DRM_MODE_FLAG_INTERLACE)
      res *= 2;

    if (mode.flags & DRM_MODE_FLAG_DBLSCAN)
      res /= 2;

    if (mode.vscan > 1)
      res /= mode.vscan;

    return res / 1000.f;
  }

  std::vector<card_t>
  cards() {
    std::vector<card_t> cards;

    for (auto const &ent : fs::directory_iterator(DRI_PATH)) {
      auto filename = ent.path().filename();
      // Check whether the file starts with `card`
      if (filename.string().find("card") == std::string::npos)
        continue;

      cards.emplace_back(card_t{ .path = ent.path() });
    }

    // Sort by ascending index of the card (i.e. card0, card1, etc.)
    std::sort(cards.begin(), cards.end(), [](const card_t &a, const card_t &b) {
      // Get the filename (e.g., "card0", "card12")
      std::string name_a = a.path.filename().string();
      std::string name_b = b.path.filename().string();

      // Offset until the number within the filenames, i.e.
      constexpr size_t pos = strlen("card");

      // Extract the numerical index
      int index_a = std::stoi(name_a.substr(pos));
      int index_b = std::stoi(name_b.substr(pos));

      return index_a < index_b;
    });

    return cards;
  }

#if defined(MINIDRM_EGL)
  void
  handle_t::init_egl() {
    if (data->egl.initialized)
      return;

    data->egl.display = eglGetDisplay(data->gbm);
    if (data->egl.display == EGL_NO_DISPLAY) {
      throw std::runtime_error("Failed to get EGL display");
    }

    if (!eglInitialize(data->egl.display, nullptr, nullptr)) {
      throw std::runtime_error("Failed to initialize EGL");
    }

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
      throw std::runtime_error("eglBindAPI failed");
    }

    // 4. Choose EGL config
    const EGLint config_attribs[] = { EGL_SURFACE_TYPE,
                                      EGL_WINDOW_BIT,
                                      // RGB
                                      EGL_RED_SIZE,
                                      8,
                                      EGL_GREEN_SIZE,
                                      8,
                                      EGL_BLUE_SIZE,
                                      8,
                                      EGL_ALPHA_SIZE,
                                      0,
                                      EGL_RENDERABLE_TYPE,
                                      EGL_OPENGL_ES2_BIT,
                                      EGL_NONE };

    EGLint    matching_config;
    EGLConfig configs[64];
    EGLint    num_configs;
    eglChooseConfig(data->egl.display, config_attribs, configs, 64, &num_configs);

    bool found = false;
    for (int i = 0; i < num_configs; ++i) {
      EGLint id;
      if (eglGetConfigAttrib(data->egl.display, configs[i], EGL_NATIVE_VISUAL_ID, &id)) {
        if (id == GBM_FORMAT_XRGB8888) {
          data->egl.config = configs[i];
          found            = true;
          break;
        }
      }
    }

    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    data->egl.context =
      eglCreateContext(data->egl.display, data->egl.config, EGL_NO_CONTEXT, ctx_attribs);
    if (data->egl.context == EGL_NO_CONTEXT) {
      throw std::runtime_error("eglCreateContext failed");
    }

    data->egl.initialized = true;
  }
#endif
} // namespace minidrm::drm

namespace minidrm::framebuffer {
  software_t::software_t(const drm::handle_t &drm_handle, uint32_t width, uint32_t height)
    : drm(drm_handle) {
    struct drm_mode_create_dumb create = {
      .height = height,
      .width  = width,
      .bpp    = 32,
    };

    drmIoctl(drm.fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
    handle = create.handle;
    stride = create.pitch;
    size   = create.size;

    uint32_t handles[4] = { handle };
    uint32_t strides[4] = { stride };
    uint32_t offsets[4] = { 0 };

    drmModeAddFB2(drm.fd, width, height, DRM_FORMAT_XRGB8888, handles, strides, offsets, &id, 0);

    struct drm_mode_map_dumb map = { .handle = handle };
    drmIoctl(drm.fd, DRM_IOCTL_MODE_MAP_DUMB, &map);

    data = reinterpret_cast<uint8_t *>(
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, drm.fd, map.offset));
  }

  void
  software_t::clear(RGB col) {
    size_t num_pixels = size / sizeof(RGB);
    RGB   *pixels     = reinterpret_cast<RGB *>(data);
    for (size_t i = 0; i < num_pixels; ++i) {
      pixels[i] = col;
    }
  }

  void
  software_t::mode_set(const drm::connector_t &conn, drm::crtc_t &crtc) {
    int result = drmModeSetCrtc(
      drm.fd, crtc.id, id, 0, 0, const_cast<uint32_t *>(&conn->connector_id), 1, &crtc.crtc->mode);
    if (result) {
      throw std::runtime_error("Failed to mode set.");
    }
  }

#if defined(MINIDRM_EGL)
  egl_t::egl_t(drm::handle_t          &drm_handle,
               const drm::connector_t &conn,
               const drm::crtc_t      &crtc,
               const drm::mode_t      &mode,
               uint32_t                bufs)
    : drm(drm_handle)
    , connector(conn)
    , crtc(crtc)
    , mode(mode)
    , num_backbuffers(bufs)
    , current_backbuffer(0) {

    surface = gbm_surface_create(drm->gbm,
                                 mode.width(),
                                 mode.height(),
                                 GBM_FORMAT_XRGB8888,
                                 GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!surface) {
      throw std::runtime_error("failed to create GBM surface");
    }

    // 7. Create EGL window surface from GBM surface
    egl_surface =
      eglCreateWindowSurface(drm->egl.display, drm->egl.config, (EGLNativeWindowType)surface, NULL);
    if (!egl_surface) {
      throw std::runtime_error("Failed to create EGL surface");
    }

    backbuffers = new egl_buffer_t[num_backbuffers];
    for (int i = 0; i < num_backbuffers; ++i) {
      backbuffers[i] = egl_buffer_t{
        .bo = nullptr,
        .fb = 0,
      };
    }

    // 8. Make context current & do an initial swap to render.
    if (!eglMakeCurrent(drm->egl.display, egl_surface, egl_surface, drm->egl.context)) {
      throw std::runtime_error("Failed to eglMakeCurrent");
    }
    eglSwapBuffers(drm->egl.display, egl_surface);

    // Provision the first FB id
    gbm_bo *bo = backbuffers[0].bo = gbm_surface_lock_front_buffer(surface);
    if (!bo) {
      throw std::runtime_error("Failed to lock gbm surface");
    }
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    int      ret =
      drmModeAddFB(drm.fd, mode.width(), mode.height(), 24, 32, stride, handle, &backbuffers[0].fb);
    if (ret) {
      gbm_surface_release_buffer(surface, bo);
      throw std::runtime_error("egl_t::ctor drmModeAddFB failed");
    }
    bo_to_fb[bo] = backbuffers[0].fb;
    gbm_surface_release_buffer(surface, bo);
  }

  egl_t::egl_t(egl_t &&other)
    : drm(other.drm)
    , connector(other.connector)
    , crtc(other.crtc)
    , mode(other.mode) {

    surface     = std::exchange(other.surface, nullptr);
    egl_surface = other.egl_surface;

    num_backbuffers    = other.num_backbuffers;
    current_backbuffer = other.current_backbuffer.load();
    bo_to_fb           = std::move(other.bo_to_fb);
    backbuffers        = std::exchange(other.backbuffers, nullptr);
    last_bo            = std::exchange(other.last_bo, nullptr);
  }

  egl_t::~egl_t() {
    delete[] backbuffers;
  }

  egl_t::egl_buffer_t
  egl_t::acquire() {
    if (!eglMakeCurrent(drm->egl.display, egl_surface, egl_surface, drm->egl.context)) {
      throw std::runtime_error("Failed to eglMakeCurrent");
    }
    return egl_buffer_t{ .bo = nullptr, .fb = 0 };
  }

  void
  egl_t::present(const egl_buffer_t &buf) {
    if (!eglMakeCurrent(drm->egl.display, egl_surface, egl_surface, drm->egl.context)) {
      throw std::runtime_error("Failed to eglMakeCurrent");
    }
    eglSwapBuffers(drm->egl.display, egl_surface);

    // Lock the surface we just rendered to.
    gbm_bo *bo = gbm_surface_lock_front_buffer(surface);
    if (!bo) {
      throw std::runtime_error("gbm_surface_lock_front_buffer failed");
    }

    // Figure out the framebuffer id we are using for this buffer
    // object, or add a new framebuffer, if none is present yet.
    uint32_t fb_id;
    auto     it = bo_to_fb.find(bo);
    if (it != bo_to_fb.end()) {
      fb_id = it->second;
    } else {
      uint32_t handle = gbm_bo_get_handle(bo).u32;
      uint32_t stride = gbm_bo_get_stride(bo);
      int ret = drmModeAddFB(drm.fd, mode.width(), mode.height(), 24, 32, stride, handle, &fb_id);
      if (ret) {
        gbm_surface_release_buffer(surface, bo);
        throw std::runtime_error("drmModeAddFB failed");
      }
      bo_to_fb[bo] = fb_id;
    }

    struct event_data {
      std::atomic<bool> flip_done{ false };
    };

    auto ev_data       = new event_data;
    ev_data->flip_done = false;

    // Tell the DRM to flip our framebuffer
    int ret = drmModePageFlip(drm.fd,
                              crtc.id,
                              fb_id,
                              DRM_MODE_PAGE_FLIP_EVENT, // async, we'll wait later
                              ev_data);
    if (ret) {
      throw std::runtime_error("drmModePageFlip failed");
    }

    // Wait for the flip to be finished
    drmEventContext evctx   = {};
    evctx.version           = DRM_EVENT_CONTEXT_VERSION;
    evctx.page_flip_handler = [](int fd, unsigned crtc, unsigned frame, unsigned sec, void *user) {
      auto *data      = reinterpret_cast<decltype(ev_data)>(user);
      data->flip_done = true;
    };

    while (!ev_data->flip_done) {
      drmHandleEvent(drm.fd, &evctx);
    }
    delete ev_data;

    // Release the last buffer
    if (last_bo) {
      gbm_surface_release_buffer(surface, last_bo);
    }
    last_bo = bo;
  }

  void
  egl_t::mode_set() {
    // Set CRTC to display the framebuffer
    int ret = drmModeSetCrtc(drm.fd,
                             crtc.id,
                             backbuffers[current_backbuffer].fb,
                             0,
                             0,
                             &connector->connector_id,
                             1,
                             &crtc.crtc->mode);
    if (ret) {
      throw std::runtime_error("Failed to mode set EGL buffer");
    }
  }
#endif
} // namespace minidrm::framebuffer
#endif // MINIDRM_IMPLEMENTATION
