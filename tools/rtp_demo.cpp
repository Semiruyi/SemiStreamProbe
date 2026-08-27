#include "semi_stream_probe/infrastructure/udp_receiver.hpp"

#include "semi_stream_probe/core/types.hpp"

#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using semi_stream_probe::Byte;
using semi_stream_probe::ByteBuffer;
using semi_stream_probe::ByteView;
using semi_stream_probe::infrastructure::UdpEndpoint;

constexpr std::array<Byte, 10> sps{
    0x67, 0x42, 0x00, 0x28, 0xF4, 0x03, 0xC0, 0x11, 0x3F, 0x2A,
};
constexpr std::array<Byte, 4> pps{0x68, 0xCE, 0x3C, 0x80};
constexpr std::array<Byte, 5> idr{0x65, 0x88, 0x84, 0x0A, 0x80};
constexpr std::array<Byte, 4> p_slice{0x61, 0xE2, 0x22, 0xA0};

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
void close_socket(NativeSocket socket) noexcept { closesocket(socket); }
[[nodiscard]] std::string socket_error(std::string_view operation) {
    return std::string(operation) + " failed with Winsock error " +
           std::to_string(WSAGetLastError());
}
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
void close_socket(NativeSocket socket) noexcept { ::close(socket); }
[[nodiscard]] std::string socket_error(std::string_view operation) {
    return std::string(operation) + " failed";
}
#endif

class UdpSender {
public:
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;
    UdpSender(UdpSender&& other) noexcept
        : socket_(std::exchange(other.socket_, invalid_socket)),
          destination_(other.destination_),
          destination_size_(other.destination_size_),
          runtime_acquired_(std::exchange(other.runtime_acquired_, false)) {}
    ~UdpSender() {
        if (socket_ != invalid_socket) {
            close_socket(socket_);
        }
#ifdef _WIN32
        if (runtime_acquired_) {
            WSACleanup();
        }
#endif
    }

    [[nodiscard]] static std::expected<UdpSender, std::string>
    create(const UdpEndpoint& endpoint) {
#ifdef _WIN32
        WSADATA data{};
        if (const int error = WSAStartup(MAKEWORD(2, 2), &data); error != 0) {
            return std::unexpected("WSAStartup failed with error " +
                                   std::to_string(error));
        }
#endif
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        addrinfo* addresses = nullptr;
        const auto service = std::to_string(endpoint.port);
        const int result = getaddrinfo(endpoint.address.c_str(),
                                       service.c_str(), &hints, &addresses);
        if (result != 0) {
#ifdef _WIN32
            WSACleanup();
#endif
            return std::unexpected("could not resolve UDP target");
        }

        NativeSocket socket = invalid_socket;
        sockaddr_storage destination{};
        int destination_size = 0;
        for (auto* current = addresses; current != nullptr;
             current = current->ai_next) {
            if (current->ai_addrlen > sizeof(destination)) {
                continue;
            }
            socket = ::socket(current->ai_family, current->ai_socktype,
                              current->ai_protocol);
            if (socket == invalid_socket) {
                continue;
            }
            std::memcpy(&destination, current->ai_addr,
                        current->ai_addrlen);
            destination_size = static_cast<int>(current->ai_addrlen);
            break;
        }
        freeaddrinfo(addresses);
        if (socket == invalid_socket) {
#ifdef _WIN32
            WSACleanup();
#endif
            return std::unexpected(socket_error("UDP socket creation"));
        }
        return UdpSender(socket, destination, destination_size,
#ifdef _WIN32
                         true
#else
                         false
#endif
        );
    }

    [[nodiscard]] std::expected<void, std::string> send(ByteView datagram) {
#ifdef _WIN32
        const int size = static_cast<int>(datagram.size());
        const int sent = sendto(
            socket_, reinterpret_cast<const char*>(datagram.data()), size, 0,
            reinterpret_cast<const sockaddr*>(&destination_),
            destination_size_);
        if (sent == SOCKET_ERROR || sent != size) {
            return std::unexpected(socket_error("UDP send"));
        }
#else
        const auto sent = sendto(
            socket_, datagram.data(), datagram.size(), 0,
            reinterpret_cast<const sockaddr*>(&destination_),
            static_cast<socklen_t>(destination_size_));
        if (sent < 0 || static_cast<std::size_t>(sent) != datagram.size()) {
            return std::unexpected(socket_error("UDP send"));
        }
#endif
        return {};
    }

private:
    UdpSender(NativeSocket socket,
              sockaddr_storage destination,
              int destination_size,
              bool runtime_acquired) noexcept
        : socket_(socket),
          destination_(destination),
          destination_size_(destination_size),
          runtime_acquired_(runtime_acquired) {}

    NativeSocket socket_{invalid_socket};
    sockaddr_storage destination_{};
    int destination_size_{0};
    bool runtime_acquired_{false};
};

enum class Scenario {
    normal,
    fu_middle_loss,
    fu_missing_start,
    fu_missing_end,
};

struct SendOptions {
    UdpEndpoint endpoint;
    Scenario scenario{Scenario::normal};
    std::uint8_t payload_type{96};
    std::uint16_t sequence_start{1000};
    std::chrono::milliseconds interval{20};
};

void print_usage() {
    std::cout
        << "SemiStreamProbe deterministic demo source\n"
        << "Usage:\n"
        << "  semistreamprobe_demo annex-b --output <file.h264>\n"
        << "  semistreamprobe_demo send --target <address:port>\n"
        << "      [--scenario normal|fu-middle-loss|fu-missing-start|fu-missing-end]\n"
        << "      [--payload-type <0..127>] [--sequence-start <0..65535>]\n"
        << "      [--interval-ms <0..1000>]\n";
}

template <typename Integer>
[[nodiscard]] std::expected<Integer, std::string>
parse_integer(std::string_view text,
              std::string_view option,
              Integer minimum,
              Integer maximum) {
    Integer value{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value < minimum || value > maximum) {
        return std::unexpected(std::string(option) + " is out of range");
    }
    return value;
}

[[nodiscard]] std::expected<Scenario, std::string>
parse_scenario(std::string_view text) {
    if (text == "normal") {
        return Scenario::normal;
    }
    if (text == "fu-middle-loss") {
        return Scenario::fu_middle_loss;
    }
    if (text == "fu-missing-start") {
        return Scenario::fu_missing_start;
    }
    if (text == "fu-missing-end") {
        return Scenario::fu_missing_end;
    }
    return std::unexpected("invalid --scenario value: " + std::string(text));
}

[[nodiscard]] ByteBuffer make_rtp_packet(std::uint16_t sequence,
                                         std::uint32_t timestamp,
                                         ByteView payload,
                                         std::uint8_t payload_type,
                                         bool marker) {
    constexpr std::uint32_t ssrc = 0x11223344U;
    ByteBuffer packet{
        0x80,
        static_cast<Byte>(payload_type | (marker ? 0x80U : 0U)),
        static_cast<Byte>(sequence >> 8U),
        static_cast<Byte>(sequence & 0xFFU),
        static_cast<Byte>(timestamp >> 24U),
        static_cast<Byte>((timestamp >> 16U) & 0xFFU),
        static_cast<Byte>((timestamp >> 8U) & 0xFFU),
        static_cast<Byte>(timestamp & 0xFFU),
        static_cast<Byte>(ssrc >> 24U),
        static_cast<Byte>((ssrc >> 16U) & 0xFFU),
        static_cast<Byte>((ssrc >> 8U) & 0xFFU),
        static_cast<Byte>(ssrc & 0xFFU),
    };
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

[[nodiscard]] std::array<ByteBuffer, 3> make_idr_fu_a() {
    const Byte indicator = static_cast<Byte>((idr[0] & 0xE0U) | 28U);
    const Byte nal_type = static_cast<Byte>(idr[0] & 0x1FU);
    return {
        ByteBuffer{indicator, static_cast<Byte>(0x80U | nal_type), idr[1]},
        ByteBuffer{indicator, nal_type, idr[2]},
        ByteBuffer{indicator, static_cast<Byte>(0x40U | nal_type),
                   idr[3], idr[4]},
    };
}

[[nodiscard]] int write_annex_b(const std::filesystem::path& output) {
    if (const auto parent = output.parent_path(); !parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            std::cerr << "Could not create output directory: "
                      << error.message() << '\n';
            return 1;
        }
    }
    std::ofstream stream(output, std::ios::binary);
    if (!stream) {
        std::cerr << "Could not open output: " << output << '\n';
        return 1;
    }
    constexpr std::array<Byte, 4> start_code{0, 0, 0, 1};
    for (const ByteView nal : {ByteView(sps), ByteView(pps), ByteView(idr),
                               ByteView(p_slice)}) {
        stream.write(reinterpret_cast<const char*>(start_code.data()),
                     static_cast<std::streamsize>(start_code.size()));
        stream.write(reinterpret_cast<const char*>(nal.data()),
                     static_cast<std::streamsize>(nal.size()));
    }
    if (!stream) {
        std::cerr << "Could not write output: " << output << '\n';
        return 1;
    }
    std::cout << "wrote deterministic Annex-B fixture to " << output << '\n';
    return 0;
}

[[nodiscard]] int send_scenario(const SendOptions& options) {
    auto sender = UdpSender::create(options.endpoint);
    if (!sender) {
        std::cerr << sender.error() << '\n';
        return 1;
    }
    std::uint16_t sequence = options.sequence_start;
    std::uint64_t sent_count = 0;

    auto send = [&](ByteView payload,
                    std::uint32_t timestamp,
                    bool marker,
                    std::string_view label)
        -> std::expected<void, std::string> {
        auto packet = make_rtp_packet(sequence, timestamp, payload,
                                      options.payload_type, marker);
        if (auto result = sender->send(packet); !result) {
            return result;
        }
        std::cout << "sent seq=" << sequence << ' ' << label << '\n';
        sequence = static_cast<std::uint16_t>(sequence + 1U);
        ++sent_count;
        std::this_thread::sleep_for(options.interval);
        return {};
    };

    const auto fragments = make_idr_fu_a();
    if (auto result = send(sps, 0, false, "SPS"); !result) {
        std::cerr << result.error() << '\n';
        return 1;
    }
    if (auto result = send(pps, 0, false, "PPS"); !result) {
        std::cerr << result.error() << '\n';
        return 1;
    }

    auto checked_send = [&](ByteView payload,
                            std::uint32_t timestamp,
                            bool marker,
                            std::string_view label) -> bool {
        if (auto result = send(payload, timestamp, marker, label); !result) {
            std::cerr << result.error() << '\n';
            return false;
        }
        return true;
    };

    switch (options.scenario) {
    case Scenario::normal:
        if (!checked_send(fragments[0], 3000, false, "IDR FU-A start") ||
            !checked_send(fragments[1], 3000, false, "IDR FU-A middle") ||
            !checked_send(fragments[2], 3000, true, "IDR FU-A end") ||
            !checked_send(p_slice, 6000, true, "P slice")) {
            return 1;
        }
        break;
    case Scenario::fu_middle_loss:
        if (!checked_send(fragments[0], 3000, false, "IDR FU-A start")) {
            return 1;
        }
        std::cout << "drop seq=" << sequence << " IDR FU-A middle\n";
        sequence = static_cast<std::uint16_t>(sequence + 1U);
        if (!checked_send(fragments[2], 3000, true, "IDR FU-A end") ||
            !checked_send(idr, 6000, true, "recovery IDR")) {
            return 1;
        }
        break;
    case Scenario::fu_missing_start:
        if (!checked_send(fragments[1], 3000, false,
                          "IDR FU-A middle without start") ||
            !checked_send(fragments[2], 3000, true,
                          "IDR FU-A end without start") ||
            !checked_send(idr, 6000, true, "recovery IDR")) {
            return 1;
        }
        break;
    case Scenario::fu_missing_end:
        if (!checked_send(fragments[0], 3000, false, "IDR FU-A start") ||
            !checked_send(fragments[1], 3000, false, "IDR FU-A middle")) {
            return 1;
        }
        break;
    }

    std::cout << "sent " << sent_count << " datagrams to "
              << semi_stream_probe::infrastructure::format_udp_endpoint(
                     options.endpoint)
              << '\n';
    return 0;
}

[[nodiscard]] int run_annex_b(int argc, char* argv[]) {
    if (argc != 4 || std::string_view(argv[2]) != "--output") {
        print_usage();
        return 2;
    }
    return write_annex_b(argv[3]);
}

[[nodiscard]] int run_send(int argc, char* argv[]) {
    std::optional<UdpEndpoint> endpoint;
    SendOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (index + 1 >= argc) {
            std::cerr << "Missing value for " << argument << '\n';
            return 2;
        }
        const std::string_view value(argv[++index]);
        if (argument == "--target") {
            auto parsed = semi_stream_probe::infrastructure::
                parse_udp_endpoint(value);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            endpoint = std::move(*parsed);
        } else if (argument == "--scenario") {
            auto parsed = parse_scenario(value);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            options.scenario = *parsed;
        } else if (argument == "--payload-type") {
            auto parsed = parse_integer<std::uint16_t>(value, argument, 0, 127);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            options.payload_type = static_cast<std::uint8_t>(*parsed);
        } else if (argument == "--sequence-start") {
            auto parsed = parse_integer<std::uint32_t>(value, argument, 0,
                                                       65'535);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            options.sequence_start = static_cast<std::uint16_t>(*parsed);
        } else if (argument == "--interval-ms") {
            auto parsed = parse_integer<std::uint32_t>(value, argument, 0,
                                                       1000);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            options.interval = std::chrono::milliseconds(*parsed);
        } else {
            std::cerr << "Unknown option: " << argument << '\n';
            return 2;
        }
    }
    if (!endpoint) {
        std::cerr << "Missing required option --target\n";
        return 2;
    }
    options.endpoint = std::move(*endpoint);
    return send_scenario(options);
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        print_usage();
        return argc < 2 ? 2 : 0;
    }
    if (std::string_view(argv[1]) == "annex-b") {
        return run_annex_b(argc, argv);
    }
    if (std::string_view(argv[1]) == "send") {
        return run_send(argc, argv);
    }
    print_usage();
    return 2;
}
