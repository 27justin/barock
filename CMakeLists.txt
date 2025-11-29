cmake_minimum_required(VERSION 3.19)
project(barock CXX C)
set (CMAKE_CXX_STANDARD 23)

include(FindPkgConfig)

add_executable(barock)
target_compile_options(barock PRIVATE "-fdiagnostics-color")
target_compile_options(barock PRIVATE "-std=c++23")
target_compile_options(barock PRIVATE "-fsanitize=address")
target_compile_options(barock PRIVATE "-Og" "-g3")
target_compile_options(barock PRIVATE "-Werror=implicit-fallthrough")

target_link_options(barock PRIVATE "-fsanitize=address")
target_link_libraries(barock PUBLIC m)

set(barock_SOURCES
  src/main.cpp
  src/compositor.cpp
  src/log.cpp

  # util
  src/util.cpp

  # input
  src/input.cpp

  # drm
  src/drm/minidrm.cpp

  # core
  src/core/wl_compositor.cpp
  src/core/surface.cpp
  src/core/shm.cpp
  src/core/shm_pool.cpp
  src/core/region.cpp
  src/core/wl_subcompositor.cpp
  src/core/wl_seat.cpp
  src/core/wl_data_device_manager.cpp
  src/core/wl_output.cpp

  # dmabuf
  src/dmabuf/dmabuf.cpp
  src/dmabuf/buffer.cpp
  src/dmabuf/feedback.cpp

  # xdg shell
  src/shell/xdg_wm_base.cpp
  src/shell/xdg_surface.cpp
  src/shell/xdg_toplevel.cpp
  )

target_include_directories(barock PUBLIC "include/")

set(MINIDRM_EGL On)
add_subdirectory(include/drm)

target_sources(barock
  PUBLIC
  ${barock_SOURCES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-server wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)

include(cmake/wayland.cmake)
generate_wayland_protocol(barock ${CMAKE_SOURCE_DIR}/protocols/xdg-shell.xml)
generate_wayland_protocol(barock ${CMAKE_SOURCE_DIR}/protocols/wayland.xml)
generate_wayland_protocol(barock ${CMAKE_SOURCE_DIR}/protocols/linux-dmabuf-v1.xml)

target_link_libraries(barock PRIVATE minidrm)
target_link_libraries(barock PRIVATE wayland-server)

# udev & libinput & libxkbcommon

pkg_check_modules(libinput REQUIRED libinput)
target_link_libraries(barock PRIVATE ${libinput_LIBRARIES})
target_include_directories(barock PRIVATE ${libinput_INCLUDE_DIRS})
target_compile_options(barock PRIVATE ${libinput_CFLAGS})

pkg_check_modules(xkbcommon REQUIRED xkbcommon)
target_link_libraries(barock PRIVATE ${xkbcommon_LIBRARIES})
target_include_directories(barock PRIVATE ${xkbcommon_INCLUDE_DIRS})
target_compile_options(barock PRIVATE ${xkbcommon_CFLAGS})

target_link_libraries(barock PRIVATE udev)
