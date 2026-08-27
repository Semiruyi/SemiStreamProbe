#include "semi_stream_probe/infrastructure/udp_receiver.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace semi_stream_probe::infrastructure {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_native_socket = INVALID_SOCKET;

[[nodiscard]] std::string socket_error(std::string_view operation) {
    return std::string(operation) + " failed with Winsock error " +
           std::to_string(WSAGetLastError());
}

class WinsockRuntime {
public:
    WinsockRuntime() {
        WSADATA data{};
        error_ = WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~WinsockRuntime() {
        if (error_ == 0) {
            WSACleanup();
        }
    }

    [[nodiscard]] int error() const noexcept { return error_; }

private:
    int error_{0};
};

[[nodiscard]] std::expected<void, std::string> ensure_socket_runtime() {
    static WinsockRuntime runtime;
    if (runtime.error() != 0) {
        return std::unexpected("WSAStartup failed with error " +
                               std::to_string(runtime.error()));
    }
    return {};
}

void close_native_socket(NativeSocket socket) noexcept {
    closesocket(socket);
}
#else
using NativeSocket = int;
constexpr NativeSocket invalid_native_socket = -1;

[[nodiscard]] std::string socket_error(std::string_view operation) {
    return std::string(operation) + " failed: " + std::strerror(errno);
}

[[nodiscard]] std::expected<void, std::string> ensure_socket_runtime() {
    return {};
}

void close_native_socket(NativeSocket socket) noexcept {
    ::close(socket);
}
#endif

[[nodiscard]] NativeSocket native_socket(std::uintptr_t handle) noexcept {
    return static_cast<NativeSocket>(handle);
}

[[nodiscard]] std::string address_error(int code) {
#ifdef _WIN32
    return "getaddrinfo failed with error " + std::to_string(code);
#else
    return std::string("getaddrinfo failed: ") + gai_strerror(code);
#endif
}

} // namespace

std::expected<UdpEndpoint, std::string>
parse_udp_endpoint(std::string_view text) {
    if (text.empty()) {
        return std::unexpected("UDP endpoint must not be empty");
    }

    std::string_view address;
    std::string_view port_text;
    if (text.front() == '[') {
        const auto closing = text.find(']');
        if (closing == std::string_view::npos || closing == 1 ||
            closing + 1 >= text.size() || text[closing + 1] != ':') {
            return std::unexpected(
                "IPv6 UDP endpoints must use [address]:port");
        }
        address = text.substr(1, closing - 1);
        port_text = text.substr(closing + 2);
    } else {
        const auto separator = text.rfind(':');
        if (separator == std::string_view::npos || separator == 0 ||
            separator + 1 >= text.size()) {
            return std::unexpected("UDP endpoint must use address:port");
        }
        address = text.substr(0, separator);
        port_text = text.substr(separator + 1);
        if (address.find(':') != std::string_view::npos) {
            return std::unexpected(
                "IPv6 UDP endpoints must use [address]:port");
        }
    }

    std::uint32_t port = 0;
    const auto parsed = std::from_chars(port_text.data(),
                                        port_text.data() + port_text.size(),
                                        port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() +
                                                   port_text.size() ||
        port == 0 || port > std::numeric_limits<std::uint16_t>::max()) {
        return std::unexpected("UDP port must be an integer in the range 1..65535");
    }

    return UdpEndpoint{
        .address = std::string(address),
        .port = static_cast<std::uint16_t>(port),
    };
}

std::string format_udp_endpoint(const UdpEndpoint& endpoint) {
    if (endpoint.address.find(':') != std::string::npos) {
        return "[" + endpoint.address + "]:" +
               std::to_string(endpoint.port);
    }
    return endpoint.address + ":" + std::to_string(endpoint.port);
}

UdpReceiver::UdpReceiver(std::uintptr_t handle) noexcept : handle_(handle) {}

UdpReceiver::UdpReceiver(UdpReceiver&& other) noexcept
    : handle_(std::exchange(other.handle_, invalid_handle)) {}

UdpReceiver& UdpReceiver::operator=(UdpReceiver&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, invalid_handle);
    }
    return *this;
}

UdpReceiver::~UdpReceiver() { close(); }

std::expected<UdpReceiver, std::string>
UdpReceiver::bind(const UdpEndpoint& endpoint) {
    if (const auto runtime = ensure_socket_runtime(); !runtime) {
        return std::unexpected(runtime.error());
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addresses = nullptr;
    const auto service = std::to_string(endpoint.port);
    const char* node = endpoint.address == "*" ? nullptr :
                                                    endpoint.address.c_str();
    const int result = getaddrinfo(node, service.c_str(), &hints, &addresses);
    if (result != 0) {
        return std::unexpected(address_error(result));
    }

    NativeSocket bound = invalid_native_socket;
    for (auto* current = addresses; current != nullptr;
         current = current->ai_next) {
        const NativeSocket candidate = ::socket(current->ai_family,
                                                current->ai_socktype,
                                                current->ai_protocol);
        if (candidate == invalid_native_socket) {
            continue;
        }
        if (::bind(candidate, current->ai_addr,
                   static_cast<int>(current->ai_addrlen)) == 0) {
            bound = candidate;
            break;
        }
        close_native_socket(candidate);
    }
    freeaddrinfo(addresses);

    if (bound == invalid_native_socket) {
        return std::unexpected(socket_error("UDP bind"));
    }
    return UdpReceiver(static_cast<std::uintptr_t>(bound));
}

std::expected<std::optional<std::size_t>, std::string>
UdpReceiver::receive(std::span<Byte> buffer,
                     std::chrono::milliseconds timeout) {
    if (handle_ == invalid_handle) {
        return std::unexpected("UDP receiver is not open");
    }
    if (buffer.empty()) {
        return std::unexpected("UDP receive buffer must not be empty");
    }

    const NativeSocket socket = native_socket(handle_);
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);

    const auto timeout_count = timeout.count();
    timeval wait{
        .tv_sec = static_cast<long>(timeout_count / 1000),
        .tv_usec = static_cast<long>((timeout_count % 1000) * 1000),
    };
#ifdef _WIN32
    const int ready = select(0, &readable, nullptr, nullptr, &wait);
#else
    const int ready = select(socket + 1, &readable, nullptr, nullptr, &wait);
#endif
    if (ready == 0) {
        return std::optional<std::size_t>{};
    }
    if (ready < 0) {
#ifndef _WIN32
        if (errno == EINTR) {
            return std::optional<std::size_t>{};
        }
#endif
        return std::unexpected(socket_error("UDP select"));
    }

#ifdef _WIN32
    const int capacity = buffer.size() >
                                 static_cast<std::size_t>(
                                     std::numeric_limits<int>::max())
                             ? std::numeric_limits<int>::max()
                             : static_cast<int>(buffer.size());
    const int received = recv(socket, reinterpret_cast<char*>(buffer.data()),
                              capacity, 0);
    if (received == SOCKET_ERROR) {
        return std::unexpected(socket_error("UDP receive"));
    }
#else
    const auto received = recv(socket, buffer.data(), buffer.size(), 0);
    if (received < 0) {
        return std::unexpected(socket_error("UDP receive"));
    }
#endif
    return std::optional<std::size_t>{static_cast<std::size_t>(received)};
}

void UdpReceiver::close() noexcept {
    if (handle_ == invalid_handle) {
        return;
    }
    close_native_socket(native_socket(handle_));
    handle_ = invalid_handle;
}

} // namespace semi_stream_probe::infrastructure
