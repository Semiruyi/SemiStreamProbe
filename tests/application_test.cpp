#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    const auto sample_path =
        std::filesystem::current_path() / "semi_stream_probe_test_sample.h264";
    constexpr std::array<semi_stream_probe::Byte, 15> bytes{
        0x00, 0x00, 0x00, 0x01, 0x67, 0x64,
        0x00, 0x00, 0x01, 0x68, 0xEE,
        0x00, 0x00, 0x01, 0x65,
    };

    {
        std::ofstream sample(sample_path, std::ios::binary);
        sample.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        check(static_cast<bool>(sample), "sample file should be written");
    }

    const semi_stream_probe::application::InspectOptions options{.nal_list = true};
    const auto result =
        semi_stream_probe::application::inspect_file(sample_path, options);
    std::error_code remove_error;
    std::filesystem::remove(sample_path, remove_error);

    check(result.has_value(), "sample file should be inspected");
    if (result) {
        check(result->input_size == bytes.size(), "input byte count");
        check(result->nal_units.size() == 3, "NAL unit count");
        const auto text = semi_stream_probe::application::render_text(*result, options);
        check(text.find("NAL units: 3") != std::string::npos, "summary count");
        check(text.find("SPS") != std::string::npos, "SPS list entry");
        check(text.find("PPS") != std::string::npos, "PPS list entry");
        check(text.find("IDR_SLICE") != std::string::npos, "IDR list entry");
    }

    const auto missing = semi_stream_probe::application::inspect_file(
        sample_path, semi_stream_probe::application::InspectOptions{});
    check(!missing, "missing file should fail");
    if (!missing) {
        check(missing.error().code == semi_stream_probe::ParseErrorCode::io_error,
              "missing file error code");
    }

    if (failures != 0) {
        std::cerr << failures << " application test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_application_tests: all tests passed\n";
    return 0;
}
