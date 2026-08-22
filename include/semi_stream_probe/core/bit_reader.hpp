#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>

namespace semi_stream_probe {

// Reads H.264 syntax bits most-significant bit first from an RBSP byte view.
class BitReader {
public:
    explicit BitReader(ByteView bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] ByteView bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::size_t bit_position() const noexcept { return bit_position_; }
    [[nodiscard]] std::size_t bits_remaining() const noexcept;

    [[nodiscard]] std::expected<bool, ParseError> read_bit();
    [[nodiscard]] std::expected<std::uint32_t, ParseError>
    read_bits(std::size_t count);

    // H.264 ue(v) and se(v) syntax elements, bounded to 32-bit result types.
    [[nodiscard]] std::expected<std::uint32_t, ParseError> read_ue();
    [[nodiscard]] std::expected<std::int32_t, ParseError> read_se();

private:
    [[nodiscard]] std::expected<std::uint64_t, ParseError>
    read_exp_golomb_code_num();

    ByteView bytes_;
    std::size_t bit_position_{0};
};

} // namespace semi_stream_probe

