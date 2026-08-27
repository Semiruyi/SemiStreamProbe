#include "semi_stream_probe/application/listen.hpp"

#include "semi_stream_probe/application/rtp_analysis.hpp"
#include "semi_stream_probe/core/h264_rtp_stream_analyzer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace semi_stream_probe::application {

std::expected<AnalysisReport, std::string>
listen_udp_h264(const ListenOptions& options,
                StopRequested stop_requested) {
    if (options.clock_rate_hz == 0) {
        return std::unexpected("RTP clock rate must be greater than zero");
    }

    auto receiver = infrastructure::UdpReceiver::bind(options.endpoint);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }

    H264RtpStreamAnalyzer analyzer(H264RtpStreamAnalyzerConfig{
        .rtp = {
            .payload_type = options.payload_type,
            .clock_rate_hz = options.clock_rate_hz,
            .ssrc = std::nullopt,
        },
    });

    constexpr std::size_t maximum_udp_datagram_size = 65'536;
    std::array<Byte, maximum_udp_datagram_size> buffer{};
    const auto listen_started = std::chrono::steady_clock::now();
    std::optional<std::chrono::steady_clock::time_point> first_datagram_time;
    auto analysis_ended = listen_started;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (stop_requested && stop_requested()) {
            analysis_ended = now;
            break;
        }
        if (options.duration && now - listen_started >= *options.duration) {
            analysis_ended = now;
            break;
        }

        auto wait = std::chrono::milliseconds(200);
        if (options.duration) {
            const auto remaining = std::chrono::duration_cast<
                std::chrono::milliseconds>(*options.duration -
                                           (now - listen_started));
            wait = std::max(std::chrono::milliseconds(0),
                            std::min(wait, remaining));
        }

        auto received = receiver->receive(buffer, wait);
        if (!received) {
            return std::unexpected(received.error());
        }
        if (!*received) {
            continue;
        }

        const auto arrival = std::chrono::steady_clock::now();
        if (!first_datagram_time) {
            first_datagram_time = arrival;
        }
        const auto arrival_us = std::chrono::duration_cast<
            std::chrono::microseconds>(arrival.time_since_epoch());
        static_cast<void>(analyzer.push(
            ByteView(buffer.data(), **received), arrival_us));
        analysis_ended = arrival;
    }

    analyzer.finish();
    std::optional<std::uint64_t> duration_us;
    if (first_datagram_time) {
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(analysis_ended - *first_datagram_time);
        duration_us = static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, elapsed.count()));
    }

    return make_rtp_analysis_report(
        analyzer,
        "udp://" + infrastructure::format_udp_endpoint(options.endpoint),
        duration_us);
}

} // namespace semi_stream_probe::application
