#include "semi_stream_probe/core/nal.hpp"

namespace semi_stream_probe {

std::expected<NalHeader, ParseError>
parse_nal_header(ByteView /*nal_unit*/) {
    return std::unexpected(make_not_implemented_error("NAL header parser"));
}

} // namespace semi_stream_probe

