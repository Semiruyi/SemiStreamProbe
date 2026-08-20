#include "semi_stream_probe/core/annex_b.hpp"

namespace semi_stream_probe {

std::expected<std::vector<NalUnitRef>, ParseError>
scan_annex_b(ByteView /*bytes*/) {
    return std::unexpected(make_not_implemented_error("Annex-B scanner"));
}

} // namespace semi_stream_probe

