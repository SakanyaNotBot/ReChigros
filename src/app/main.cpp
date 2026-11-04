#include "Application.hpp"

#include "rechigros/app/SelfTest.hpp"

#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

int main(int argc, char** argv) {
  for (int index = 1; index < argc; ++index) {
    const char* argument = argv[index];
    if (argument == nullptr) continue;
    if (std::strcmp(argument, "--self-test") == 0) return rechigros::app::runSelfTest();
    if (std::strcmp(argument, "--help") == 0 || std::strcmp(argument, "-h") == 0) {
      std::printf("ReChigros\n");
      std::printf("  ReChigros [chart-or-package]\n");
      std::printf("  ReChigros --self-test            headless end-to-end check\n");
      std::printf("  ReChigros <chart> --frames 240   hidden-window loop for N frames\n");
      std::printf("    --expect-pause                 verify pause/resume state machine\n");
      std::printf("    --expect-result                verify gameplay settles into results\n");
      std::printf("    --record-replay <file>         write the run's replay file\n");
      std::printf("    --replay-playback <file>       replay a file through the touch pipeline\n");
      return 0;
    }
  }
  try {
    return rechigros::app::Application{argc, argv}.run();
  } catch (const std::exception& error) {
    // A failure thrown out of the startup path would otherwise terminate the process without
    // a word, which on a packaged platform is indistinguishable from a crash loop.
    std::fprintf(stderr, "fatal: %s\n", error.what());
  } catch (...) {
    std::fprintf(stderr, "fatal: unknown exception\n");
  }
  return 1;
}
