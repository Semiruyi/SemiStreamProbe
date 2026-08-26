#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/core/parse_error.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

enum class OutputFormat {
    text,
    json,
};

void print_usage() {
    std::cout
        << "SemiStreamProbe " << "0.1.0" << "\n"
        << "Usage:\n"
        << "  semistreamprobe inspect <file.h264> [--nal-list] "
           "[--output text|json]\n"
        << "  semistreamprobe --version\n"
        << "\n";
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

    if (std::string_view(argv[1]) != "inspect" || argc < 3) {
        print_usage();
        return 2;
    }

    semi_stream_probe::application::InspectOptions options;
    auto output_format = OutputFormat::text;
    for (int index = 3; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--nal-list") {
            options.nal_list = true;
            continue;
        }
        if (std::string_view(argv[index]) == "--output") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for --output\n";
                return 2;
            }
            const std::string_view value(argv[++index]);
            if (value == "text") {
                output_format = OutputFormat::text;
                continue;
            }
            if (value == "json") {
                output_format = OutputFormat::json;
                continue;
            }
            std::cerr << "Invalid --output value: " << value << '\n';
            return 2;
        }
        std::cerr << "Unknown option: " << argv[index] << '\n';
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

    const auto report = semi_stream_probe::application::make_annex_b_report(
        *result, argv[2], options);
    if (output_format == OutputFormat::json) {
        std::cout << semi_stream_probe::application::render_json(report);
    } else {
        std::cout << semi_stream_probe::application::render_text(report);
    }
    return 0;
}

