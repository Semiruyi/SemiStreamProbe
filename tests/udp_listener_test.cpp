#include "semi_stream_probe/application/listen.hpp"
#include "semi_stream_probe/application/report.hpp"
#include "semi_stream_probe/infrastructure/udp_receiver.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_endpoint_parsing() {
    using semi_stream_probe::infrastructure::format_udp_endpoint;
    using semi_stream_probe::infrastructure::parse_udp_endpoint;

    const auto ipv4 = parse_udp_endpoint("127.0.0.1:5004");
    check(ipv4 && ipv4->address == "127.0.0.1" && ipv4->port == 5004,
          "IPv4 endpoint");
    if (ipv4) {
        check(format_udp_endpoint(*ipv4) == "127.0.0.1:5004",
              "IPv4 endpoint formatting");
    }

    const auto ipv6 = parse_udp_endpoint("[::1]:5004");
    check(ipv6 && ipv6->address == "::1" && ipv6->port == 5004,
          "IPv6 endpoint");
    if (ipv6) {
        check(format_udp_endpoint(*ipv6) == "[::1]:5004",
              "IPv6 endpoint formatting");
    }

    check(!parse_udp_endpoint("::1:5004"), "unbracketed IPv6 rejected");
    check(!parse_udp_endpoint("127.0.0.1:0"), "zero port rejected");
    check(!parse_udp_endpoint("127.0.0.1:65536"),
          "out-of-range port rejected");
    check(!parse_udp_endpoint("127.0.0.1:http"),
          "non-numeric port rejected");
}

void test_empty_local_listener() {
    const auto report = semi_stream_probe::application::listen_udp_h264({
        .endpoint = {.address = "127.0.0.1", .port = 0},
        .payload_type = 96,
        .clock_rate_hz = 90'000,
        .duration = std::chrono::milliseconds(10),
    });

    check(report.has_value(), "local UDP listener should bind and time out");
    if (!report) {
        std::cerr << report.error() << '\n';
        return;
    }
    check(report->analysis.kind ==
              semi_stream_probe::application::AnalysisKind::rtp_session,
          "listener analysis kind");
    check(report->input.kind ==
              semi_stream_probe::application::InputKind::udp,
          "listener input kind");
    check(report->input.datagrams_received == 0,
          "empty listener datagram count");
    check(!report->input.duration_us,
          "duration starts with first received datagram");
    check(report->rtp && report->rtp->payload_type == 96,
          "listener RTP configuration");
}

} // namespace

int main() {
    test_endpoint_parsing();
    test_empty_local_listener();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "udp listener tests passed\n";
    return EXIT_SUCCESS;
}
