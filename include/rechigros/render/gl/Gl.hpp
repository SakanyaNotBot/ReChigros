#pragma once

// Single include point for the OpenGL flavour selected by the build:
// desktop core profile through the loader, ES 3.0 through the platform headers.

#ifdef RECHIGROS_GLES
# ifdef __APPLE__
#  include <OpenGLES/ES3/gl.h>
#  if __has_include(<OpenGLES/ES3/glext.h>)
#   include <OpenGLES/ES3/glext.h>
#  endif
# else
#  include <GLES3/gl3.h>
# endif
#else
# include <glad/glad.h>
#endif

namespace rechigros::render::gl {

#ifdef RECHIGROS_GLES
// The timer query tokens come from the extension header, which recent Apple SDKs may
// omit even though the entry points remain available through SDL_GL_GetProcAddress.
# ifndef GL_TIME_ELAPSED_EXT
#  define GL_TIME_ELAPSED_EXT 0x88BF
# endif
# ifndef GL_GPU_DISJOINT_EXT
#  define GL_GPU_DISJOINT_EXT 0x8FBB
# endif
#endif

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

// The framebuffer the window presents from. A desktop GL context and an EGL surface draw
// into framebuffer 0, but the UIKit OpenGL view renders into a framebuffer object of its
// own: binding 0 there draws into an object with no attachments, and the renderbuffer the
// layer presents keeps whatever it had. The application publishes the value SDL reports for
// the window once the context exists, and every "draw to the screen" path binds this instead
// of a literal zero.
[[nodiscard]] unsigned int screenFramebuffer() noexcept;
void setScreenFramebuffer(unsigned int framebuffer) noexcept;
void bindScreenFramebuffer() noexcept;

} // namespace rechigros::render::gl
