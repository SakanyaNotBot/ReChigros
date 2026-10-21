#pragma once

#include <filesystem>
#include <string>

namespace rechigros::test {

// A writable scratch directory for a test run.
//
// The system temporary directory is not used on purpose: resolving it throws when the
// platform cannot represent the path in the narrow encoding, which happens for a user
// profile containing characters outside the ANSI code page. A directory under the current
// working directory is always resolvable, and the runner already runs inside the build
// tree.
[[nodiscard]] inline std::filesystem::path scratchDirectory(const std::string& name) {
  std::error_code error;
  auto base = std::filesystem::current_path(error);
  if (error) base = std::filesystem::path(".");
  auto path = base / "test-scratch" / name;
  std::filesystem::create_directories(path, error);
  return path;
}

} // namespace rechigros::test
