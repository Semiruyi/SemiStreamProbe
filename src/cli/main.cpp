#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/core/parse_error.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "SemiStreamProbe " << "0.1.0" << "\n"
        << "Usage:\n"
        << "  semistreamprobe inspect <file.h264> [--nal-list]\n"
        << "\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 1 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        print_usage();
        return 0;
    }

    if (std::string_view(argv[1]) != "inspect" || argc < 3) {
        print_usage();
        return 2;
    }

    semi_stream_probe::application::InspectOptions options;
    for (int index = 3; index < argc; ++index) {
        if (std::string_view(argv[index]) == "--nal-list") {
            options.nal_list = true;
            continue;
        }
        std::cerr << "Unknown option: " << argv[index] << '\n';
        return 2;
    }

    const auto result = semi_stream_probe::application::inspect_file(argv[2], options);
    if (!result) {
        const auto& error = result.error();
        std::cerr << "Error [" << semi_stream_probe::to_string(error.code) << "] at byte "
                  << error.byte_offset << ": " << error.message << '\n';
        return 1;
    }

    std::cout << semi_stream_probe::application::render_text(*result, options);
    return 0;
}

