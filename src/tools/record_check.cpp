// End-to-end check for the video encoder: it feeds synthetic frames through the same path a
// recording uses and verifies that a playable file comes out. Isolating the encoder from the
// renderer means a failure here points at the encode step rather than at the picture.
//
// Usage: rechigros_record_check <output-dir> [<encoder>]

#include "rechigros/app/VideoEncoder.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
  std::printf("%s  %s\n", condition ? "PASS" : "FAIL", message);
  if (!condition) ++failures;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
    return 2;
  }
  const std::filesystem::path outputDir = argv[1];
  std::error_code errorCode;
  std::filesystem::create_directories(outputDir, errorCode);

  const auto encoderPath = rechigros::app::VideoEncoder::findEncoder();
  std::printf("encoder: %s\n", encoderPath.string().c_str());

  rechigros::app::VideoEncoder::Settings settings;
  settings.width = 320;
  settings.height = 240;
  settings.fps = 30;
  settings.codec = rechigros::app::VideoEncoder::Codec::H264;
  settings.quality = 23;

  rechigros::app::VideoEncoder encoder;
  std::string error;
  const auto output = outputDir / "record-check.mp4";
  std::filesystem::remove(output, errorCode);
  check(encoder.open(settings, output, error), "the encoder starts");
  if (failures != 0) {
    std::printf("      reason: %s\n", error.c_str());
    return 1;
  }

  // The encoder has to accept exactly the byte count the frame size implies; a mismatch would
  // shift every following frame and corrupt the file.
  std::vector<std::uint8_t> frame(static_cast<std::size_t>(settings.width)
      * static_cast<std::size_t>(settings.height) * 4U, 0U);
  bool wroteAll = true;
  for (int index = 0; index < 30; ++index) {
    for (std::size_t pixel = 0; pixel < frame.size(); pixel += 4) {
      frame[pixel + 0] = static_cast<std::uint8_t>(index * 8);
      frame[pixel + 1] = 128;
      frame[pixel + 2] = 64;
      frame[pixel + 3] = 255;
    }
    if (!encoder.writeFrame(frame, error)) {
      std::printf("      frame %d was rejected: %s\n", index, error.c_str());
      wroteAll = false;
      break;
    }
  }
  check(wroteAll, "every frame is accepted");
  check(encoder.finish(error), "the encoder finishes cleanly");
  check(std::filesystem::exists(output), "the video file exists");
  const auto size = std::filesystem::file_size(output, errorCode);
  check(!errorCode && size > 0, "the video file is not empty");
  std::printf("output: %s (%llu bytes)\n", output.string().c_str(),
      static_cast<unsigned long long>(size));

  if (failures != 0) {
    std::fprintf(stderr, "%d recording check(s) failed\n", failures);
    return 1;
  }
  std::printf("recording encoder check passed\n");
  return 0;
}
