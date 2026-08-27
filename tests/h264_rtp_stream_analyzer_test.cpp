#include "semi_stream_probe/core/h264_rtp_stream_analyzer.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<semi_stream_probe::Byte>
make_packet(std::uint16_t sequence_number,
            std::uint32_t timestamp,
            std::span<const semi_stream_probe::Byte> payload,
            bool marker = false) {
    constexpr std::uint32_t ssrc = 0x11223344U;
    std::vector<semi_stream_probe::Byte> packet{
        0x80,
        static_cast<semi_stream_probe::Byte>(96U | (marker ? 0x80U : 0U)),
        static_cast<semi_stream_probe::Byte>(sequence_number >> 8U),
        static_cast<semi_stream_probe::Byte>(sequence_number & 0xFFU),
        static_cast<semi_stream_probe::Byte>(timestamp >> 24U),
        static_cast<semi_stream_probe::Byte>((timestamp >> 16U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>((timestamp >> 8U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>(timestamp & 0xFFU),
        static_cast<semi_stream_probe::Byte>(ssrc >> 24U),
        static_cast<semi_stream_probe::Byte>((ssrc >> 16U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>((ssrc >> 8U) & 0xFFU),
        static_cast<semi_stream_probe::Byte>(ssrc & 0xFFU),
    };
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::size_t count_diagnostic(
    const semi_stream_probe::H264RtpStreamAnalyzer& analyzer,
    semi_stream_probe::DiagnosticCode code) {
    std::size_t count = 0;
    for (const auto& diagnostic : analyzer.diagnostics()) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
}

void test_single_nal_and_stap_a() {
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    std::vector<semi_stream_probe::Byte> single_payload{0x65, 0xAA};
    auto single_packet = make_packet(1, 90'000, single_payload, true);
    auto single = analyzer.push(single_packet, 0ms);

    check(single.size() == 1, "Single NAL produces one owned NAL");
    if (!single.empty()) {
        check(single[0].header.nal_unit_type == 5, "Single NAL type");
        check(single[0].bytes == single_payload, "Single NAL bytes");
        check(single[0].marker, "Single NAL marker");
        single_packet.back() = 0x00;
        check(single[0].bytes.back() == 0xAA,
              "Single NAL output owns its bytes");
    }

    constexpr semi_stream_probe::Byte stap_payload[]{
        0x78,
        0x00, 0x02, 0x67, 0x42,
        0x00, 0x02, 0x68, 0xCE,
    };
    const auto stap_packet = make_packet(2, 93'000, stap_payload, true);
    const auto stap = analyzer.push(stap_packet, 33ms);
    check(stap.size() == 2, "STAP-A produces all contained NAL units");
    if (stap.size() == 2) {
        check(stap[0].header.nal_unit_type == 7, "STAP-A SPS type");
        check(stap[1].header.nal_unit_type == 8, "STAP-A PPS type");
        check(!stap[0].marker && stap[1].marker,
              "STAP-A marker belongs to final contained NAL");
    }

    check(analyzer.h264_statistics().single_nal_packets == 1,
          "Single NAL packet count");
    check(analyzer.h264_statistics().stap_a_packets == 1,
          "STAP-A packet count");
    check(analyzer.h264_statistics().completed_nal_units == 3,
          "completed NAL count includes STAP-A contents");
}

void test_complete_fu_a() {
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    constexpr semi_stream_probe::Byte start[]{0x7C, 0x85, 0x11};
    constexpr semi_stream_probe::Byte middle[]{0x7C, 0x05, 0x22};
    constexpr semi_stream_probe::Byte end[]{0x7C, 0x45, 0x33};

    check(analyzer.push(make_packet(10, 90'000, start), 0ms).empty(),
          "FU-A start does not complete a NAL");
    check(analyzer.push(make_packet(11, 90'000, middle), 1ms).empty(),
          "FU-A middle does not complete a NAL");
    const auto completed =
        analyzer.push(make_packet(12, 90'000, end, true), 2ms);

    constexpr semi_stream_probe::Byte expected[]{0x65, 0x11, 0x22, 0x33};
    check(completed.size() == 1, "FU-A end completes one NAL");
    if (!completed.empty()) {
        check(completed[0].bytes ==
                  std::vector<semi_stream_probe::Byte>(std::begin(expected),
                                                       std::end(expected)),
              "FU-A reconstructed bytes");
        check(completed[0].start_sequence_number == 10 &&
                  completed[0].end_sequence_number == 12,
              "FU-A sequence range");
        check(completed[0].marker, "FU-A final marker");
    }
    check(analyzer.h264_statistics().fu_a_packets == 3,
          "FU-A packet count");
    check(analyzer.h264_statistics().completed_idr_nal_units == 1,
          "completed IDR NAL count");
    check(analyzer.diagnostics().empty(), "valid FU-A has no diagnostics");
}

void test_idr_gap_and_recovery() {
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    constexpr semi_stream_probe::Byte idr_start[]{0x7C, 0x85, 0x11};
    constexpr semi_stream_probe::Byte idr_end[]{0x7C, 0x45, 0x33};

    static_cast<void>(analyzer.push(make_packet(20, 90'000, idr_start), 0ms));
    check(analyzer.push(make_packet(22, 90'000, idr_end, true), 2ms).empty(),
          "FU-A after sequence gap is discarded");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::rtp_sequence_gap) == 1,
          "RTP gap diagnostic");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_fu_a_sequence_gap) == 1,
          "FU-A gap diagnostic");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_idr_incomplete) == 1,
          "incomplete IDR diagnostic");

    static_cast<void>(analyzer.push(make_packet(23, 93'000, idr_start), 3ms));
    const auto recovered =
        analyzer.push(make_packet(24, 93'000, idr_end, true), 4ms);
    check(recovered.size() == 1 && recovered[0].header.nal_unit_type == 5,
          "later intact IDR is reconstructed");
    check(analyzer.h264_statistics().incomplete_idr_nal_units == 1,
          "one incomplete IDR counted");
    check(analyzer.h264_statistics().completed_idr_nal_units == 1,
          "one later complete IDR counted");
}

void test_missing_start_and_new_start_recovery() {
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    constexpr semi_stream_probe::Byte idr_start[]{0x7C, 0x85, 0x11};
    constexpr semi_stream_probe::Byte idr_end[]{0x7C, 0x45, 0x22};
    constexpr semi_stream_probe::Byte p_start[]{0x7C, 0x81, 0x33};
    constexpr semi_stream_probe::Byte p_end[]{0x7C, 0x41, 0x44};

    check(analyzer.push(make_packet(30, 90'000, idr_end, true), 0ms).empty(),
          "FU-A end without start is discarded");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_fu_a_missing_start) == 1,
          "missing FU-A start diagnostic");

    static_cast<void>(analyzer.push(make_packet(31, 93'000, idr_start), 1ms));
    static_cast<void>(analyzer.push(make_packet(32, 96'000, p_start), 2ms));
    const auto completed =
        analyzer.push(make_packet(33, 96'000, p_end, true), 3ms);
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_fu_a_interrupted) == 1,
          "new start interrupts previous FU-A");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_idr_incomplete) == 1,
          "interrupted IDR is reported");
    check(completed.size() == 1 && completed[0].header.nal_unit_type == 1,
          "new FU-A start becomes active and completes");
}

void test_context_change_and_finish() {
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    constexpr semi_stream_probe::Byte start[]{0x7C, 0x85, 0x11};
    constexpr semi_stream_probe::Byte end[]{0x7C, 0x45, 0x22};

    static_cast<void>(analyzer.push(make_packet(40, 90'000, start), 0ms));
    check(analyzer.push(make_packet(41, 90'001, end, true), 1ms).empty(),
          "timestamp change discards FU-A");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_fu_a_context_changed) ==
              1,
          "FU-A context-change diagnostic");

    static_cast<void>(analyzer.push(make_packet(42, 93'000, start), 2ms));
    analyzer.finish();
    analyzer.finish();
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_fu_a_incomplete) == 1,
          "finish reports incomplete FU-A once");
    check(analyzer.h264_statistics().incomplete_fu_a_nal_units == 2,
          "context change and end-of-input incomplete NALs are counted");
}

void test_invalid_stap_a() {
    semi_stream_probe::H264RtpStreamAnalyzer analyzer;
    constexpr semi_stream_probe::Byte invalid_stap[]{0x78, 0x00, 0x03, 0x67};
    check(analyzer.push(make_packet(50, 90'000, invalid_stap), 0ms).empty(),
          "truncated STAP-A is discarded");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::h264_stap_a_invalid) == 1,
          "invalid STAP-A diagnostic");
}

} // namespace

int main() {
    test_single_nal_and_stap_a();
    test_complete_fu_a();
    test_idr_gap_and_recovery();
    test_missing_start_and_new_start_recovery();
    test_context_change_and_finish();
    test_invalid_stap_a();

    if (failures != 0) {
        std::cerr << failures << " H.264 RTP stream analyzer test(s) failed\n";
        return 1;
    }
    std::cout
        << "semi_stream_probe_h264_rtp_stream_analyzer_tests: all tests passed\n";
    return 0;
}
