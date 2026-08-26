#include "semi_stream_probe/core/rtp_stream_analyzer.hpp"

#include "semi_stream_probe/core/parse_error.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace semi_stream_probe {

namespace {

constexpr std::uint64_t unseen_extended_sequence =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::uint32_t sequence_modulus = 1U << 16U;
constexpr std::uint16_t half_sequence_space = 1U << 15U;
constexpr double microseconds_per_second = 1'000'000.0;
constexpr double jitter_smoothing_factor = 16.0;

[[nodiscard]] DiagnosticLocation location_for(const RtpPacket& packet) {
    return DiagnosticLocation{
        .input_byte_offset = std::nullopt,
        .bit_offset = std::nullopt,
        .nal_index = std::nullopt,
        .rtp_sequence_number = packet.sequence_number,
        .ssrc = packet.ssrc,
        .rtp_timestamp = packet.timestamp,
    };
}

[[nodiscard]] std::int64_t
signed_timestamp_delta(std::uint32_t current, std::uint32_t previous) noexcept {
    const auto raw_delta = current - previous;
    if (raw_delta <=
        static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return static_cast<std::int64_t>(raw_delta);
    }
    return static_cast<std::int64_t>(raw_delta) -
           static_cast<std::int64_t>(std::uint64_t{1} << 32U);
}

} // namespace

RtpStreamAnalyzer::RtpStreamAnalyzer(RtpStreamAnalyzerConfig config)
    : config_(config),
      statistics_{},
      seen_extended_sequences_(sequence_modulus, unseen_extended_sequence) {
    statistics_.ssrc = config.ssrc;
    statistics_.payload_type = config.payload_type;
    statistics_.clock_rate_hz = config.clock_rate_hz;
}

RtpStreamPacketResult
RtpStreamAnalyzer::push(ByteView datagram,
                        std::chrono::microseconds arrival_time) {
    ++statistics_.datagrams_received;
    auto parsed = parse_rtp_packet(datagram);
    if (!parsed) {
        const auto& error = parsed.error();
        diagnostics_.push_back(Diagnostic{
            .severity = DiagnosticSeverity::error,
            .code = DiagnosticCode::rtp_invalid_packet,
            .summary = "RTP packet is invalid",
            .evidence = error.message,
            .impact = "the datagram was ignored",
            .recovery = "analysis continues with the next datagram",
            .location = {
                .input_byte_offset = error.byte_offset,
                .bit_offset = std::nullopt,
                .nal_index = std::nullopt,
                .rtp_sequence_number = error.rtp_sequence_number,
                .ssrc = std::nullopt,
                .rtp_timestamp = std::nullopt,
            },
        });
        return {.disposition = RtpPacketDisposition::invalid,
                .packet = std::nullopt};
    }

    RtpPacket packet = std::move(*parsed);
    if (packet.payload_type != config_.payload_type) {
        diagnostics_.push_back(Diagnostic{
            .severity = DiagnosticSeverity::warning,
            .code = DiagnosticCode::rtp_unexpected_payload_type,
            .summary = "RTP payload type does not match the configured stream",
            .evidence = "configured payload type " +
                        std::to_string(config_.payload_type) + ", received " +
                        std::to_string(packet.payload_type),
            .impact = "the packet was ignored",
            .recovery = "analysis continues with packets using the configured payload type",
            .location = location_for(packet),
        });
        return {.disposition = RtpPacketDisposition::unexpected_payload_type,
                .packet = std::nullopt};
    }

    if (!statistics_.ssrc) {
        statistics_.ssrc = packet.ssrc;
    } else if (packet.ssrc != *statistics_.ssrc) {
        diagnostics_.push_back(Diagnostic{
            .severity = DiagnosticSeverity::warning,
            .code = DiagnosticCode::rtp_unexpected_ssrc,
            .summary = "RTP SSRC does not match the active stream",
            .evidence = "active SSRC " + std::to_string(*statistics_.ssrc) +
                        ", received " + std::to_string(packet.ssrc),
            .impact = "the packet was ignored",
            .recovery = "analysis continues with packets from the active SSRC",
            .location = location_for(packet),
        });
        return {.disposition = RtpPacketDisposition::unexpected_ssrc,
                .packet = std::nullopt};
    }

    ++statistics_.packets_received;
    if (!highest_extended_sequence_) {
        initialize_sequence(packet.sequence_number);
        update_jitter(packet, arrival_time);
        return {.disposition = RtpPacketDisposition::accepted,
                .packet = std::move(packet)};
    }

    const auto highest_raw = static_cast<std::uint16_t>(
        *highest_extended_sequence_ % sequence_modulus);
    const auto forward_delta = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(packet.sequence_number) - highest_raw);

    if (forward_delta == 0) {
        ++statistics_.duplicate_packets;
        diagnostics_.push_back(Diagnostic{
            .severity = DiagnosticSeverity::warning,
            .code = DiagnosticCode::rtp_duplicate_packet,
            .summary = "RTP packet is duplicated",
            .evidence = "RTP sequence " +
                        std::to_string(packet.sequence_number) +
                        " was already received",
            .impact = "the duplicate packet was ignored",
            .recovery = std::nullopt,
            .location = location_for(packet),
        });
        return {.disposition = RtpPacketDisposition::duplicate,
                .packet = std::nullopt};
    }

    if (forward_delta < half_sequence_space) {
        const auto extended_sequence =
            *highest_extended_sequence_ + forward_delta;
        if (forward_delta > 1) {
            const auto missing = static_cast<std::uint64_t>(forward_delta - 1U);
            const auto expected = static_cast<std::uint16_t>(highest_raw + 1U);
            diagnostics_.push_back(Diagnostic{
                .severity = DiagnosticSeverity::error,
                .code = DiagnosticCode::rtp_sequence_gap,
                .summary = "RTP sequence is discontinuous",
                .evidence = "expected RTP sequence " +
                            std::to_string(expected) + ", received " +
                            std::to_string(packet.sequence_number) + "; " +
                            std::to_string(missing) +
                            (missing == 1 ? " packet is missing"
                                          : " packets are missing"),
                .impact = "one or more media payloads may be unavailable",
                .recovery = "sequence tracking continued from RTP sequence " +
                            std::to_string(packet.sequence_number),
                .location = location_for(packet),
            });
        }
        highest_extended_sequence_ = extended_sequence;
        statistics_.last_sequence_number = packet.sequence_number;
        seen_extended_sequences_[packet.sequence_number] = extended_sequence;
        ++statistics_.unique_packets_received;
        update_loss_statistics();
        update_jitter(packet, arrival_time);
        return {.disposition = RtpPacketDisposition::accepted,
                .packet = std::move(packet)};
    }

    const auto behind = static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(highest_raw) - packet.sequence_number);
    std::optional<std::uint64_t> candidate;
    if (static_cast<std::uint64_t>(behind) <= *highest_extended_sequence_) {
        const auto value = *highest_extended_sequence_ - behind;
        if (value >= *base_extended_sequence_) {
            candidate = value;
        }
    }

    if (candidate &&
        seen_extended_sequences_[packet.sequence_number] == *candidate) {
        ++statistics_.duplicate_packets;
        diagnostics_.push_back(Diagnostic{
            .severity = DiagnosticSeverity::warning,
            .code = DiagnosticCode::rtp_duplicate_packet,
            .summary = "RTP packet is duplicated",
            .evidence = "RTP sequence " +
                        std::to_string(packet.sequence_number) +
                        " was already received",
            .impact = "the duplicate packet was ignored",
            .recovery = std::nullopt,
            .location = location_for(packet),
        });
        return {.disposition = RtpPacketDisposition::duplicate,
                .packet = std::nullopt};
    }

    ++statistics_.out_of_order_packets;
    if (candidate) {
        seen_extended_sequences_[packet.sequence_number] = *candidate;
        ++statistics_.unique_packets_received;
        update_loss_statistics();
    }
    diagnostics_.push_back(Diagnostic{
        .severity = DiagnosticSeverity::warning,
        .code = DiagnosticCode::rtp_out_of_order_packet,
        .summary = "RTP packet arrived out of order",
        .evidence = "RTP sequence " +
                    std::to_string(packet.sequence_number) +
                    " arrived after RTP sequence " +
                    std::to_string(highest_raw),
        .impact = "the late packet was not forwarded for H.264 reassembly",
        .recovery = "sequence analysis continues from the highest received packet",
        .location = location_for(packet),
    });
    return {.disposition = RtpPacketDisposition::out_of_order,
            .packet = std::nullopt};
}

const RtpSessionStatistics& RtpStreamAnalyzer::statistics() const noexcept {
    return statistics_;
}

std::span<const Diagnostic> RtpStreamAnalyzer::diagnostics() const noexcept {
    return diagnostics_;
}

void RtpStreamAnalyzer::initialize_sequence(std::uint16_t sequence_number) {
    const auto extended_sequence = static_cast<std::uint64_t>(sequence_number);
    base_extended_sequence_ = extended_sequence;
    highest_extended_sequence_ = extended_sequence;
    seen_extended_sequences_[sequence_number] = extended_sequence;
    statistics_.first_sequence_number = sequence_number;
    statistics_.last_sequence_number = sequence_number;
    statistics_.unique_packets_received = 1;
    update_loss_statistics();
}

void RtpStreamAnalyzer::update_loss_statistics() noexcept {
    if (!base_extended_sequence_ || !highest_extended_sequence_) {
        return;
    }
    statistics_.packets_expected =
        *highest_extended_sequence_ - *base_extended_sequence_ + 1U;
    statistics_.packets_lost =
        statistics_.packets_expected > statistics_.unique_packets_received
            ? statistics_.packets_expected -
                  statistics_.unique_packets_received
            : 0;
}

void RtpStreamAnalyzer::update_jitter(
    const RtpPacket& packet,
    std::chrono::microseconds arrival_time) noexcept {
    if (previous_arrival_time_ && previous_rtp_timestamp_ &&
        config_.clock_rate_hz != 0) {
        const auto arrival_delta_us =
            static_cast<double>((arrival_time - *previous_arrival_time_).count());
        const auto arrival_delta_timestamp_units =
            arrival_delta_us * static_cast<double>(config_.clock_rate_hz) /
            microseconds_per_second;
        const auto rtp_delta = static_cast<double>(signed_timestamp_delta(
            packet.timestamp, *previous_rtp_timestamp_));
        const auto difference =
            std::abs(arrival_delta_timestamp_units - rtp_delta);
        jitter_timestamp_units_ +=
            (difference - jitter_timestamp_units_) / jitter_smoothing_factor;
        statistics_.jitter_timestamp_units = jitter_timestamp_units_;
    }
    previous_arrival_time_ = arrival_time;
    previous_rtp_timestamp_ = packet.timestamp;
}

} // namespace semi_stream_probe
