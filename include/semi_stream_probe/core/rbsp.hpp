#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <expected>

namespace semi_stream_probe {

// Converts the EBSP bytes after a NAL header into RBSP bytes. An
// emulation_prevention_three_byte (0x03) is removed only when it follows two
// zero bytes and the following byte is in the range 0x00..0x03.
[[nodiscard]] std::expected<ByteBuffer, ParseError>
ebsp_to_rbsp(ByteView ebsp);

} // namespace semi_stream_probe
