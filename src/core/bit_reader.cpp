#include "semi_stream_probe/core/bit_reader.hpp"

#include <limits>
#include <string>
#include <utility>

namespace semi_stream_probe {

namespace {

constexpr std::size_t bits_per_byte = 8;
constexpr std::size_t maximum_read_bits = 32;
constexpr std::size_t maximum_exp_golomb_leading_zeros = 32;

[[nodiscard]] ParseError make_bit_error(ParseErrorCode code,
                                        std::size_t bit_offset,
                                        std::string message) {
    return ParseError{
        .code = code,
        .byte_offset = bit_offset / bits_per_byte,
        .bit_offset = bit_offset,
        .message = std::move(message),
    };
}

} // namespace

std::size_t BitReader::bits_remaining() const noexcept {
    const auto byte_position = bit_position_ / bits_per_byte;
    if (byte_position >= bytes_.size()) {
        return 0;
    }

    const auto bit_in_byte = bit_position_ % bits_per_byte;
    const auto bytes_after_current = bytes_.size() - byte_position - 1;
    return (bits_per_byte - bit_in_byte) +
           bytes_after_current * bits_per_byte;
}

std::expected<bool, ParseError> BitReader::read_bit() {
    auto value = read_bits(1);
    if (!value) {
        return std::unexpected(std::move(value.error()));
    }
    return *value != 0;
}

std::expected<std::uint32_t, ParseError>
BitReader::read_bits(std::size_t count) {
    if (count > maximum_read_bits) {
        return std::unexpected(make_bit_error(
            ParseErrorCode::invalid_bit_count, bit_position_,
            "BitReader cannot read more than 32 bits at once"));
    }

    const auto remaining = bits_remaining();
    if (count > remaining) {
        return std::unexpected(make_bit_error(
            ParseErrorCode::unexpected_end_of_data, bit_position_ + remaining,
            "RBSP ended before the requested bits could be read"));
    }

    std::uint32_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto byte_position = bit_position_ / bits_per_byte;
        const auto bit_in_byte =
            static_cast<unsigned int>(bit_position_ % bits_per_byte);
        const auto shift = 7U - bit_in_byte;
        const auto bit = static_cast<std::uint32_t>(
            (bytes_[byte_position] >> shift) & 0x01U);

        value = (value << 1U) | bit;
        ++bit_position_;
    }
    return value;
}

std::expected<std::uint64_t, ParseError>
BitReader::read_exp_golomb_code_num() {
    std::size_t leading_zero_bits = 0;
    while (true) {
        auto bit = read_bit();
        if (!bit) {
            return std::unexpected(std::move(bit.error()));
        }
        if (*bit) {
            break;
        }

        ++leading_zero_bits;
        if (leading_zero_bits > maximum_exp_golomb_leading_zeros) {
            return std::unexpected(make_bit_error(
                ParseErrorCode::exp_golomb_overflow, bit_position_,
                "Exp-Golomb code has more than 32 leading zero bits"));
        }
    }

    auto suffix = read_bits(leading_zero_bits);
    if (!suffix) {
        return std::unexpected(std::move(suffix.error()));
    }

    const auto base =
        (std::uint64_t{1} << static_cast<unsigned int>(leading_zero_bits)) - 1;
    return base + static_cast<std::uint64_t>(*suffix);
}

std::expected<std::uint32_t, ParseError> BitReader::read_ue() {
    auto code_num = read_exp_golomb_code_num();
    if (!code_num) {
        return std::unexpected(std::move(code_num.error()));
    }
    if (*code_num > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(make_bit_error(
            ParseErrorCode::exp_golomb_overflow, bit_position_,
            "unsigned Exp-Golomb value exceeds the 32-bit range"));
    }
    return static_cast<std::uint32_t>(*code_num);
}

std::expected<std::int32_t, ParseError> BitReader::read_se() {
    auto code_num = read_exp_golomb_code_num();
    if (!code_num) {
        return std::unexpected(std::move(code_num.error()));
    }

    if ((*code_num & 1U) != 0) {
        const auto magnitude = (*code_num + 1U) / 2U;
        if (magnitude >
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return std::unexpected(make_bit_error(
                ParseErrorCode::exp_golomb_overflow, bit_position_,
                "signed Exp-Golomb value exceeds the 32-bit range"));
        }
        return static_cast<std::int32_t>(magnitude);
    }

    const auto magnitude = *code_num / 2U;
    const auto minimum_magnitude =
        std::uint64_t{1} << (std::numeric_limits<std::uint32_t>::digits - 1);
    if (magnitude > minimum_magnitude) {
        return std::unexpected(make_bit_error(
            ParseErrorCode::exp_golomb_overflow, bit_position_,
            "signed Exp-Golomb value exceeds the 32-bit range"));
    }
    if (magnitude == minimum_magnitude) {
        return std::numeric_limits<std::int32_t>::min();
    }
    return -static_cast<std::int32_t>(magnitude);
}

} // namespace semi_stream_probe
