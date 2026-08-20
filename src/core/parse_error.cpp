#include "semi_stream_probe/core/parse_error.hpp"

namespace semi_stream_probe {

std::string_view to_string(ParseErrorCode code) noexcept {
    switch (code) {
    case ParseErrorCode::not_implemented:
        return "not_implemented";
    case ParseErrorCode::invalid_input:
        return "invalid_input";
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

