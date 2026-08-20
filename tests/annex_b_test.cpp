#include "semi_stream_probe/core/annex_b.hpp"

#include <array>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_mixed_start_codes_and_trailing_zeros() {
    constexpr std::array<semi_stream_probe::Byte, 12> bytes{
        0x00, 0x00, 0x00, 0x01, 0x67, 0xAA,
        0x00, 0x00, 0x01, 0x68, 0x00, 0x00,
    };

    const auto result = semi_stream_probe::scan_annex_b(bytes);
    check(result.has_value(), "mixed start codes should parse");
    if (!result) {
        return;
    }

    check(result->size() == 2, "two NAL units should be found");
    if (result->size() != 2) {
        return;
    }

    const auto& first = result->at(0);
    check(first.index == 0, "first NAL index");
    check(first.start_code_offset == 0, "four-byte start offset");
    check(first.start_code_size == 4, "four-byte start size");
    check(first.payload_offset == 4, "first payload offset");
    check(first.payload_size == 2, "first payload size");

    const auto& second = result->at(1);
    check(second.index == 1, "second NAL index");
    check(second.start_code_offset == 6, "three-byte start offset");
    check(second.start_code_size == 3, "three-byte start size");
    check(second.payload_offset == 9, "second payload offset");
    check(second.payload_size == 1, "trailing zero bytes are framing bytes");
}

void test_leading_zero_byte() {
    constexpr std::array<semi_stream_probe::Byte, 6> bytes{
        0x00, 0x00, 0x00, 0x00, 0x01, 0x65,
    };
    const auto result = semi_stream_probe::scan_annex_b(bytes);
    check(result.has_value(), "leading zero byte should be accepted");
    if (result) {
        check(result->size() == 1, "one NAL after leading zero byte");
        check(result->front().start_code_offset == 1,
              "start code begins after leading zero byte");
        check(result->front().start_code_size == 4, "four-byte start code detected");
    }
}

void test_errors() {
    const std::array<semi_stream_probe::Byte, 0> empty{};
    const auto no_start = semi_stream_probe::scan_annex_b(empty);
    check(!no_start, "empty input should fail");
    if (!no_start) {
        check(no_start.error().code ==
                  semi_stream_probe::ParseErrorCode::start_code_not_found,
              "empty input error code");
    }

    constexpr std::array<semi_stream_probe::Byte, 5> garbage{
        0x12, 0x00, 0x00, 0x01, 0x67,
    };
    const auto garbage_result = semi_stream_probe::scan_annex_b(garbage);
    check(!garbage_result, "non-zero bytes before first start code should fail");
    if (!garbage_result) {
        check(garbage_result.error().byte_offset == 0, "garbage error offset");
    }

    constexpr std::array<semi_stream_probe::Byte, 7> empty_nal{
        0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x67,
    };
    const auto empty_nal_result = semi_stream_probe::scan_annex_b(empty_nal);
    check(!empty_nal_result, "empty NAL should fail");
    if (!empty_nal_result) {
        check(empty_nal_result.error().code ==
                  semi_stream_probe::ParseErrorCode::empty_nal_unit,
              "empty NAL error code");
        check(empty_nal_result.error().byte_offset == 3, "empty NAL error offset");
    }
}

} // namespace

int main() {
    test_mixed_start_codes_and_trailing_zeros();
    test_leading_zero_byte();
    test_errors();

    if (failures != 0) {
        std::cerr << failures << " Annex-B test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_annex_b_tests: all tests passed\n";
    return 0;
}
