#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace semi_stream_probe {

enum class ParseErrorCode {
    not_implemented,
    invalid_input,
    start_code_not_found,
    empty_nal_unit,
    forbidden_zero_bit_set,
    io_error,
};

struct ParseError {
    ParseErrorCode code{ParseErrorCode::invalid_input};
    std::size_t byte_offset{0};
    std::size_t bit_offset{0};
    std::size_t nal_index{0};
    std::string message;
};

[[nodiscard]] std::string_view to_string(ParseErrorCode code) noexcept;

[[nodiscard]] ParseError make_not_implemented_error(std::string_view component);

} // namespace semi_stream_probe

