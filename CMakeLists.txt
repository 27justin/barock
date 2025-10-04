cmake_minimum_required(VERSION 3.19)
project(barock CXX C)
set (CMAKE_CXX_STANDARD 23)

include(FindPkgConfig)

add_executable(barock)
target_compile_options(barock PRIVATE "-fdiagnostics-color")
target_compile_options(barock PRIVATE "-std=c++23")
target_compile_options(barock PRIVATE "-fsanitize=address")
target_compile_options(barock PRIVATE "-Og" "-g3")
target_link_options(barock PRIVATE "-fsanitize=address")
target_link_libraries(barock PUBLIC m)

set(barock_SOURCES
  src/main.cpp
  src/compositor.cpp
  src/log.cpp

  # core
  src/core/wl_compositor.cpp
  src/core/wl_surface.cpp
  src/core/shm.cpp
  src/core/shm_pool.cpp

  # xdg shell
  src/shell/xdg_wm_base.cpp
  src/shell/xdg_surface.cpp
  src/shell/xdg_toplevel.cpp
  )

target_include_directories(barock PUBLIC "include/")

add_subdirectory(src/drm)

target_sources(barock
  PUBLIC
  ${barock_SOURCES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-server wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)

include(cmake/wayland.cmake)
generate_wayland_protocol(barock ${CMAKE_SOURCE_DIR}/protocols/xdg-shell.xml)
generate_wayland_protocol(barock ${CMAKE_SOURCE_DIR}/protocols/wayland.xml)

target_link_libraries(barock PRIVATE minidrm)
target_link_libraries(barock PRIVATE wayland-server)
