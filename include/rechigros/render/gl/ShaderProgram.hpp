#pragma once

#include "rechigros/render/gl/Gl.hpp"

#include <string>
#include <unordered_map>

namespace rechigros::render::gl {

// Compiled and linked program with a lazily filled uniform location cache.
class ShaderProgram final {
public:
  ShaderProgram() = default;
  ~ShaderProgram();
  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(const ShaderProgram&) = delete;
  ShaderProgram(ShaderProgram&& other) noexcept;
  ShaderProgram& operator=(ShaderProgram&& other) noexcept;

  // Both sources are passed through prepareSource() first, so callers may write
  // either legacy ES 1.00 style or fully versioned modern GLSL.
  bool build(const std::string& vertexSource, const std::string& fragmentSource) noexcept;
  void reset() noexcept;

  void use() const noexcept;
  [[nodiscard]] unsigned int id() const noexcept { return program_; }
  [[nodiscard]] bool valid() const noexcept { return program_ != 0; }

  [[nodiscard]] int attribute(const char* name) const;
  [[nodiscard]] int uniform(const char* name) const;

  void set1f(const char* name, float value) const;
  void set2f(const char* name, float a, float b) const;
  void set3f(const char* name, float a, float b, float c) const;
  void set4f(const char* name, float a, float b, float c, float d) const;
  void set1i(const char* name, int value) const;
  // Uploads `count` vec2 values to a uniform array such as `touchPos[]`.
  void set2fv(const char* name, int count, const float* values) const;
  void setMat4(const char* name, const float* value) const;

  // Last compile or link diagnostic, for startup logging.
  [[nodiscard]] const std::string& log() const noexcept { return log_; }

private:
  unsigned int program_{};
  mutable std::unordered_map<std::string, int> locations_;
  std::string log_;
};

// Normalises a shader body to the context version: strips any existing #version,
// rewrites legacy attribute/varying/texture2D/gl_FragColor spellings and injects
// the version header plus a fragment output declaration when needed.
[[nodiscard]] std::string prepareShaderSource(bool fragment, const std::string& source);

// Same, for an explicitly chosen language family. The ES branch is reachable from a
// desktop host so a build-time check can validate the device form of every shader
// without an ES context.
[[nodiscard]] std::string prepareShaderSourceForLanguage(bool fragment, bool esProfile,
    const std::string& source);

} // namespace rechigros::render::gl
