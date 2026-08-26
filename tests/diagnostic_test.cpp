#include "semi_stream_probe/core/diagnostic.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_severity_strings() {
    using semi_stream_probe::DiagnosticSeverity;

    check(semi_stream_probe::to_string(DiagnosticSeverity::info) == "info",
          "info severity string");
    check(semi_stream_probe::to_string(DiagnosticSeverity::warning) ==
              "warning",
          "warning severity string");
    check(semi_stream_probe::to_string(DiagnosticSeverity::error) == "error",
          "error severity string");
}

void test_stable_code_strings() {
    using semi_stream_probe::DiagnosticCode;

    constexpr std::array mappings{
        std::pair{DiagnosticCode::rtp_invalid_packet,
                  std::string_view{"RTP_INVALID_PACKET"}},
        std::pair{DiagnosticCode::rtp_unexpected_payload_type,
                  std::string_view{"RTP_UNEXPECTED_PAYLOAD_TYPE"}},
        std::pair{DiagnosticCode::rtp_unexpected_ssrc,
                  std::string_view{"RTP_UNEXPECTED_SSRC"}},
        std::pair{DiagnosticCode::rtp_sequence_gap,
                  std::string_view{"RTP_SEQUENCE_GAP"}},
        std::pair{DiagnosticCode::rtp_duplicate_packet,
                  std::string_view{"RTP_DUPLICATE_PACKET"}},
        std::pair{DiagnosticCode::rtp_out_of_order_packet,
                  std::string_view{"RTP_OUT_OF_ORDER_PACKET"}},
        std::pair{DiagnosticCode::h264_rtp_invalid_payload,
                  std::string_view{"H264_RTP_INVALID_PAYLOAD"}},
        std::pair{DiagnosticCode::h264_stap_a_invalid,
                  std::string_view{"H264_STAP_A_INVALID"}},
        std::pair{DiagnosticCode::h264_fu_a_missing_start,
                  std::string_view{"H264_FU_A_MISSING_START"}},
        std::pair{DiagnosticCode::h264_fu_a_sequence_gap,
                  std::string_view{"H264_FU_A_SEQUENCE_GAP"}},
        std::pair{DiagnosticCode::h264_fu_a_interrupted,
                  std::string_view{"H264_FU_A_INTERRUPTED"}},
        std::pair{DiagnosticCode::h264_fu_a_context_changed,
                  std::string_view{"H264_FU_A_CONTEXT_CHANGED"}},
        std::pair{DiagnosticCode::h264_parameter_set_not_found,
                  std::string_view{"H264_PARAMETER_SET_NOT_FOUND"}},
        std::pair{DiagnosticCode::h264_idr_incomplete,
                  std::string_view{"H264_IDR_INCOMPLETE"}},
    };

    for (const auto& [code, expected] : mappings) {
        check(semi_stream_probe::to_string(code) == expected,
              "diagnostic code string matches report contract");
    }
}

void test_value_model() {
    semi_stream_probe::Diagnostic diagnostic{
        .severity = semi_stream_probe::DiagnosticSeverity::error,
        .code = semi_stream_probe::DiagnosticCode::h264_fu_a_sequence_gap,
        .summary = "FU-A sequence is discontinuous",
        .evidence = "expected RTP sequence 12031, received 12032",
        .impact = "the current NAL unit cannot be reconstructed safely",
        .recovery = "a later FU-A start can begin a new NAL",
        .location = {
            .input_byte_offset = std::nullopt,
            .bit_offset = std::nullopt,
            .nal_index = std::nullopt,
            .rtp_sequence_number = static_cast<std::uint16_t>(12032),
            .ssrc = 0x11223344U,
            .rtp_timestamp = 90'000U,
        },
    };

    check(diagnostic.impact.has_value(), "diagnostic impact is optional value");
    check(diagnostic.recovery.has_value(),
          "diagnostic recovery is optional value");
    check(!diagnostic.location.input_byte_offset.has_value(),
          "inapplicable byte offset remains empty");
    check(diagnostic.location.rtp_sequence_number == 12032,
          "RTP sequence location");
    check(diagnostic.location.ssrc == 0x11223344U, "SSRC location");
    check(diagnostic.location.rtp_timestamp == 90'000U,
          "RTP timestamp location");
}

} // namespace

int main() {
    test_severity_strings();
    test_stable_code_strings();
    test_value_model();

    if (failures != 0) {
        std::cerr << failures << " diagnostic test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_diagnostic_tests: all tests passed\n";
    return 0;
}
