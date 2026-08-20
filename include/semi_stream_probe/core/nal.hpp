#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <expected>

namespace semi_stream_probe {

struct NalHeader {
    bool forbidden_zero_bit{false};
    std::uint8_t nal_ref_idc{0};
    std::uint8_t nal_unit_type{0};
};

// Parses the first byte of a NAL unit after framing has identified its payload.
[[nodiscard]] std::expected<NalHeader, ParseError>
parse_nal_header(ByteView nal_unit);

[[nodiscard]] const char* nal_unit_type_name(std::uint8_t nal_unit_type) noexcept;

} // namespace semi_stream_probe

