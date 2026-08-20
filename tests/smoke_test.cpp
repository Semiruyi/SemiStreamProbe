#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/core/annex_b.hpp"
#include "semi_stream_probe/core/bit_reader.hpp"
#include "semi_stream_probe/core/h264_syntax.hpp"
#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstdint>
#include <iostream>

int main() {
    static_assert(sizeof(semi_stream_probe::Byte) == 1);

    const semi_stream_probe::Byte bytes[] = {0x00, 0x00, 0x01};
    const semi_stream_probe::ByteView view(bytes);
    const semi_stream_probe::BitReader reader(view);

    if (reader.bytes().size() != 3 || reader.bit_position() != 0) {
        std::cerr << "basic type smoke test failed\n";
        return 1;
    }

    std::cout << "semi_stream_probe_smoke_tests: scaffold OK\n";
    return 0;
}

