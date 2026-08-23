#include "semi_stream_probe/core/h264_rtp.hpp"

#include <array>
#include <cstdint>
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

void check_kind(semi_stream_probe::Byte first_byte,
                semi_stream_probe::H264RtpPayloadKind expected,
                std::string_view message) {
    const std::array<semi_stream_probe::Byte, 1> payload{first_byte};
    const auto result =
        semi_stream_probe::parse_h264_rtp_payload_header(payload);
    check(result.has_value(), message);
    if (result) {
        check(result->kind == expected, "H.264 RTP payload kind");
    }
}

void test_payload_classification() {
    using Kind = semi_stream_probe::H264RtpPayloadKind;

    check_kind(0x61, Kind::single_nal_unit, "type 1 is Single NAL Unit");
    check_kind(0x77, Kind::single_nal_unit, "type 23 is Single NAL Unit");
    check_kind(0x78, Kind::stap_a, "type 24 is STAP-A");
    check_kind(0x79, Kind::stap_b, "type 25 is STAP-B");
    check_kind(0x7A, Kind::mtap16, "type 26 is MTAP16");
    check_kind(0x7B, Kind::mtap24, "type 27 is MTAP24");
    check_kind(0x7C, Kind::fu_a, "type 28 is FU-A");
    check_kind(0x7D, Kind::fu_b, "type 29 is FU-B");
    check_kind(0x60, Kind::reserved, "type 0 is reserved");
    check_kind(0x7E, Kind::reserved, "type 30 is reserved");
    check_kind(0x7F, Kind::reserved, "type 31 is reserved");

    check(std::string_view(semi_stream_probe::h264_rtp_payload_kind_name(
              Kind::single_nal_unit)) == "Single NAL Unit",
          "Single NAL Unit display name");
    check(std::string_view(semi_stream_probe::h264_rtp_payload_kind_name(
              Kind::fu_a)) == "FU-A",
          "FU-A display name");
}

void test_single_nal_depacketization() {
    constexpr std::array<semi_stream_probe::Byte, 16> bytes{
        0x80, 0xE0, 0xFF, 0xFE,
        0x01, 0x02, 0x03, 0x04,
        0x11, 0x22, 0x33, 0x44,
        0x65, 0x88, 0x99, 0xAA,
    };

    const auto rtp = semi_stream_probe::parse_rtp_packet(bytes);
    check(rtp.has_value(), "Single NAL RTP packet should parse");
    if (!rtp) {
        return;
    }

    const auto nal = semi_stream_probe::depacketize_h264_single_nal(*rtp);
    check(nal.has_value(), "Single NAL Unit should depacketize");
    if (!nal) {
        return;
    }

    check(nal->header.nal_ref_idc == 3, "Single NAL nal_ref_idc");
    check(nal->header.nal_unit_type == 5, "Single NAL type");
    check(nal->bytes.size() == 4, "complete NAL bytes are returned");
    check(nal->bytes.data() == bytes.data() + 12,
          "Single NAL is a zero-copy view into the RTP packet");
}

void test_rtp_padding_is_not_nal_data() {
    constexpr std::array<semi_stream_probe::Byte, 18> bytes{
        0xA0, 0x60, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x09,
        0x10, 0x20, 0x30, 0x40,
        0x67, 0x42,
        0x00, 0x00, 0x00, 0x04,
    };

    const auto rtp = semi_stream_probe::parse_rtp_packet(bytes);
    check(rtp.has_value(), "padded H.264 RTP packet should parse");
    if (!rtp) {
        return;
    }

    const auto nal = semi_stream_probe::depacketize_h264_single_nal(*rtp);
    check(nal.has_value(), "padded Single NAL should depacketize");
    if (nal) {
        check(nal->bytes.size() == 2, "RTP padding is excluded from NAL bytes");
        check(nal->bytes.back() == 0x42, "NAL ends before RTP padding");
    }
}

void test_stap_a_depacketization() {
    constexpr std::array<semi_stream_probe::Byte, 22> bytes{
        0x80, 0xE0, 0x01, 0x41,
        0x10, 0x20, 0x30, 0x40,
        0x11, 0x22, 0x33, 0x44,
        0x78,
        0x00, 0x02, 0x67, 0x42,
        0x00, 0x03, 0x68, 0xCE, 0x06,
    };

    const auto rtp = semi_stream_probe::parse_rtp_packet(bytes);
    check(rtp.has_value(), "STAP-A RTP packet should parse");
    if (!rtp) {
        return;
    }

    const auto stap_a = semi_stream_probe::depacketize_h264_stap_a(*rtp);
    check(stap_a.has_value(), "STAP-A should depacketize");
    if (!stap_a) {
        return;
    }

    check(stap_a->indicator.nal_unit_type == 24, "STAP-A indicator type");
    check(stap_a->indicator.nal_ref_idc == 3, "STAP-A indicator NRI");
    check(stap_a->nal_units.size() == 2, "STAP-A contains two NAL units");
    if (stap_a->nal_units.size() != 2) {
        return;
    }

    check(stap_a->nal_units[0].header.nal_unit_type == 7,
          "first STAP-A NAL is SPS");
    check(stap_a->nal_units[0].bytes.size() == 2,
          "first STAP-A NAL size");
    check(stap_a->nal_units[0].bytes.data() == bytes.data() + 15,
          "first STAP-A NAL is a zero-copy view");
    check(stap_a->nal_units[1].header.nal_unit_type == 8,
          "second STAP-A NAL is PPS");
    check(stap_a->nal_units[1].bytes.size() == 3,
          "second STAP-A NAL size");
}

void check_stap_a_error(
    semi_stream_probe::ByteView payload,
    semi_stream_probe::ParseErrorCode expected_code,
    std::size_t expected_offset,
    std::string_view message) {
    semi_stream_probe::RtpPacket packet;
    packet.sequence_number = 777;
    packet.payload = payload;

    const auto result = semi_stream_probe::depacketize_h264_stap_a(packet);
    check(!result, message);
    if (!result) {
        check(result.error().code == expected_code,
              "STAP-A failure should use expected error code");
        check(result.error().byte_offset == expected_offset,
              "STAP-A failure byte offset");
        check(result.error().rtp_sequence_number == 777,
              "STAP-A failure carries RTP sequence number");
    }
}

void test_invalid_stap_a_payloads() {
    constexpr std::array<semi_stream_probe::Byte, 1> empty{0x78};
    check_stap_a_error(
        empty, semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload, 1,
        "STAP-A without aggregation units should fail");

    constexpr std::array<semi_stream_probe::Byte, 2> truncated_size{0x78,
                                                                    0x00};
    check_stap_a_error(
        truncated_size,
        semi_stream_probe::ParseErrorCode::unexpected_end_of_data, 1,
        "truncated STAP-A NAL size should fail");

    constexpr std::array<semi_stream_probe::Byte, 3> zero_size{0x78, 0x00,
                                                               0x00};
    check_stap_a_error(
        zero_size,
        semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload, 1,
        "zero STAP-A NAL size should fail");

    constexpr std::array<semi_stream_probe::Byte, 5> truncated_nal{
        0x78, 0x00, 0x03, 0x67, 0x42,
    };
    check_stap_a_error(
        truncated_nal,
        semi_stream_probe::ParseErrorCode::unexpected_end_of_data, 3,
        "STAP-A NAL shorter than declared size should fail");

    constexpr std::array<semi_stream_probe::Byte, 4> forbidden_nal{
        0x78, 0x00, 0x01, 0xE7,
    };
    check_stap_a_error(
        forbidden_nal,
        semi_stream_probe::ParseErrorCode::forbidden_zero_bit_set, 3,
        "STAP-A NAL with set forbidden bit should fail");

    constexpr std::array<semi_stream_probe::Byte, 4> nested_stap_a{
        0x78, 0x00, 0x01, 0x78,
    };
    check_stap_a_error(
        nested_stap_a,
        semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload, 3,
        "nested STAP-A should fail");

    constexpr std::array<semi_stream_probe::Byte, 4> contained_fu_a{
        0x78, 0x00, 0x01, 0x7C,
    };
    check_stap_a_error(
        contained_fu_a,
        semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload, 3,
        "STAP-A containing FU-A should fail");

    constexpr std::array<semi_stream_probe::Byte, 4> nri_mismatch{
        0x78, 0x00, 0x01, 0x47,
    };
    check_stap_a_error(
        nri_mismatch,
        semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload, 0,
        "STAP-A NRI mismatch should fail");

    constexpr std::array<semi_stream_probe::Byte, 2> single_nal{0x67, 0x42};
    check_stap_a_error(
        single_nal,
        semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload, 0,
        "Single NAL must not be treated as STAP-A");
}

void test_invalid_payloads() {
    semi_stream_probe::RtpPacket empty_packet;
    empty_packet.sequence_number = 100;
    const auto empty =
        semi_stream_probe::depacketize_h264_single_nal(empty_packet);
    check(!empty, "empty H.264 RTP payload should fail");
    if (!empty) {
        check(empty.error().code ==
                  semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload,
              "empty payload error code");
        check(empty.error().rtp_sequence_number == 100,
              "RTP sequence number is preserved in payload error context");
    }

    constexpr std::array<semi_stream_probe::Byte, 1> forbidden{0xE5};
    const auto forbidden_result =
        semi_stream_probe::parse_h264_rtp_payload_header(forbidden);
    check(!forbidden_result, "set F bit should fail");
    if (!forbidden_result) {
        check(forbidden_result.error().code ==
                  semi_stream_probe::ParseErrorCode::forbidden_zero_bit_set,
              "forbidden bit error is reused from NAL parsing");
    }

    constexpr std::array<semi_stream_probe::Byte, 3> stap_a{0x78, 0x00, 0x00};
    semi_stream_probe::RtpPacket aggregation_packet;
    aggregation_packet.sequence_number = 321;
    aggregation_packet.payload = stap_a;
    const auto aggregation =
        semi_stream_probe::depacketize_h264_single_nal(aggregation_packet);
    check(!aggregation, "STAP-A must not be treated as Single NAL Unit");
    if (!aggregation) {
        check(aggregation.error().code ==
                  semi_stream_probe::ParseErrorCode::invalid_h264_rtp_payload,
              "non-Single packet error code");
        check(aggregation.error().rtp_sequence_number == 321,
              "non-Single error carries RTP sequence number");
    }

    constexpr std::array<semi_stream_probe::Byte, 1> reserved{0x60};
    semi_stream_probe::RtpPacket reserved_packet;
    reserved_packet.payload = reserved;
    const auto reserved_result =
        semi_stream_probe::depacketize_h264_single_nal(reserved_packet);
    check(!reserved_result, "reserved type must not be treated as Single NAL");
}

} // namespace

int main() {
    test_payload_classification();
    test_single_nal_depacketization();
    test_rtp_padding_is_not_nal_data();
    test_stap_a_depacketization();
    test_invalid_stap_a_payloads();
    test_invalid_payloads();

    if (failures != 0) {
        std::cerr << failures << " H.264 RTP test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_h264_rtp_tests: all tests passed\n";
    return 0;
}
