#pragma once

#include "semi_stream_probe/application/report.hpp"
#include "semi_stream_probe/infrastructure/udp_receiver.hpp"

#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <string>

namespace semi_stream_probe::application {

struct ListenOptions {
    infrastructure::UdpEndpoint endpoint;
    std::uint8_t payload_type{96};
    std::uint32_t clock_rate_hz{90'000};
    std::optional<std::chrono::milliseconds> duration;
};

using StopRequested = std::function<bool()>;

[[nodiscard]] std::expected<AnalysisReport, std::string>
listen_udp_h264(const ListenOptions& options,
                StopRequested stop_requested = {});

} // namespace semi_stream_probe::application
