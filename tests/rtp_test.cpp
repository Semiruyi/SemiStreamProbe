#include "semi_stream_probe/core/rtp.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_fixed_header_and_payload() {
    constexpr std::array<semi_stream_probe::Byte, 16> packet{
        0x80, 0xE0, 0x12, 0x34,
        0x01, 0x02, 0x03, 0x04,
        0xA1, 0xA2, 0xA3, 0xA4,
        0x65, 0x88, 0x84, 0x00,
    };

    const auto result = semi_stream_probe::parse_rtp_packet(packet);
    check(result.has_value(), "basic RTP packet should parse");
    if (!result) {
        return;
    }

    check(result->version == 2, "RTP version");
    check(!result->has_padding, "padding flag is clear");
    check(!result->has_extension, "extension flag is clear");
    check(result->marker, "marker bit");
    check(result->payload_type == 96, "payload type");
    check(result->sequence_number == 0x1234, "sequence number");
    check(result->timestamp == 0x01020304, "timestamp");
    check(result->ssrc == 0xA1A2A3A4, "SSRC");
    check(result->csrcs.empty(), "empty CSRC list");
    check(!result->extension.has_value(), "no extension value");
    check(result->header_size == 12, "fixed header size");
    check(result->padding_size == 0, "zero padding size");
    check(result->payload.size() == 4, "payload size");
    check(result->payload[0] == 0x65, "payload starts at byte 12");
}

void test_csrc_list() {
    constexpr std::array<semi_stream_probe::Byte, 21> packet{
        0x82, 0x60, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x02,
        0x11, 0x22, 0x33, 0x44,
        0x01, 0x02, 0x03, 0x04,
        0xF1, 0xF2, 0xF3, 0xF4,
        0x61,
    };

    const auto result = semi_stream_probe::parse_rtp_packet(packet);
    check(result.has_value(), "RTP packet with CSRCs should parse");
    if (!result) {
        return;
    }

    check(result->csrcs.size() == 2, "CSRC count");
    check(result->csrcs[0] == 0x01020304, "first CSRC");
    check(result->csrcs[1] == 0xF1F2F3F4, "second CSRC");
    check(result->header_size == 20, "header includes CSRC list");
    check(result->payload.size() == 1 && result->payload[0] == 0x61,
          "payload follows CSRC list");
}

void test_header_extension() {
    constexpr std::array<semi_stream_probe::Byte, 25> packet{
        0x90, 0x60, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x03,
        0x10, 0x20, 0x30, 0x40,
        0xBE, 0xDE, 0x00, 0x02,
        0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88,
        0x67,
    };

    const auto result = semi_stream_probe::parse_rtp_packet(packet);
    check(result.has_value(), "RTP packet with extension should parse");
    if (!result) {
        return;
    }

    check(result->extension.has_value(), "extension value exists");
    if (result->extension) {
        check(result->extension->profile_identifier == 0xBEDE,
              "extension profile identifier");
        check(result->extension->length_words == 2,
              "extension length in words");
        check(result->extension->data.size() == 8, "extension data size");
        check(result->extension->data.front() == 0x11,
              "extension data starts after extension header");
    }
    check(result->header_size == 24, "header includes extension");
    check(result->payload.size() == 1 && result->payload[0] == 0x67,
          "payload follows extension");
}

void test_padding() {
    constexpr std::array<semi_stream_probe::Byte, 18> packet{
        0xA0, 0x60, 0x00, 0x03,
        0x00, 0x00, 0x00, 0x04,
        0x12, 0x34, 0x56, 0x78,
        0x65, 0x99,
        0x00, 0x00, 0x00, 0x04,
    };

    const auto result = semi_stream_probe::parse_rtp_packet(packet);
    check(result.has_value(), "padded RTP packet should parse");
    if (!result) {
        return;
    }

    check(result->has_padding, "padding flag");
    check(result->padding_size == 4, "padding count includes final byte");
    check(result->payload.size() == 2, "padding excluded from payload");
    check(result->payload[1] == 0x99, "payload ends before padding");
}

void check_error(semi_stream_probe::ByteView packet,
                 semi_stream_probe::ParseErrorCode expected_code,
                 std::string_view message) {
    const auto result = semi_stream_probe::parse_rtp_packet(packet);
    check(!result, message);
    if (!result) {
        check(result.error().code == expected_code,
              "RTP failure should use expected error code");
    }
}

void test_invalid_packets() {
    constexpr std::array<semi_stream_probe::Byte, 11> short_packet{};
    check_error(short_packet,
                semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
                "packet shorter than fixed header should fail");

    constexpr std::array<semi_stream_probe::Byte, 12> wrong_version{
        0x40, 0x60,
    };
    check_error(wrong_version, semi_stream_probe::ParseErrorCode::invalid_rtp,
                "RTP version other than 2 should fail");

    constexpr std::array<semi_stream_probe::Byte, 16> truncated_csrcs{
        0x82, 0x60,
    };
    check_error(truncated_csrcs,
                semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
                "truncated CSRC list should fail");

    constexpr std::array<semi_stream_probe::Byte, 14> missing_extension_header{
        0x90, 0x60,
    };
    check_error(missing_extension_header,
                semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
                "truncated extension header should fail");

    constexpr std::array<semi_stream_probe::Byte, 20> truncated_extension_data{
        0x90, 0x60, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xBE, 0xDE, 0x00, 0x02,
        0x01, 0x02, 0x03, 0x04,
    };
    check_error(truncated_extension_data,
                semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
                "truncated extension data should fail");

    constexpr std::array<semi_stream_probe::Byte, 12> missing_padding{
        0xA0, 0x60,
    };
    check_error(missing_padding,
                semi_stream_probe::ParseErrorCode::invalid_rtp,
                "set padding flag without bytes should fail");

    constexpr std::array<semi_stream_probe::Byte, 13> zero_padding{
        0xA0, 0x60, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00,
    };
    check_error(zero_padding, semi_stream_probe::ParseErrorCode::invalid_rtp,
                "zero padding count should fail");

    constexpr std::array<semi_stream_probe::Byte, 14> excessive_padding{
        0xA0, 0x60, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x65, 0x03,
    };
    check_error(excessive_padding,
                semi_stream_probe::ParseErrorCode::invalid_rtp,
                "padding larger than bytes after header should fail");
}

} // namespace

int main() {
    test_fixed_header_and_payload();
    test_csrc_list();
    test_header_extension();
    test_padding();
    test_invalid_packets();

    if (failures != 0) {
        std::cerr << failures << " RTP test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_rtp_tests: all tests passed\n";
    return 0;
}
