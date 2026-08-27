#pragma once

#include "semi_stream_probe/core/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace semi_stream_probe::infrastructure {

struct UdpEndpoint {
    std::string address;
    std::uint16_t port{0};
};

[[nodiscard]] std::expected<UdpEndpoint, std::string>
parse_udp_endpoint(std::string_view text);

[[nodiscard]] std::string format_udp_endpoint(const UdpEndpoint& endpoint);

class UdpReceiver {
public:
    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;
    UdpReceiver(UdpReceiver&& other) noexcept;
    UdpReceiver& operator=(UdpReceiver&& other) noexcept;
    ~UdpReceiver();

    [[nodiscard]] static std::expected<UdpReceiver, std::string>
    bind(const UdpEndpoint& endpoint);

    // A disengaged optional means the timeout elapsed without a datagram.
    [[nodiscard]] std::expected<std::optional<std::size_t>, std::string>
    receive(std::span<Byte> buffer, std::chrono::milliseconds timeout);

private:
    static constexpr std::uintptr_t invalid_handle =
        std::numeric_limits<std::uintptr_t>::max();

    explicit UdpReceiver(std::uintptr_t handle) noexcept;
    void close() noexcept;

    std::uintptr_t handle_{invalid_handle};
};

} // namespace semi_stream_probe::infrastructure
