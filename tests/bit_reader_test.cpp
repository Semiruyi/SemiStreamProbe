#include "semi_stream_probe/core/bit_reader.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_fixed_width_reads() {
    constexpr std::array<semi_stream_probe::Byte, 2> bytes{0xB2, 0x61};
    semi_stream_probe::BitReader reader(bytes);

    const auto empty = reader.read_bits(0);
    check(empty && *empty == 0, "zero-bit read should return zero");
    check(reader.bit_position() == 0, "zero-bit read should not advance");

    const auto first = reader.read_bits(4);
    check(first && *first == 0x0B, "first four bits should be read MSB-first");

    const auto across_bytes = reader.read_bits(8);
    check(across_bytes && *across_bytes == 0x26,
          "read should cross the byte boundary");

    const auto last = reader.read_bits(4);
    check(last && *last == 0x01, "last four bits should be read");
    check(reader.bit_position() == 16, "reader should reach the end");
    check(reader.bits_remaining() == 0, "no bits should remain");

    const auto exhausted = reader.read_bit();
    check(!exhausted, "read past the end should fail");
    if (!exhausted) {
        check(exhausted.error().code ==
                  semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
              "read past the end error code");
        check(exhausted.error().byte_offset == 2, "read past the end byte offset");
        check(exhausted.error().bit_offset == 16, "read past the end bit offset");
    }
}

void test_invalid_or_truncated_reads_are_atomic() {
    constexpr std::array<semi_stream_probe::Byte, 1> bytes{0xFE};

    semi_stream_probe::BitReader invalid_count_reader(bytes);
    const auto invalid_count = invalid_count_reader.read_bits(33);
    check(!invalid_count, "reading more than 32 bits should fail");
    if (!invalid_count) {
        check(invalid_count.error().code ==
                  semi_stream_probe::ParseErrorCode::invalid_bit_count,
              "invalid bit count error code");
    }
    check(invalid_count_reader.bit_position() == 0,
          "invalid bit count should not advance the reader");

    semi_stream_probe::BitReader truncated_reader(bytes);
    const auto prefix = truncated_reader.read_bits(7);
    check(prefix && *prefix == 0x7F, "seven-bit prefix should parse");

    const auto truncated = truncated_reader.read_bits(2);
    check(!truncated, "truncated fixed-width read should fail");
    if (!truncated) {
        check(truncated.error().code ==
                  semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
              "truncated read error code");
        check(truncated.error().bit_offset == 8,
              "truncated read should point to the first missing bit");
    }
    check(truncated_reader.bit_position() == 7,
          "truncated read should not consume available bits");
}

void test_unsigned_exp_golomb_values() {
    // Concatenated ue(v) values 0, 1, 2, 3 and 4:
    // 1 | 010 | 011 | 00100 | 00101
    constexpr std::array<semi_stream_probe::Byte, 3> bytes{0xA6, 0x42, 0x80};
    semi_stream_probe::BitReader reader(bytes);

    for (std::uint32_t expected = 0; expected <= 4; ++expected) {
        const auto value = reader.read_ue();
        check(value && *value == expected, "ue(v) sequence value");
    }
    check(reader.bit_position() == 17, "ue(v) sequence bit position");
}

void test_signed_exp_golomb_values() {
    // The same codeNum values 0..4 map to 0, 1, -1, 2 and -2.
    constexpr std::array<semi_stream_probe::Byte, 3> bytes{0xA6, 0x42, 0x80};
    constexpr std::array<std::int32_t, 5> expected{0, 1, -1, 2, -2};
    semi_stream_probe::BitReader reader(bytes);

    for (const auto expected_value : expected) {
        const auto value = reader.read_se();
        check(value && *value == expected_value, "se(v) sequence value");
    }
}

void test_exp_golomb_boundaries_and_errors() {
    constexpr std::array<semi_stream_probe::Byte, 1> truncated_bytes{0x00};
    semi_stream_probe::BitReader truncated_reader(truncated_bytes);
    const auto truncated = truncated_reader.read_ue();
    check(!truncated, "unterminated Exp-Golomb code should fail");
    if (!truncated) {
        check(truncated.error().code ==
                  semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
              "unterminated Exp-Golomb error code");
        check(truncated.error().bit_offset == 8,
              "unterminated Exp-Golomb bit offset");
    }

    // 33 leading zero bits exceed the supported Exp-Golomb width.
    constexpr std::array<semi_stream_probe::Byte, 5> overflow_bytes{
        0x00, 0x00, 0x00, 0x00, 0x40,
    };
    semi_stream_probe::BitReader overflow_reader(overflow_bytes);
    const auto overflow = overflow_reader.read_ue();
    check(!overflow, "oversized Exp-Golomb code should fail");
    if (!overflow) {
        check(overflow.error().code ==
                  semi_stream_probe::ParseErrorCode::exp_golomb_overflow,
              "oversized Exp-Golomb error code");
    }

    // 32 leading zeros, a stop bit and a zero suffix encode UINT32_MAX.
    constexpr std::array<semi_stream_probe::Byte, 9> maximum_ue_bytes{
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
    };
    semi_stream_probe::BitReader maximum_ue_reader(maximum_ue_bytes);
    const auto maximum_ue = maximum_ue_reader.read_ue();
    check(maximum_ue && *maximum_ue == std::numeric_limits<std::uint32_t>::max(),
          "UINT32_MAX ue(v) should parse");

    semi_stream_probe::BitReader signed_overflow_reader(maximum_ue_bytes);
    const auto signed_overflow = signed_overflow_reader.read_se();
    check(!signed_overflow, "positive se(v) overflow should fail");
    if (!signed_overflow) {
        check(signed_overflow.error().code ==
                  semi_stream_probe::ParseErrorCode::exp_golomb_overflow,
              "positive se(v) overflow error code");
    }

    // codeNum 2^32 maps to INT32_MIN.
    constexpr std::array<semi_stream_probe::Byte, 9> minimum_se_bytes{
        0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80,
    };
    semi_stream_probe::BitReader minimum_se_reader(minimum_se_bytes);
    const auto minimum_se = minimum_se_reader.read_se();
    check(minimum_se && *minimum_se == std::numeric_limits<std::int32_t>::min(),
          "INT32_MIN se(v) should parse");
}

} // namespace

int main() {
    test_fixed_width_reads();
    test_invalid_or_truncated_reads_are_atomic();
    test_unsigned_exp_golomb_values();
    test_signed_exp_golomb_values();
    test_exp_golomb_boundaries_and_errors();

    if (failures != 0) {
        std::cerr << failures << " BitReader test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_bit_reader_tests: all tests passed\n";
    return 0;
}
