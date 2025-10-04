cmake_minimum_required(VERSION 3.10)
project(minidrm CXX)

option(MINIDRM_EGL On "Build with EGL/GLESv2 hardware acceleration")

# someday...
# option(MINIDRM_VULKAN On "Build with Vulkan hardware acceleration")

include(FindPkgConfig)

add_library(minidrm INTERFACE)
target_include_directories(minidrm INTERFACE "${PROJECT_SOURCE_DIR}")

# DRM
pkg_check_modules(drm REQUIRED libdrm)
target_link_libraries(minidrm INTERFACE ${drm_LIBRARIES})
target_include_directories(minidrm INTERFACE ${drm_INCLUDE_DIRS})
target_compile_options(minidrm PUBLIC ${drm_CFLAGS_OTHER})

if(MINIDRM_EGL OR MINIDRM_VULKAN)
  # GBM
  pkg_check_modules(gbm REQUIRED gbm)
  target_link_libraries(minidrm INTERFACE ${gbm_LIBRARIES})
  target_include_directories(minidrm INTERFACE ${gbm_INCLUDE_DIRS})
  target_compile_options(minidrm INTERFACE ${gbm_CFLAGS_OTHER})
endif()

if(MINIDRM_EGL)
  # EGL
  pkg_check_modules(EGL REQUIRED egl)
  target_link_libraries(minidrm INTERFACE ${EGL_LIBRARIES})
  target_include_directories(minidrm INTERFACE ${EGL_INCLUDE_DIRS})
  target_compile_options(minidrm INTERFACE ${EGL_CFLAGS_OTHER})

  # GLES2
  pkg_check_modules(glesv2 REQUIRED glesv2)
  target_link_libraries(minidrm INTERFACE ${glesv2_LIBRARIES})
  target_include_directories(minidrm INTERFACE ${glesv2_INCLUDE_DIRS})
  target_compile_options(minidrm INTERFACE ${glesv2_CFLAGS_OTHER})

  target_compile_options(minidrm INTERFACE "-DMINIDRM_EGL")
endif()
