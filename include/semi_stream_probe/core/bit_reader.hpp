#pragma once

#include "semi_stream_probe/core/types.hpp"

#include <cstddef>

namespace semi_stream_probe {

// Deliberately small placeholder for the bit-level reader used by RBSP,
// Exp-Golomb, SPS, PPS and Slice parsing in later milestones.
class BitReader {
public:
    explicit BitReader(ByteView bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] ByteView bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t bit_position() const noexcept { return bit_position_; }

private:
    ByteView bytes_;
    std::size_t bit_position_{0};
};

} // namespace semi_stream_probe

