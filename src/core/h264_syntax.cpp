#include "semi_stream_probe/core/h264_syntax.hpp"

namespace semi_stream_probe {

std::expected<Sps, ParseError> parse_sps(ByteView /*rbsp*/) {
    return std::unexpected(make_not_implemented_error("SPS parser"));
}

std::expected<Pps, ParseError> parse_pps(ByteView /*rbsp*/) {
    return std::unexpected(make_not_implemented_error("PPS parser"));
}

} // namespace semi_stream_probe

