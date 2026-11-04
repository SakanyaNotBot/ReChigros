// Standalone probe for the blocked-combo mask.
//
// The mask is the part of the failed-challenge readout that cannot be checked by looking at
// a frame: a mask that fails to build leaves an empty field, which a screenshot shows as a
// merely missing effect. This probe prints the shape of the mask for a few strings so the
// glyph coverage can be confirmed directly.
//
// It links against the render library and needs no GL context because the mask is produced
// on the CPU.

#include "rechigros/render/fx/BlockedComboField.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  const std::string assetRoot = argc > 1 ? argv[1] : "assets";

  rechigros::render::fx::BlockedComboField field;
  const char* samples[] = {"0", "123", "1024", "COMBO"};
  int failures = 0;
  for (const char* text : samples) {
    field.prepare(assetRoot, text, 70.0F);
    if (!field.valid()) {
      std::printf("mask \"%s\": NOT BUILT\n", text);
      ++failures;
      continue;
    }
    const int covered = field.coveredCells();
    std::printf("mask \"%s\": %dx%d cells, %d covered\n", text, field.width(), field.height(),
        covered);
    if (covered <= 0) {
      std::printf("  FAIL: a mask with no covered cell would draw nothing\n");
      ++failures;
    }
  }
  std::printf("%s\n", failures == 0 ? "BLOCKED-COMBO PASS" : "BLOCKED-COMBO FAIL");
  return failures == 0 ? 0 : 1;
}
