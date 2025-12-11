#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cassert>
#include <condition_variable>
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

using namespace minidrm;
using namespace barock;

int
main() {
  if (!getenv("XDG_SEAT")) {
    ERROR("No XDG_SEAT environment variable set. Exitting.");
    return 1;
  }

  auto cards = drm::cards();
  if (cards.size() == 0) {
    CRITICAL("Found no graphics card, bailing out!");
    return 1;
  }

  // Use the first one.
  auto &card = cards.front();

  TRACE("Using DRM card at {}", card.path.string());
  auto hdl = card.open();

  auto compositor = compositor_t(hdl, getenv("XDG_SEAT"));
  compositor.load_file("config.janet");

  wl_display    *display = compositor.display();
  wl_event_loop *loop    = wl_display_get_event_loop(display);

  for (auto &output : compositor.registry_.output->outputs()) {
    std::thread([&] {
      std::condition_variable     &cv = output->dirty_cv();
      std::unique_lock<std::mutex> lock(output->dirty());

      // Perform the mode set on this thread, EGL is a thread local
      // state machine, we have to mode set on the thread that will
      // also render to the output.
      compositor.registry_.output->mode_set(*output);

      for (;;) {
        cv.wait(lock);
        // Whenever we wake up, we re-render.  The subscribers of
        // `output_t::on_repaint` are responsible for adhering to the
        // damage tree, we just submit.
        output->paint();
      }
    }).detach();
  }

  while (1) {
    wl_event_loop_dispatch(loop, -1); // 0 = non-blocking, -1 = blocking
    wl_display_flush_clients(compositor.display());
  }

  return 0;
}
