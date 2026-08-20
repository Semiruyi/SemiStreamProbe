#include "semi_stream_probe/core/nal.hpp"

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

void test_valid_headers() {
    constexpr std::array<semi_stream_probe::Byte, 1> sps{0x67};
    const auto sps_header = semi_stream_probe::parse_nal_header(sps);
    check(sps_header.has_value(), "SPS header should parse");
    if (sps_header) {
        check(!sps_header->forbidden_zero_bit, "forbidden bit is clear");
        check(sps_header->nal_ref_idc == 3, "SPS nal_ref_idc");
        check(sps_header->nal_unit_type == 7, "SPS unit type");
        check(std::string_view(semi_stream_probe::nal_unit_type_name(
                  sps_header->nal_unit_type)) == "SPS",
              "SPS display name");
    }

    constexpr std::array<semi_stream_probe::Byte, 1> idr{0x05};
    const auto idr_header = semi_stream_probe::parse_nal_header(idr);
    check(idr_header.has_value(), "IDR header should parse");
    if (idr_header) {
        check(idr_header->nal_ref_idc == 0, "IDR test nal_ref_idc");
        check(idr_header->nal_unit_type == 5, "IDR unit type");
    }
}

void test_invalid_headers() {
    const std::array<semi_stream_probe::Byte, 0> empty{};
    const auto empty_result = semi_stream_probe::parse_nal_header(empty);
    check(!empty_result, "empty NAL should fail");
    if (!empty_result) {
        check(empty_result.error().code ==
                  semi_stream_probe::ParseErrorCode::empty_nal_unit,
              "empty NAL header error code");
    }

    constexpr std::array<semi_stream_probe::Byte, 1> forbidden{0x87};
    const auto forbidden_result = semi_stream_probe::parse_nal_header(forbidden);
    check(!forbidden_result, "set forbidden_zero_bit should fail");
    if (!forbidden_result) {
        check(forbidden_result.error().code ==
                  semi_stream_probe::ParseErrorCode::forbidden_zero_bit_set,
              "forbidden bit error code");
    }
}

} // namespace

int main() {
    test_valid_headers();
    test_invalid_headers();

    if (failures != 0) {
        std::cerr << failures << " NAL test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_nal_tests: all tests passed\n";
    return 0;
}
