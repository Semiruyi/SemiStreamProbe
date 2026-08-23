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
    const auto pps_rbsp = semi_stream_probe::test::make_baseline_pps_rbsp();
    const auto pps_ebsp = semi_stream_probe::test::rbsp_to_ebsp(pps_rbsp);
    semi_stream_probe::test::BitWriter slice_writer;
    slice_writer.write_ue(0);      // first_mb_in_slice
    slice_writer.write_ue(7);      // I slice, all slices same type
    slice_writer.write_ue(0);      // pic_parameter_set_id
    slice_writer.write_bits(0, 4); // frame_num
    slice_writer.write_ue(0);      // idr_pic_id
    slice_writer.write_bits(0, 4); // pic_order_cnt_lsb
    slice_writer.write_bit(false); // no_output_of_prior_pics_flag
    slice_writer.write_bit(false); // long_term_reference_flag
    slice_writer.write_se(0);      // slice_qp_delta
    slice_writer.write_ue(1);      // disable_deblocking_filter_idc
    const auto slice_ebsp = semi_stream_probe::test::rbsp_to_ebsp(
        slice_writer.finish_rbsp());

    // Put PPS before SPS to verify inspect resolves parameter sets in two passes.
    semi_stream_probe::ByteBuffer bytes{
        0x00, 0x00, 0x00, 0x01, 0x68,
    };
    bytes.insert(bytes.end(), pps_ebsp.begin(), pps_ebsp.end());
    const semi_stream_probe::ByteBuffer sps_start{
        0x00, 0x00, 0x01, 0x67,
    };
    bytes.insert(bytes.end(), sps_start.begin(), sps_start.end());
    bytes.insert(bytes.end(), sps_ebsp.begin(), sps_ebsp.end());
    const semi_stream_probe::ByteBuffer idr{
        0x00, 0x00, 0x01, 0x65,
    };
    bytes.insert(bytes.end(), idr.begin(), idr.end());
    bytes.insert(bytes.end(), slice_ebsp.begin(), slice_ebsp.end());

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
        check(result->picture_parameter_sets.size() == 1, "PPS count");
        check(result->slices.size() == 1, "Slice count");
        check(result->access_units.size() == 1, "Access Unit count");
        check(result->gop_statistics.kinds.i == 1 &&
                  result->gop_statistics.idr_access_unit_indices ==
                      std::vector<std::size_t>({0}),
              "IDR and I access unit statistics");
        if (!result->access_units.empty()) {
            check(result->access_units.front().nal_indices ==
                      std::vector<std::size_t>({0, 1, 2}),
                  "SPS, PPS, and IDR are grouped into one Access Unit");
            check(result->access_units.front().vcl_nal_indices ==
                      std::vector<std::size_t>({2}),
                  "IDR is the Access Unit VCL NAL");
        }
        if (!result->slices.empty()) {
            check(result->slices.front().header.slice_type ==
                      semi_stream_probe::SliceType::i,
                  "Slice type");
            check(result->slices.front().header.frame_num == 0,
                  "Slice frame number");
        }
        if (!result->picture_parameter_sets.empty()) {
            check(result->picture_parameter_sets.front().pic_parameter_set_id == 0,
                  "PPS id");
            check(result->picture_parameter_sets.front().seq_parameter_set_id == 0,
                  "PPS referenced SPS id");
        }
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
        check(text.find("PPS: 1") != std::string::npos, "PPS summary count");
        check(text.find("Slices: 1") != std::string::npos,
              "Slice summary count");
        check(text.find("Access units: 1") != std::string::npos,
              "Access Unit summary count");
        check(text.find("AU slice types: I=1 P=0 B=0") != std::string::npos,
              "Access Unit slice type summary");
        check(text.find("IDR access units: 1") != std::string::npos,
              "IDR summary count");
        check(text.find("IDR interval: n/a") != std::string::npos,
              "single IDR has no interval");
        check(text.find("PPS id: 0 (SPS 0)") != std::string::npos,
              "PPS summary id");
        check(text.find("SPS") != std::string::npos, "SPS list entry");
        check(text.find("PPS") != std::string::npos, "PPS list entry");
        check(text.find("IDR_SLICE") != std::string::npos, "IDR list entry");
        check(text.find("SLICE") != std::string::npos, "Slice list column");
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
