cmake_minimum_required(VERSION 3.20)

project(ReChigros VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RECHIGROS_BUILD_APP "Build the SDL application" ON)
option(RECHIGROS_BUILD_TESTS "Build core regression tests" ON)
option(RECHIGROS_USE_SYSTEM_SDL "Use an installed SDL3 package" OFF)
option(RECHIGROS_ENABLE_ASAN "Enable AddressSanitizer in supported toolchains" OFF)
option(RECHIGROS_ENABLE_LTO "Enable interprocedural optimisation for release builds" ON)
# The interface framework is GNU C and cannot be compiled by MSVC's C front end. On a
# Windows host this points at a clang that can, and the framework's three libraries are
# built from it as static archives.
set(RECHIGROS_GNU_C_COMPILER "" CACHE FILEPATH "clang used for the GNU C interface framework")

# Windows SDK headers define min/max as macros, which breaks std::min/std::max in
# every translation unit that includes a platform header before the STL ones.
if(WIN32)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
  add_compile_options(/utf-8 /Zc:__cplusplus /permissive-)
else()
  add_compile_options(-fno-strict-aliasing)
endif()

# Header-only dependencies are shared by the chart layer and the application, so the
# dependency graph is always available; SDL is the only heavy component and it is
# fetched lazily by Dependencies.cmake.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

add_subdirectory(src/core)
add_subdirectory(src/chart)
if(RECHIGROS_BUILD_APP)
  # The interface framework and its two support libraries are GNU C, so they are built from
  # their vendored sources with a GNU-compatible compiler regardless of the host toolchain.
  include(ObjectivelyMVC)
  add_subdirectory(src/platform)
  add_subdirectory(src/audio)
  add_subdirectory(src/render)
  add_subdirectory(src/app)
  # Developer diagnostics that need a desktop GL context; the packaged platforms skip them.
  if(NOT ANDROID AND NOT IOS)
    add_subdirectory(src/tools)
  endif()
endif()

if(RECHIGROS_BUILD_TESTS)
  if(NOT ANDROID AND NOT IOS)
    enable_testing()
  endif()
  add_subdirectory(tests)
endif()

# Every object must be rebuilt when any public header changes.
#
# The compiler records which headers it read, and the build tool uses that record to decide
# what to recompile. That record is parsed from a compiler diagnostic whose wording is
# localised, so a non-English toolchain can silently produce an empty dependency record:
# edits to a class layout are then not noticed, and an object built against the previous
# layout is linked in. The result is memory corruption at run time that looks like a logic
# bug. Making each object depend on every public header removes that failure mode at the
# cost of recompiling the project when a header changes, which is seconds on this codebase.
file(GLOB_RECURSE RECHIGROS_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp")
function(rechigros_track_headers target)
  if(NOT TARGET ${target})
    return()
  endif()
  get_target_property(sources ${target} SOURCES)
  if(NOT sources)
    return()
  endif()
  # A target lists its sources relative to its own directory, so re-anchor them before
  # attaching the dependency.
  get_target_property(targetDirectory ${target} SOURCE_DIR)
  set(objects)
  foreach(source IN LISTS sources)
    if(source MATCHES "\\$<" OR source MATCHES "\\.(h|hpp|inl)$")
      continue()
    endif()
    if(NOT IS_ABSOLUTE "${source}")
      set(source "${targetDirectory}/${source}")
    endif()
    list(APPEND objects "${source}")
  endforeach()
  if(objects)
    # Source-file properties are scoped to the directory that sets them, so the dependency
    # has to be attached through the target's own directory.
    set_property(SOURCE ${objects} DIRECTORY "${targetDirectory}" APPEND PROPERTY
      OBJECT_DEPENDS "${RECHIGROS_PUBLIC_HEADERS}")
  endif()
endfunction()

rechigros_track_headers(rechigros_core)
rechigros_track_headers(rechigros_chart)
if(RECHIGROS_BUILD_APP)
  rechigros_track_headers(rechigros_platform)
  rechigros_track_headers(rechigros_audio)
  rechigros_track_headers(rechigros_render)
  rechigros_track_headers(ReChigros)
endif()

# Tools, diagnostics and test executables are linked against the same headers, so they need
# the same dependency. Without it a header layout change leaves a stale object in a tool or
# test binary and it corrupts memory at run time (a crash in a previously working test).
foreach(extraTarget IN ITEMS
    rechigros_render_smoke rechigros_block_probe rechigros_frame_profile
    rechigros_import_check
    rechigros_core_tests rechigros_chart_tests rechigros_platform_tests rechigros_audio_tests
    rechigros_transition_tests rechigros_level_effect_tests rechigros_app_data_tests
    rechigros_skin_tests)
  rechigros_track_headers(${extraTarget})
endforeach()
