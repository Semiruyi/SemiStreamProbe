#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <expected>

namespace semi_stream_probe {

// These value types will grow as the corresponding H.264 syntax is learned.
// Keeping them as data objects prevents the parser from owning stream state.
struct Sps {};
struct Pps {};

// TODO: implement EBSP-to-RBSP conversion and syntax parsing in later
// milestones. These declarations only reserve the module boundary.
[[nodiscard]] std::expected<Sps, ParseError> parse_sps(ByteView rbsp);
[[nodiscard]] std::expected<Pps, ParseError> parse_pps(ByteView rbsp);

} // namespace semi_stream_probe

