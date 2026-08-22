#include "semi_stream_probe/core/rbsp.hpp"

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

void test_emulation_prevention_bytes_are_removed() {
    constexpr std::array<semi_stream_probe::Byte, 19> ebsp{
        0x00, 0x00, 0x03, 0x00, 0x80,
        0x00, 0x00, 0x03, 0x01, 0x80,
        0x00, 0x00, 0x03, 0x02, 0x80,
        0x00, 0x00, 0x03, 0x03,
    };
    constexpr std::array<semi_stream_probe::Byte, 15> expected{
        0x00, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x01, 0x80,
        0x00, 0x00, 0x02, 0x80,
        0x00, 0x00, 0x03,
    };

    const auto result = semi_stream_probe::ebsp_to_rbsp(ebsp);
    check(result.has_value(), "valid EBSP should convert");
    if (result) {
        check(*result == semi_stream_probe::ByteBuffer(expected.begin(), expected.end()),
              "all emulation prevention bytes should be removed");
    }
}

void test_non_prevention_bytes_are_preserved() {
    constexpr std::array<semi_stream_probe::Byte, 8> ebsp{
        0x00, 0x00, 0x03, 0x04, 0x12, 0x00, 0x00, 0x03,
    };

    const auto result = semi_stream_probe::ebsp_to_rbsp(ebsp);
    check(result.has_value(), "EBSP with literal 0x03 bytes should convert");
    if (result) {
        check(*result == semi_stream_probe::ByteBuffer(ebsp.begin(), ebsp.end()),
              "literal 0x03 bytes should be preserved");
    }
}

void test_empty_ebsp() {
    constexpr std::array<semi_stream_probe::Byte, 0> ebsp{};
    const auto result = semi_stream_probe::ebsp_to_rbsp(ebsp);

    check(result.has_value(), "empty EBSP should convert");
    if (result) {
        check(result->empty(), "empty EBSP should produce empty RBSP");
    }
}

} // namespace

int main() {
    test_emulation_prevention_bytes_are_removed();
    test_non_prevention_bytes_are_preserved();
    test_empty_ebsp();

    if (failures != 0) {
        std::cerr << failures << " RBSP test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_rbsp_tests: all tests passed\n";
    return 0;
}
