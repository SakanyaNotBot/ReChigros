#include "rechigros/render/gl/GlDebug.hpp"

#include "rechigros/render/gl/Gl.hpp"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstring>

namespace rechigros::render::gl {
namespace {

bool g_enabled = false;

// Most stages report at most a handful of distinct status codes over a whole run, so a
// fixed table is enough and avoids allocating on a render thread.
constexpr int kMaxStages = 48;
constexpr int kMaxCodesPerStage = 8;
struct StageRecord {
  char name[48];
  unsigned int codes[kMaxCodesPerStage];
  int codeCount;
};
StageRecord g_stages[kMaxStages];
int g_stageCount = 0;

const char* statusName(unsigned int code) {
  switch (code) {
    case 0x0500: return "INVALID_ENUM";
    case 0x0501: return "INVALID_VALUE";
    case 0x0502: return "INVALID_OPERATION";
    case 0x0503: return "STACK_OVERFLOW";
    case 0x0504: return "STACK_UNDERFLOW";
    case 0x0505: return "OUT_OF_MEMORY";
    case 0x0506: return "INVALID_FRAMEBUFFER_OPERATION";
    default: return "UNKNOWN";
  }
}

StageRecord* recordFor(const char* stage) {
  for (int index = 0; index < g_stageCount; ++index) {
    if (std::strcmp(g_stages[index].name, stage) == 0) return &g_stages[index];
  }
  if (g_stageCount >= kMaxStages) return nullptr;
  StageRecord* record = &g_stages[g_stageCount++];
  std::snprintf(record->name, sizeof(record->name), "%s", stage);
  record->codeCount = 0;
  return record;
}

void report(const char* stage, unsigned int code, bool repeated) {
  // The log has to go through the platform log on a device: the standard streams are
  // not connected to anything there, and the point of this mode is to read the report
  // back from a connected device.
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[gl] stage=%s error=0x%04x (%s)%s", stage, code,
      statusName(code), repeated ? " repeated" : "");
}

} // namespace

void setErrorLoggingEnabled(bool enabled) noexcept { g_enabled = enabled; }

bool errorLoggingEnabled() noexcept { return g_enabled; }

void logErrors(const char* stage) noexcept {
  if (!g_enabled || stage == nullptr) return;
  StageRecord* record = recordFor(stage);
  for (int guard = 0; guard < 32; ++guard) {
    const unsigned int code = glGetError();
    if (code == 0) break;
    if (record == nullptr) continue;
    bool seen = false;
    for (int index = 0; index < record->codeCount; ++index) {
      if (record->codes[index] == code) {
        seen = true;
        break;
      }
    }
    if (!seen && record->codeCount < kMaxCodesPerStage) {
      record->codes[record->codeCount++] = code;
      report(stage, code, false);
    } else if (seen) {
      report(stage, code, true);
    }
  }
}

void logContextInfo() noexcept {
  if (!g_enabled) return;
  const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  const auto* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  const auto* language = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[gl] version=%s renderer=%s vendor=%s glsl=%s",
      version != nullptr ? version : "?", renderer != nullptr ? renderer : "?",
      vendor != nullptr ? vendor : "?", language != nullptr ? language : "?");
  // Draining here keeps the caller's first real stage check meaningful: a context that
  // was created imperfectly reports its own failure before any drawing happens.
  logErrors("context");
}

} // namespace rechigros::render::gl
