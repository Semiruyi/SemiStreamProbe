#include "semi_stream_probe/core/annex_b.hpp"

#include <optional>
#include <utility>

namespace semi_stream_probe {

namespace {

struct StartCode {
    std::size_t offset;
    std::size_t size;
};

[[nodiscard]] std::optional<StartCode>
find_start_code(ByteView bytes, std::size_t begin) noexcept {
    for (std::size_t index = begin; index + 2 < bytes.size(); ++index) {
        if (bytes[index] != 0 || bytes[index + 1] != 0) {
            continue;
        }

        if (bytes[index + 2] == 1) {
            return StartCode{.offset = index, .size = 3};
        }

        if (index + 3 < bytes.size() && bytes[index + 2] == 0 &&
            bytes[index + 3] == 1) {
            return StartCode{.offset = index, .size = 4};
        }
    }

    return std::nullopt;
}

[[nodiscard]] ParseError make_scan_error(ParseErrorCode code,
                                         std::size_t offset,
                                         std::size_t nal_index,
                                         std::string message) {
    return ParseError{
        .code = code,
        .byte_offset = offset,
        .nal_index = nal_index,
        .message = std::move(message),
    };
}

} // namespace

std::expected<std::vector<NalUnitRef>, ParseError>
scan_annex_b(ByteView bytes) {
    const auto first = find_start_code(bytes, 0);
    if (!first) {
        return std::unexpected(make_scan_error(
            ParseErrorCode::start_code_not_found, 0, 0,
            "Annex-B start code was not found"));
    }

    for (std::size_t index = 0; index < first->offset; ++index) {
        if (bytes[index] != 0) {
            return std::unexpected(make_scan_error(
                ParseErrorCode::invalid_input, index, 0,
                "non-zero data appears before the first Annex-B start code"));
        }
    }

    std::vector<NalUnitRef> units;
    auto current = *first;

    while (true) {
        const auto payload_offset = current.offset + current.size;
        const auto next = find_start_code(bytes, payload_offset);
        auto payload_end = next ? next->offset : bytes.size();

        while (payload_end > payload_offset && bytes[payload_end - 1] == 0) {
            --payload_end;
        }

        if (payload_end == payload_offset) {
            return std::unexpected(make_scan_error(
                ParseErrorCode::empty_nal_unit, payload_offset, units.size(),
                "Annex-B start code is not followed by a NAL unit"));
        }

        units.push_back(NalUnitRef{
            .index = units.size(),
            .start_code_offset = current.offset,
            .start_code_size = current.size,
            .payload_offset = payload_offset,
            .payload_size = payload_end - payload_offset,
        });

        if (!next) {
            break;
        }
        current = *next;
    }

    return units;
}

} // namespace semi_stream_probe

