#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/core/types.hpp"

#include "h264_test_data.hpp"

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
    semi_stream_probe::test::BaselineSpsOptions sps_options;
    sps_options.crop_bottom = 4;
    const auto sps_rbsp =
        semi_stream_probe::test::make_baseline_sps_rbsp(sps_options);
    const auto sps_ebsp = semi_stream_probe::test::rbsp_to_ebsp(sps_rbsp);

    semi_stream_probe::ByteBuffer bytes{
        0x00, 0x00, 0x00, 0x01, 0x67,
    };
    bytes.insert(bytes.end(), sps_ebsp.begin(), sps_ebsp.end());
    const semi_stream_probe::ByteBuffer remaining_nal_units{
        0x00, 0x00, 0x01, 0x68, 0xEE,
        0x00, 0x00, 0x01, 0x65,
    };
    bytes.insert(bytes.end(), remaining_nal_units.begin(),
                 remaining_nal_units.end());

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
        check(result->sequence_parameter_sets.size() == 1, "SPS count");
        if (!result->sequence_parameter_sets.empty()) {
            check(result->sequence_parameter_sets.front().width == 1920 &&
                      result->sequence_parameter_sets.front().height == 1080,
                  "SPS display dimensions");
        }
        const auto text = semi_stream_probe::application::render_text(*result, options);
        check(text.find("NAL units: 3") != std::string::npos, "summary count");
        check(text.find("Resolution: 1920x1080") != std::string::npos,
              "summary resolution");
        check(text.find("Profile: Baseline") != std::string::npos,
              "summary profile");
        check(text.find("Level: 4.0") != std::string::npos, "summary level");
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
