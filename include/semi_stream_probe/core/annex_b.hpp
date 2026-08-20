#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <expected>
#include <vector>

namespace semi_stream_probe {

// A non-owning description of one NAL unit inside an Annex-B byte buffer.
// The referenced bytes remain owned by the caller of scan_annex_b().
struct NalUnitRef {
    std::size_t index{0};
    std::size_t start_code_offset{0};
    std::size_t start_code_size{0};
    std::size_t payload_offset{0};
    std::size_t payload_size{0};
};

// Finds three-byte and four-byte Annex-B start codes. Leading and trailing
// zero bytes are framing bytes and are not included in a NAL payload.
[[nodiscard]] std::expected<std::vector<NalUnitRef>, ParseError>
scan_annex_b(ByteView bytes);

} // namespace semi_stream_probe

