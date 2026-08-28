#include "semi_stream_probe/core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

template <std::size_t Size>
void run(const std::array<semi_stream_probe::Byte, Size>& input) {
    if (LLVMFuzzerTestOneInput(input.data(), input.size()) != 0) {
        std::cerr << "fuzz entry point returned a failure\n";
        std::exit(1);
    }
}

} // namespace

int main() {
    // Fixed regression corpus: empty/random input, framing failures, malformed
    // EBSP, and truncated parameter-set/slice NAL units.
    run(std::array<semi_stream_probe::Byte, 0>{});
    run(std::array<semi_stream_probe::Byte, 5>{0x12, 0x34, 0x56, 0x78, 0x9A});
    run(std::array<semi_stream_probe::Byte, 4>{0x00, 0x00, 0x00, 0x01});
    run(std::array<semi_stream_probe::Byte, 7>{
        0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x67});
    run(std::array<semi_stream_probe::Byte, 7>{
        0x00, 0x00, 0x01, 0x67, 0x00, 0x00, 0x02});
    run(std::array<semi_stream_probe::Byte, 4>{0x00, 0x00, 0x01, 0x67});
    run(std::array<semi_stream_probe::Byte, 5>{0x00, 0x00, 0x01, 0x68, 0x80});
    run(std::array<semi_stream_probe::Byte, 5>{0x00, 0x00, 0x01, 0x65, 0x80});
    run(std::array<semi_stream_probe::Byte, 14>{
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00,
        0x1E, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x06});

    std::cout << "semi_stream_probe_annex_b_fuzz_smoke_tests: corpus passed\n";
    return 0;
}
