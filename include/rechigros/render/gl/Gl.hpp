#pragma once

// Single include point for the OpenGL flavour selected by the build:
// desktop core profile through the loader, ES 3.0 through the platform headers.

#ifdef RECHIGROS_GLES
# ifdef __APPLE__
#  include <OpenGLES/ES3/gl.h>
#  include <OpenGLES/ES3/glext.h>
# else
#  include <GLES3/gl3.h>
# endif
#else
# include <glad/glad.h>
#endif

namespace rechigros::render::gl {

// The shader language version every generated program starts with.
#ifdef RECHIGROS_GLES
inline constexpr const char* SHADER_HEADER = "#version 300 es\n";
#else
inline constexpr const char* SHADER_HEADER = "#version 330 core\n";
#endif

// True once a graphics context has been made current. Screens and asset code may run
// before that happens (a headless frame, a unit test), so every path that would touch a
// graphics entry point checks this first instead of calling through a null pointer.
[[nodiscard]] bool contextAvailable() noexcept;

} // namespace rechigros::render::gl
