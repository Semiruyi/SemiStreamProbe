#include "semi_stream_probe/core/rtp_stream_analyzer.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
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
            std::uint32_t ssrc = 0x11223344U,
            std::uint8_t payload_type = 96) {
    return {
        0x80,
        payload_type,
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
        0x65,
    };
}

std::size_t count_diagnostic(
    const semi_stream_probe::RtpStreamAnalyzer& analyzer,
    semi_stream_probe::DiagnosticCode code) {
    std::size_t count = 0;
    for (const auto& diagnostic : analyzer.diagnostics()) {
        if (diagnostic.code == code) {
            ++count;
        }
    }
    return count;
}

void test_filters_and_ssrc_lock() {
    semi_stream_probe::RtpStreamAnalyzer analyzer;

    constexpr semi_stream_probe::Byte invalid[]{0x80};
    const auto invalid_result = analyzer.push(invalid, 0us);
    check(invalid_result.disposition ==
              semi_stream_probe::RtpPacketDisposition::invalid,
          "truncated datagram is invalid");

    const auto wrong_payload = make_packet(1, 0, 0x11111111U, 97);
    const auto wrong_payload_result = analyzer.push(wrong_payload, 1ms);
    check(wrong_payload_result.disposition ==
              semi_stream_probe::RtpPacketDisposition::
                  unexpected_payload_type,
          "unexpected payload type is ignored");

    const auto first = make_packet(10, 0, 0x11111111U);
    const auto first_result = analyzer.push(first, 2ms);
    check(first_result.disposition ==
              semi_stream_probe::RtpPacketDisposition::accepted,
          "first matching packet is accepted");
    check(first_result.packet.has_value(), "accepted packet is returned");

    const auto wrong_ssrc = make_packet(11, 3'000, 0x22222222U);
    const auto wrong_ssrc_result = analyzer.push(wrong_ssrc, 35ms);
    check(wrong_ssrc_result.disposition ==
              semi_stream_probe::RtpPacketDisposition::unexpected_ssrc,
          "unexpected SSRC is ignored");

    const auto& statistics = analyzer.statistics();
    check(statistics.datagrams_received == 4, "all datagrams are counted");
    check(statistics.packets_received == 1,
          "only matching valid packets enter session statistics");
    check(statistics.ssrc == 0x11111111U,
          "first matching packet locks active SSRC");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::rtp_invalid_packet) == 1,
          "invalid packet diagnostic");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::
                  rtp_unexpected_payload_type) == 1,
          "unexpected payload diagnostic");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::rtp_unexpected_ssrc) == 1,
          "unexpected SSRC diagnostic");
}

void test_gap_late_packet_and_duplicate() {
    semi_stream_probe::RtpStreamAnalyzer analyzer;

    const auto first = make_packet(100, 0);
    const auto after_gap = make_packet(102, 6'000);
    const auto late = make_packet(101, 3'000);

    check(analyzer.push(first, 0ms).disposition ==
              semi_stream_probe::RtpPacketDisposition::accepted,
          "first sequence accepted");
    check(analyzer.push(after_gap, 66ms).disposition ==
              semi_stream_probe::RtpPacketDisposition::accepted,
          "forward packet after gap accepted");

    const auto after_gap_statistics = analyzer.statistics();
    check(after_gap_statistics.packets_expected == 3,
          "gap expands expected sequence range");
    check(after_gap_statistics.unique_packets_received == 2,
          "two unique packets before late arrival");
    check(after_gap_statistics.packets_lost == 1,
          "one packet is provisionally lost");

    check(analyzer.push(late, 70ms).disposition ==
              semi_stream_probe::RtpPacketDisposition::out_of_order,
          "late missing packet is classified out of order");
    check(analyzer.statistics().unique_packets_received == 3,
          "late unique packet fills the sequence range");
    check(analyzer.statistics().packets_lost == 0,
          "late packet reduces final loss count");

    check(analyzer.push(late, 71ms).disposition ==
              semi_stream_probe::RtpPacketDisposition::duplicate,
          "second copy of late packet is duplicate");
    check(analyzer.statistics().packets_received == 4,
          "matching packets include duplicate and late arrivals");
    check(analyzer.statistics().duplicate_packets == 1,
          "duplicate count");
    check(analyzer.statistics().out_of_order_packets == 1,
          "out-of-order count");
    check(count_diagnostic(
              analyzer,
              semi_stream_probe::DiagnosticCode::rtp_sequence_gap) == 1,
          "sequence gap diagnostic");
}

void test_sequence_number_wrap() {
    semi_stream_probe::RtpStreamAnalyzer analyzer;
    const std::uint16_t sequences[]{65'534, 65'535, 0, 1};

    for (std::size_t index = 0; index < std::size(sequences); ++index) {
        const auto packet = make_packet(
            sequences[index], static_cast<std::uint32_t>(index * 3'000U));
        check(analyzer.push(packet,
                            std::chrono::milliseconds(
                                static_cast<std::int64_t>(index * 33U)))
                      .disposition ==
                  semi_stream_probe::RtpPacketDisposition::accepted,
              "packet across sequence wrap is accepted");
    }

    const auto& statistics = analyzer.statistics();
    check(statistics.first_sequence_number == 65'534,
          "first sequence before wrap");
    check(statistics.last_sequence_number == 1,
          "last raw sequence after wrap");
    check(statistics.packets_expected == 4,
          "expected count spans sequence wrap");
    check(statistics.packets_lost == 0, "no loss across sequence wrap");
}

void test_rfc3550_jitter_and_timestamp_wrap() {
    semi_stream_probe::RtpStreamAnalyzer analyzer;

    const auto first = make_packet(1, 0);
    const auto second = make_packet(2, 9'000);
    const auto third = make_packet(3, 18'000);
    static_cast<void>(analyzer.push(first, 0ms));
    static_cast<void>(analyzer.push(second, 100ms));
    static_cast<void>(analyzer.push(third, 210ms));

    check(analyzer.statistics().jitter_timestamp_units.has_value(),
          "jitter is available after multiple packets");
    if (analyzer.statistics().jitter_timestamp_units) {
        check(std::abs(*analyzer.statistics().jitter_timestamp_units - 56.25) <
                  0.0001,
              "RFC 3550 jitter smoothing");
    }

    semi_stream_probe::RtpStreamAnalyzer wrapping_analyzer;
    const auto before_wrap = make_packet(10, 0xFFFFFFF0U);
    const auto after_wrap = make_packet(11, 0x00000010U);
    static_cast<void>(wrapping_analyzer.push(before_wrap, 0us));
    static_cast<void>(wrapping_analyzer.push(after_wrap, 356us));
    check(wrapping_analyzer.statistics().jitter_timestamp_units.has_value(),
          "timestamp wrap still produces jitter");
    if (wrapping_analyzer.statistics().jitter_timestamp_units) {
        check(*wrapping_analyzer.statistics().jitter_timestamp_units < 0.01,
              "timestamp wrap uses signed modular delta");
    }
}

} // namespace

int main() {
    test_filters_and_ssrc_lock();
    test_gap_late_packet_and_duplicate();
    test_sequence_number_wrap();
    test_rfc3550_jitter_and_timestamp_wrap();

    if (failures != 0) {
        std::cerr << failures << " RTP stream analyzer test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_rtp_stream_analyzer_tests: all tests passed\n";
    return 0;
}
