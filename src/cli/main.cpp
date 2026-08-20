#include <iostream>
#include <string_view>

namespace {

void print_usage() {
    std::cout
        << "SemiStreamProbe " << "0.1.0" << "\n"
        << "Usage:\n"
        << "  semistreamprobe inspect <file.h264> [--nal-list]\n"
        << "\n"
        << "The parser is currently a learning scaffold; behavior will be\n"
        << "implemented milestone by milestone.\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 1 || std::string_view(argv[1]) == "--help" ||
        std::string_view(argv[1]) == "-h") {
        print_usage();
        return 0;
    }

    std::cerr << "SemiStreamProbe scaffold: command logic is not implemented yet.\n";
    return 2;
}

