#pragma once

#include "rechigros/render/asset/ImageCache.hpp"

#include <cstddef>
#include <cstdint>

namespace rechigros::render::asset {

// Decodes a RIFF/WebP payload. The image cache calls this when the regular decoder rejects
// a file; some packs store WebP bytes under a .png name, so the extension cannot decide.
[[nodiscard]] Image decodeWebpMemory(const std::uint8_t* data, std::size_t size);

} // namespace rechigros::render::asset
