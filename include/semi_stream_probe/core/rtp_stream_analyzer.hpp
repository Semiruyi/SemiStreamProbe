#pragma once

#include "semi_stream_probe/core/diagnostic.hpp"
#include "semi_stream_probe/core/rtp.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace semi_stream_probe {

struct RtpStreamAnalyzerConfig {
    std::uint8_t payload_type{96};
    std::uint32_t clock_rate_hz{90'000};
    std::optional<std::uint32_t> ssrc;
};

struct RtpSessionStatistics {
    std::optional<std::uint32_t> ssrc;
    std::uint8_t payload_type{96};
    std::uint32_t clock_rate_hz{90'000};
    std::optional<std::uint16_t> first_sequence_number;
    std::optional<std::uint16_t> last_sequence_number;
    std::uint64_t datagrams_received{0};
    std::uint64_t packets_received{0};
    std::uint64_t unique_packets_received{0};
    std::uint64_t packets_expected{0};
    std::uint64_t packets_lost{0};
    std::uint64_t duplicate_packets{0};
    std::uint64_t out_of_order_packets{0};
    std::optional<double> jitter_timestamp_units;
};

enum class RtpPacketDisposition {
    accepted,
    invalid,
    unexpected_payload_type,
    unexpected_ssrc,
    duplicate,
    out_of_order,
};

struct RtpStreamPacketResult {
    RtpPacketDisposition disposition{RtpPacketDisposition::invalid};
    std::optional<RtpPacket> packet;
};

class RtpStreamAnalyzer {
public:
    explicit RtpStreamAnalyzer(RtpStreamAnalyzerConfig config = {});

    // arrival_time must use one monotonic clock domain for the lifetime of
    // this analyzer. Only accepted in-order packets are returned downstream.
    [[nodiscard]] RtpStreamPacketResult
    push(ByteView datagram, std::chrono::microseconds arrival_time);

    [[nodiscard]] const RtpSessionStatistics& statistics() const noexcept;
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept;

private:
    void initialize_sequence(std::uint16_t sequence_number);
    void update_loss_statistics() noexcept;
    void update_jitter(const RtpPacket& packet,
                       std::chrono::microseconds arrival_time) noexcept;

    RtpStreamAnalyzerConfig config_;
    RtpSessionStatistics statistics_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<std::uint64_t> seen_extended_sequences_;
    std::optional<std::uint64_t> base_extended_sequence_;
    std::optional<std::uint64_t> highest_extended_sequence_;
    std::optional<std::chrono::microseconds> previous_arrival_time_;
    std::optional<std::uint32_t> previous_rtp_timestamp_;
    double jitter_timestamp_units_{0.0};
};

} // namespace semi_stream_probe
