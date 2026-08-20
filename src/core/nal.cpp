#include "semi_stream_probe/core/nal.hpp"

namespace semi_stream_probe {

namespace {

constexpr Byte forbidden_zero_bit_mask = 0b1000'0000;
constexpr Byte nal_ref_idc_mask = 0b0110'0000;
constexpr Byte nal_unit_type_mask = 0b0001'1111;
constexpr unsigned int nal_ref_idc_shift = 5U;

} // namespace

std::expected<NalHeader, ParseError>
parse_nal_header(ByteView nal_unit) {
    if (nal_unit.empty()) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::empty_nal_unit,
            .message = "NAL unit does not contain a header byte",
        });
    }

    const auto value = nal_unit.front();
    if ((value & forbidden_zero_bit_mask) != 0) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::forbidden_zero_bit_set,
            .message = "NAL forbidden_zero_bit must be zero",
        });
    }

    return NalHeader{
        .forbidden_zero_bit = false,
        .nal_ref_idc = static_cast<std::uint8_t>(
            (value & nal_ref_idc_mask) >> nal_ref_idc_shift),
        .nal_unit_type =
            static_cast<std::uint8_t>(value & nal_unit_type_mask),
    };
}

const char* nal_unit_type_name(std::uint8_t nal_unit_type) noexcept {
    switch (nal_unit_type) {
    case 1:
        return "NON_IDR_SLICE";
    case 5:
        return "IDR_SLICE";
    case 6:
        return "SEI";
    case 7:
        return "SPS";
    case 8:
        return "PPS";
    case 9:
        return "AUD";
    default:
        return "OTHER";
    }
}

} // namespace semi_stream_probe

