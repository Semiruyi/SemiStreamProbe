#include "semi_stream_probe/core/parse_error.hpp"

namespace semi_stream_probe {

std::string_view to_string(ParseErrorCode code) noexcept {
    switch (code) {
    case ParseErrorCode::not_implemented:
        return "not_implemented";
    case ParseErrorCode::invalid_input:
        return "invalid_input";
    case ParseErrorCode::start_code_not_found:
        return "start_code_not_found";
    case ParseErrorCode::empty_nal_unit:
        return "empty_nal_unit";
    case ParseErrorCode::forbidden_zero_bit_set:
        return "forbidden_zero_bit_set";
    case ParseErrorCode::invalid_bit_count:
        return "invalid_bit_count";
    case ParseErrorCode::unexpected_end_of_data:
        return "unexpected_end_of_data";
    case ParseErrorCode::exp_golomb_overflow:
        return "exp_golomb_overflow";
    case ParseErrorCode::invalid_sps:
        return "invalid_sps";
    case ParseErrorCode::invalid_pps:
        return "invalid_pps";
    case ParseErrorCode::invalid_slice:
        return "invalid_slice";
    case ParseErrorCode::invalid_rtp:
        return "invalid_rtp";
    case ParseErrorCode::parameter_set_not_found:
        return "parameter_set_not_found";
    case ParseErrorCode::io_error:
        return "io_error";
    }

    return "unknown";
}

ParseError make_not_implemented_error(std::string_view component) {
    return ParseError{
        .code = ParseErrorCode::not_implemented,
        .message = std::string(component) + " is not implemented yet",
    };
}

} // namespace semi_stream_probe

