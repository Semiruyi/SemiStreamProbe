#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/application/listen.hpp"
#include "semi_stream_probe/application/report.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/infrastructure/udp_receiver.hpp"

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

enum class OutputFormat {
    text,
    json,
};

volatile std::sig_atomic_t stop_requested = 0;

extern "C" void request_stop(int) { stop_requested = 1; }

void print_usage() {
    std::cout
        << "SemiStreamProbe 0.1.0\n"
        << "Usage:\n"
        << "  semistreamprobe inspect <file.h264> [--nal-list] "
           "[--output text|json]\n"
        << "  semistreamprobe listen --udp <address:port> "
           "--payload-type <0..127>\n"
        << "      [--clock-rate <hz>] [--duration <seconds>] "
           "[--output text|json]\n"
        << "  semistreamprobe --version\n\n";
}

[[nodiscard]] std::expected<OutputFormat, std::string>
parse_output_format(std::string_view value) {
    if (value == "text") {
        return OutputFormat::text;
    }
    if (value == "json") {
        return OutputFormat::json;
    }
    return std::unexpected("Invalid --output value: " + std::string(value));
}

template <typename Integer>
[[nodiscard]] std::expected<Integer, std::string>
parse_integer(std::string_view value,
              std::string_view option,
              Integer minimum,
              Integer maximum) {
    Integer result{};
    const auto parsed = std::from_chars(value.data(),
                                        value.data() + value.size(), result);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != value.data() + value.size() || result < minimum ||
        result > maximum) {
        return std::unexpected(std::string(option) + " must be in the range " +
                               std::to_string(minimum) + ".." +
                               std::to_string(maximum));
    }
    return result;
}

void render_report(const semi_stream_probe::application::AnalysisReport& report,
                   OutputFormat format) {
    if (format == OutputFormat::json) {
        std::cout << semi_stream_probe::application::render_json(report);
    } else {
        std::cout << semi_stream_probe::application::render_text(report);
    }
}

int run_inspect(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage();
        return 2;
    }

    semi_stream_probe::application::InspectOptions options;
    auto output_format = OutputFormat::text;
    for (int index = 3; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--nal-list") {
            options.nal_list = true;
            continue;
        }
        if (argument == "--output") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --output\n";
                return 2;
            }
            const auto parsed = parse_output_format(argv[++index]);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            output_format = *parsed;
            continue;
        }
        std::cerr << "Unknown option: " << argument << '\n';
        return 2;
    }

    const auto result =
        semi_stream_probe::application::inspect_file(argv[2], options);
    if (!result) {
        const auto& error = result.error();
        std::cerr << "Error [" << semi_stream_probe::to_string(error.code)
                  << "] at byte " << error.byte_offset << ": "
                  << error.message << '\n';
        return 1;
    }

    render_report(semi_stream_probe::application::make_annex_b_report(
                      *result, argv[2], options),
                  output_format);
    return 0;
}

int run_listen(int argc, char* argv[]) {
    std::optional<semi_stream_probe::infrastructure::UdpEndpoint> endpoint;
    std::optional<std::uint8_t> payload_type;
    std::uint32_t clock_rate_hz = 90'000;
    std::optional<std::chrono::milliseconds> duration;
    auto output_format = OutputFormat::text;

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--udp") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --udp\n";
                return 2;
            }
            auto parsed = semi_stream_probe::infrastructure::
                parse_udp_endpoint(argv[++index]);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            endpoint = std::move(*parsed);
            continue;
        }
        if (argument == "--payload-type") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --payload-type\n";
                return 2;
            }
            const auto parsed = parse_integer<std::uint16_t>(
                argv[++index], "--payload-type", 0, 127);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            payload_type = static_cast<std::uint8_t>(*parsed);
            continue;
        }
        if (argument == "--clock-rate") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --clock-rate\n";
                return 2;
            }
            const auto parsed = parse_integer<std::uint32_t>(
                argv[++index], "--clock-rate", 1,
                std::numeric_limits<std::uint32_t>::max());
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            clock_rate_hz = *parsed;
            continue;
        }
        if (argument == "--duration") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --duration\n";
                return 2;
            }
            constexpr std::uint64_t maximum_seconds =
                static_cast<std::uint64_t>(
                    std::chrono::milliseconds::max().count()) /
                1000U;
            const auto parsed = parse_integer<std::uint64_t>(
                argv[++index], "--duration", 1, maximum_seconds);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            duration = std::chrono::seconds(*parsed);
            continue;
        }
        if (argument == "--output") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --output\n";
                return 2;
            }
            const auto parsed = parse_output_format(argv[++index]);
            if (!parsed) {
                std::cerr << parsed.error() << '\n';
                return 2;
            }
            output_format = *parsed;
            continue;
        }
        std::cerr << "Unknown option: " << argument << '\n';
        return 2;
    }

    if (!endpoint) {
        std::cerr << "Missing required option --udp\n";
        return 2;
    }
    if (!payload_type) {
        std::cerr << "Missing required option --payload-type\n";
        return 2;
    }

    stop_requested = 0;
    std::signal(SIGINT, request_stop);
    const auto result = semi_stream_probe::application::listen_udp_h264(
        semi_stream_probe::application::ListenOptions{
            .endpoint = *endpoint,
            .payload_type = *payload_type,
            .clock_rate_hz = clock_rate_hz,
            .duration = duration,
        },
        [] { return stop_requested != 0; });
    if (!result) {
        std::cerr << "Error: " << result.error() << '\n';
        return 1;
    }
    render_report(*result, output_format);
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 1 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        print_usage();
        return 0;
    }

    if (std::string_view(argv[1]) == "--version") {
        std::cout << "SemiStreamProbe 0.1.0\n";
        return 0;
    }
    if (std::string_view(argv[1]) == "inspect") {
        return run_inspect(argc, argv);
    }
    if (std::string_view(argv[1]) == "listen") {
        return run_listen(argc, argv);
    }

    print_usage();
    return 2;
}
