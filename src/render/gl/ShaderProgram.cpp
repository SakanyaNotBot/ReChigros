#include "rechigros/render/gl/ShaderProgram.hpp"

#include <cctype>
#include <utility>
#include <vector>

namespace rechigros::render::gl {
namespace {

std::string replaceWord(const std::string& source, const char* needle, const char* replacement) {
  std::string out;
  out.reserve(source.size() + 32);
  std::size_t index = 0;
  const std::size_t length = std::char_traits<char>::length(needle);
  while (index < source.size()) {
    if (source.compare(index, length, needle) == 0) {
      const bool leftOk = index == 0 ||
          !(std::isalnum(static_cast<unsigned char>(source[index - 1])) || source[index - 1] == '_');
      const std::size_t after = index + length;
      const bool rightOk = after >= source.size() ||
          !(std::isalnum(static_cast<unsigned char>(source[after])) || source[after] == '_');
      if (leftOk && rightOk) {
        out += replacement;
        index = after;
        continue;
      }
    }
    out.push_back(source[index++]);
  }
  return out;
}

bool startsWithVersion(const std::string& line) {
  std::size_t index = 0;
  while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
  return line.compare(index, 8, "#version") == 0;
}

bool isBlankOrComment(const std::string& line) {
  std::size_t index = 0;
  while (index < line.size() && std::isspace(static_cast<unsigned char>(line[index]))) ++index;
  return index >= line.size() || (line[index] == '/' && index + 1 < line.size() &&
                                  (line[index + 1] == '/' || line[index + 1] == '*'));
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string current;
  for (const char character : text) {
    if (character == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (character != '\r') {
      current.push_back(character);
    }
  }
  lines.push_back(current);
  return lines;
}

// True when the source already declares the shader's colour output, e.g. a preset
// that names its own out variable.
bool declaresFragmentOutput(const std::string& source) {
  const auto lines = splitLines(source);
  for (const auto& line : lines) {
    std::size_t begin = 0;
    while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) ++begin;
    if (line.compare(begin, 4, "out ") != 0) continue;
    if (line.find(';', begin) != std::string::npos && line.find('{', begin) == std::string::npos) {
      return true;
    }
  }
  return false;
}

} // namespace

std::string prepareShaderSource(bool fragment, const std::string& source) {
  std::string body = replaceWord(source, "attribute", "in");
  body = replaceWord(body, "varying", fragment ? "in" : "out");
  body = replaceWord(body, "texture2D", "texture");
  body = replaceWord(body, "gl_FragColor", "fragmentColor");
  body = replaceWord(body, "gl_FragData[0]", "fragmentColor");

  // Drop any author-supplied version directive: the build context decides it.
  std::string stripped;
  for (const auto& line : splitLines(body)) {
    if (startsWithVersion(line)) continue;
    stripped += line;
    stripped.push_back('\n');
  }

  const bool needsOutput = fragment && !declaresFragmentOutput(stripped);
  std::string out;
#ifdef RECHIGROS_GLES
  out = "#version 300 es\n";
  // GLSL ES requires the default float precision to be established before any
  // declaration. The colour output is a declaration, so the precision statement has
  // to be emitted first, ahead of the injected output variable.
  if (needsOutput && stripped.find("precision ") == std::string::npos) {
    out += "precision highp float;\n";
  }
#else
  out = "#version 330 core\n";
#endif
  if (needsOutput) out += "out vec4 fragmentColor;\n";
  out += stripped;
  return out;
}

ShaderProgram::~ShaderProgram() { reset(); }

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_), locations_(std::move(other.locations_)), log_(std::move(other.log_)) {
  other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
  if (this != &other) {
    reset();
    program_ = other.program_;
    locations_ = std::move(other.locations_);
    log_ = std::move(other.log_);
    other.program_ = 0;
  }
  return *this;
}

void ShaderProgram::reset() noexcept {
  if (program_ != 0) glDeleteProgram(program_);
  program_ = 0;
  locations_.clear();
  log_.clear();
}

namespace {
unsigned int compile(GLenum type, const std::string& source, std::string& log) {
  const unsigned int shader = glCreateShader(type);
  if (shader == 0) {
    log += "glCreateShader failed\n";
    return 0;
  }
  const char* text = source.c_str();
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string info(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
    glGetShaderInfoLog(shader, static_cast<GLsizei>(info.size()), nullptr, info.data());
    log += (type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ");
    log += info;
    log.push_back('\n');
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}
} // namespace

bool ShaderProgram::build(const std::string& vertexSource, const std::string& fragmentSource) noexcept {
  reset();
  const std::string vertex = prepareShaderSource(false, vertexSource);
  const std::string fragment = prepareShaderSource(true, fragmentSource);
  const unsigned int vertexShader = compile(GL_VERTEX_SHADER, vertex, log_);
  const unsigned int fragmentShader = compile(GL_FRAGMENT_SHADER, fragment, log_);
  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader != 0) glDeleteShader(vertexShader);
    if (fragmentShader != 0) glDeleteShader(fragmentShader);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vertexShader);
  glAttachShader(program_, fragmentShader);
  glLinkProgram(program_);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  GLint linked = GL_FALSE;
  glGetProgramiv(program_, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    GLint length = 0;
    glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &length);
    std::string info(static_cast<std::size_t>(length > 1 ? length : 1), '\0');
    glGetProgramInfoLog(program_, static_cast<GLsizei>(info.size()), nullptr, info.data());
    log_ += "link: ";
    log_ += info;
    glDeleteProgram(program_);
    program_ = 0;
    return false;
  }
  return true;
}

void ShaderProgram::use() const noexcept { glUseProgram(program_); }

int ShaderProgram::attribute(const char* name) const {
  if (program_ == 0) return -1;
  const std::string key(name);
  if (const auto it = locations_.find(key); it != locations_.end()) return it->second;
  const int location = glGetAttribLocation(program_, name);
  locations_.emplace(key, location);
  return location;
}

int ShaderProgram::uniform(const char* name) const {
  if (program_ == 0) return -1;
  const std::string key(name);
  if (const auto it = locations_.find(key); it != locations_.end()) return it->second;
  const int location = glGetUniformLocation(program_, key.c_str());
  locations_.emplace(key, location);
  return location;
}

void ShaderProgram::set1f(const char* name, float value) const {
  const int location = uniform(name);
  if (location >= 0) glUniform1f(location, value);
}

void ShaderProgram::set2f(const char* name, float a, float b) const {
  const int location = uniform(name);
  if (location >= 0) glUniform2f(location, a, b);
}

void ShaderProgram::set3f(const char* name, float a, float b, float c) const {
  const int location = uniform(name);
  if (location >= 0) glUniform3f(location, a, b, c);
}

void ShaderProgram::set4f(const char* name, float a, float b, float c, float d) const {
  const int location = uniform(name);
  if (location >= 0) glUniform4f(location, a, b, c, d);
}

void ShaderProgram::set1i(const char* name, int value) const {
  const int location = uniform(name);
  if (location >= 0) glUniform1i(location, value);
}

void ShaderProgram::setMat4(const char* name, const float* value) const {
  const int location = uniform(name);
  if (location >= 0) glUniformMatrix4fv(location, 1, GL_FALSE, value);
}

} // namespace rechigros::render::gl
