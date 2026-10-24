# ObjectivelyMVC and its two support libraries, built from the sources vendored under
# third_party/objectivelymvc.
#
# The upstream projects ship autotools and Visual Studio projects only, so the source list
# is enumerated here. All three are GNU C: they use zero-length array members, statement
# expressions and __ATOMIC_* builtins. CMake drives one C compiler per tree, and on a
# Windows host that compiler is MSVC, which rejects all of the above; so when the host is
# MSVC the three libraries are compiled by a separate clang invocation into static
# archives and imported. With any other host toolchain they are ordinary CMake targets.

set(OMVC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/objectivelymvc")

# Upstream generates Config.h from configure.ac. Only the release version and the POSIX
# header probes are consumed, so it is written here instead of running autotools.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/omvc-include")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/omvc-include/Config.h"
  "#pragma once\n"
  "#define PACKAGE_VERSION \"2.5.3\"\n"
  "#define RELEASE_VERSION \"2\"\n"
  "#define HAVE_PTHREAD_H 1\n"
  "#define HAVE_UNISTD_H 1\n"
  "#define HAVE_SYS_TYPES_H 1\n")

set(OMVC_INCLUDES
  "${CMAKE_CURRENT_BINARY_DIR}/omvc-include"
  "${OMVC_ROOT}/objectively-src"
  "${OMVC_ROOT}/objectivelygpu-src"
  "${OMVC_ROOT}/objectivelymvc-src")

# The include directories the application needs to reach the framework headers.
set(OMVC_PUBLIC_INCLUDES ${OMVC_INCLUDES} "${OMVC_ROOT}/objectively-src")

# Include roots for the headers the framework includes as <SDL3/...>, <SDL3_ttf/...> and
# <SDL3_image/...>. They are read from the CMake targets so a different SDL source layout
# still works.
set(OMVC_DEP_INCLUDES)
foreach(dep_target SDL3::Headers SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)
  if(TARGET ${dep_target})
    get_target_property(dep_includes ${dep_target} INTERFACE_INCLUDE_DIRECTORIES)
    if(dep_includes)
      foreach(dep_include IN LISTS dep_includes)
        if(IS_DIRECTORY "${dep_include}")
          list(APPEND OMVC_DEP_INCLUDES "${dep_include}")
        else()
          # Generator expressions and build-relative paths cannot be resolved here; the
          # fallbacks below cover the layouts this project actually uses.
        endif()
      endforeach()
    endif()
  endif()
endforeach()
if(TARGET SDL3::Headers)
  get_target_property(sdl_include_dirs SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
  if(sdl_include_dirs)
    foreach(sdl_include IN LISTS sdl_include_dirs)
      if(IS_DIRECTORY "${sdl_include}" AND NOT sdl_include IN_LIST OMVC_DEP_INCLUDES)
        list(APPEND OMVC_DEP_INCLUDES "${sdl_include}")
      endif()
    endforeach()
  endif()
endif()
# The CMake targets expose their include directories through generator expressions, which
# cannot be expanded while configuring this list. The layouts this project can produce are
# known, so they are added directly: a local SDL3 source tree, the fetched build copy, and
# the two fetched extension trees.
foreach(candidate
    "${PROJECT_SOURCE_DIR}/third_party/SDL3/include"
    "${CMAKE_BINARY_DIR}/third_party/SDL3/include-revision"
    "${CMAKE_BINARY_DIR}/third_party/SDL3/include"
    "${RECHIGROS_DOWNLOAD_DIR}/sdl3_ttf/SDL3_ttf-3.2.2/include"
    "${RECHIGROS_DOWNLOAD_DIR}/sdl3_image/SDL3_image-3.2.4/include")
  if(IS_DIRECTORY "${candidate}" AND NOT candidate IN_LIST OMVC_DEP_INCLUDES)
    list(APPEND OMVC_DEP_INCLUDES "${candidate}")
  endif()
endforeach()

list(REMOVE_DUPLICATES OMVC_DEP_INCLUDES)

set(OMVC_GNU_C_TOOLCHAIN FALSE)
if(MSVC)
  if(RECHIGROS_GNU_C_COMPILER AND EXISTS "${RECHIGROS_GNU_C_COMPILER}")
    set(OMVC_GNU_C_TOOLCHAIN TRUE)
  else()
    message(FATAL_ERROR
      "The interface framework is GNU C, which MSVC cannot compile. "
      "Set -DRECHIGROS_GNU_C_COMPILER=<path to clang.exe> or build with a GNU toolchain.")
  endif()
endif()

# build_gnu_c_library(<target> <include dirs...>)
#   On the clang path: compiles every source into an object, archives them, and exposes the
#   archive as an INTERFACE library so the rest of the tree links exactly like a normal
#   static library.
function(build_gnu_c_library omvc_target)
  file(GLOB omvc_sources CONFIGURE_DEPENDS
    "${OMVC_ROOT}/${ARGN}" )
  set(omvc_include_args)
  foreach(include IN LISTS OMVC_INCLUDES)
    list(APPEND omvc_include_args "-I${include}")
  endforeach()
  # The libraries include SDL3, SDL3_ttf and SDL3_image headers by their canonical
  # <SDL3/...> spelling, so their include roots are passed alongside the framework's own.
  foreach(dep_include IN LISTS OMVC_DEP_INCLUDES)
    list(APPEND omvc_include_args "-I${dep_include}")
  endforeach()
  # A GNU-compatible toolchain builds the sources directly. On Windows this is the WSL GCC
  # or clang path, because MSVC's C front end rejects the GNU extensions these libraries
  # use and its Windows SDK headers redefine `interface`, which is a field name here.
  if(NOT MSVC)
    add_library(${omvc_target} STATIC ${omvc_sources})
    target_include_directories(${omvc_target} PUBLIC ${OMVC_PUBLIC_INCLUDES})
    target_compile_options(${omvc_target} PRIVATE -w)
    set_target_properties(${omvc_target} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    return()
  endif()
  # MSVC hosts without a clang that can escape the Windows SDK headers cannot build this
  # framework at all; the error names the switch to set instead of failing much later.
  if(NOT RECHIGROS_GNU_C_COMPILER)
    message(FATAL_ERROR
      "The interface framework is GNU C and cannot be built by MSVC. "
      "Configure with -DRECHIGROS_GNU_C_COMPILER pointing at a clang/wsl gcc, "
      "or set RECHIGROS_OMVC=OFF to build without it.")
  endif()
  set(omvc_objdir "${CMAKE_CURRENT_BINARY_DIR}/omvc-objects/${omvc_target}")
  set(omvc_objects)
  foreach(source IN LISTS omvc_sources)
    get_filename_component(source_dir "${source}" DIRECTORY)
    get_filename_component(source_name "${source}" NAME)
    string(RANDOM LENGTH 8 salt)
    string(MD5 stamp "${source}")
    set(object "${omvc_objdir}/${stamp}.obj")
    add_custom_command(
      OUTPUT "${object}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${omvc_objdir}"
      COMMAND "${RECHIGROS_GNU_C_COMPILER}" -c -w -O2 -fno-strict-aliasing -D_GNU_SOURCE
              -std=gnu11 ${omvc_include_args} "${source}" -o "${object}"
      DEPENDS "${source}"
      COMMENT "GNU C ${source_name}"
      VERBATIM)
    list(APPEND omvc_objects "${object}")
  endforeach()
  set(omvc_archive "${omvc_objdir}/lib${omvc_target}.lib")
  # CMAKE_AR on an MSVC host is lib.exe, which has no "qc" operation; it takes the output
  # archive followed by the objects and creates the archive itself.
  add_custom_command(
    OUTPUT "${omvc_archive}"
    COMMAND "${CMAKE_AR}" /nologo /OUT:${omvc_archive} ${omvc_objects}
    COMMAND_EXPAND_LISTS
    DEPENDS ${omvc_objects}
    COMMENT "Archive ${omvc_target}"
    VERBATIM)
  add_custom_target(${omvc_target}-objects DEPENDS "${omvc_archive}")
  add_library(${omvc_target} INTERFACE)
  add_dependencies(${omvc_target} ${omvc_target}-objects)
  target_include_directories(${omvc_target} INTERFACE ${OMVC_PUBLIC_INCLUDES})
  target_link_libraries(${omvc_target} INTERFACE "${omvc_archive}")
endfunction()

build_gnu_c_library(objectively objectively-src/Objectively/*.c)
# The Windows-only Windowly shim lives beside the Visual Studio project, not in Sources.
if(MSVC)
  target_include_directories(objectively INTERFACE "${OMVC_ROOT}/objectively-src")
endif()

build_gnu_c_library(objectivelygpu objectivelygpu-src/ObjectivelyGPU/*.c)
build_gnu_c_library(objectivelymvc objectivelymvc-src/ObjectivelyMVC/*.c)

target_link_libraries(objectivelygpu INTERFACE ${RECHIGROS_SDL_TARGET})
target_link_libraries(objectivelymvc INTERFACE objectivelygpu SDL3_ttf::SDL3_ttf SDL3_image::SDL3_image)

# dladdr (used by class introspection) lives in libdl on ELF platforms.
if(UNIX AND NOT APPLE AND NOT ANDROID)
  target_link_libraries(objectively INTERFACE dl)
endif()
