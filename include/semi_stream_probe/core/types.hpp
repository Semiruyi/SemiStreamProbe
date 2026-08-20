#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace semi_stream_probe {

using Byte = std::uint8_t;
using ByteView = std::span<const Byte>;
using ByteBuffer = std::vector<Byte>;

} // namespace semi_stream_probe

