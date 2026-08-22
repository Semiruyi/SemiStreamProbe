#include "semi_stream_probe/core/rbsp.hpp"

namespace semi_stream_probe {

std::expected<ByteBuffer, ParseError> ebsp_to_rbsp(ByteView ebsp) {
    ByteBuffer rbsp;
    rbsp.reserve(ebsp.size());

    std::size_t consecutive_zero_bytes = 0;
    for (std::size_t index = 0; index < ebsp.size(); ++index) {
        const Byte value = ebsp[index];

        if (value == 0x03 && consecutive_zero_bytes == 2 &&
            index + 1 < ebsp.size() && ebsp[index + 1] <= 0x03) {
            // Reset the raw zero run. The next byte is the RBSP byte that the
            // prevention byte protected; it must be processed normally.
            consecutive_zero_bytes = 0;
            continue;
        }

        rbsp.push_back(value);
        if (value == 0x00) {
            if (consecutive_zero_bytes < 2) {
                ++consecutive_zero_bytes;
            }
        } else {
            consecutive_zero_bytes = 0;
        }
    }

    return rbsp;
}

} // namespace semi_stream_probe
