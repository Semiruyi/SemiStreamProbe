#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace semi_stream_probe {

enum class ParseErrorCode {
    not_implemented,
    invalid_input,
    start_code_not_found,
    empty_nal_unit,
    forbidden_zero_bit_set,
    invalid_bit_count,
    unexpected_end_of_data,
    exp_golomb_overflow,
    invalid_sps,
    invalid_pps,
    invalid_slice,
    invalid_rtp,
    invalid_h264_rtp_payload,
    parameter_set_not_found,
    io_error,
};

struct ParseError {
    ParseErrorCode code{ParseErrorCode::invalid_input};
    std::size_t byte_offset{0};
    std::size_t bit_offset{0};
    std::size_t nal_index{0};
    std::optional<std::uint16_t> rtp_sequence_number{std::nullopt};
    std::string message;
};

[[nodiscard]] std::string_view to_string(ParseErrorCode code) noexcept;

[[nodiscard]] ParseError make_not_implemented_error(std::string_view component);

} // namespace semi_stream_probe

