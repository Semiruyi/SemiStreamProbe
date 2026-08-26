#include "semi_stream_probe/application/inspect.hpp"
#include "semi_stream_probe/core/types.hpp"

#include "h264_test_data.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

void append_nal(semi_stream_probe::ByteBuffer& stream,
                semi_stream_probe::Byte header,
                semi_stream_probe::ByteView rbsp) {
    const semi_stream_probe::ByteBuffer start_code{0x00, 0x00, 0x01};
    stream.insert(stream.end(), start_code.begin(), start_code.end());
    stream.push_back(header);
    const auto ebsp = semi_stream_probe::test::rbsp_to_ebsp(rbsp);
    stream.insert(stream.end(), ebsp.begin(), ebsp.end());
}

semi_stream_probe::ByteBuffer make_p_slice_rbsp(
    std::uint32_t frame_num,
    std::size_t frame_num_bits,
    std::uint32_t poc_lsb,
    std::optional<std::int32_t> delta_pic_order_bottom) {
    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0); // first_mb_in_slice
    writer.write_ue(0); // P slice
    writer.write_ue(0); // pic_parameter_set_id
    writer.write_bits(frame_num, frame_num_bits);
    writer.write_bits(poc_lsb, 4);
    if (delta_pic_order_bottom) {
        writer.write_se(*delta_pic_order_bottom);
    }
    writer.write_bit(false); // num_ref_idx_active_override_flag
    writer.write_bit(false); // ref_pic_list_modification_flag_l0
    writer.write_bit(false); // adaptive_ref_pic_marking_mode_flag
    writer.write_se(0);      // slice_qp_delta
    writer.write_ue(1);      // disable_deblocking_filter_idc
    return writer.finish_rbsp();
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

    // Put PPS before SPS to verify syntax extraction permits out-of-order
    // parameter-set delivery while Slice activation still follows NAL order.
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
        const auto report =
            semi_stream_probe::application::make_annex_b_report(
                *result, sample_path, options);
        const auto text = semi_stream_probe::application::render_text(report);
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

        const auto json = semi_stream_probe::application::render_json(report);
        check(json.find("\"schema_version\": \"1.0\"") !=
                  std::string::npos,
              "JSON schema version");
        check(json.find("\"kind\": \"annex_b\"") != std::string::npos,
              "JSON analysis kind");
        check(json.find("\"nal_units\": 3") != std::string::npos,
              "JSON NAL count");
        check(json.find("\"picture_parameter_sets\": [") !=
                  std::string::npos,
              "JSON parameter-set details");
    }

    const auto missing = semi_stream_probe::application::inspect_file(
        sample_path, semi_stream_probe::application::InspectOptions{});
    check(!missing, "missing file should fail");
    if (!missing) {
        check(missing.error().code == semi_stream_probe::ParseErrorCode::io_error,
              "missing file error code");
    }

    const auto timeline_path =
        std::filesystem::current_path() /
        "semi_stream_probe_parameter_timeline.h264";
    semi_stream_probe::test::BaselineSpsOptions first_sps_options;
    first_sps_options.log2_max_frame_num_minus4 = 0;
    semi_stream_probe::test::BaselineSpsOptions second_sps_options;
    second_sps_options.log2_max_frame_num_minus4 = 1;
    semi_stream_probe::test::BaselinePpsOptions first_pps_options;
    first_pps_options.bottom_field_pic_order_in_frame_present_flag = false;
    semi_stream_probe::test::BaselinePpsOptions second_pps_options;
    second_pps_options.bottom_field_pic_order_in_frame_present_flag = true;

    semi_stream_probe::ByteBuffer timeline_bytes;
    const auto first_sps = semi_stream_probe::test::make_baseline_sps_rbsp(
        first_sps_options);
    const auto first_pps = semi_stream_probe::test::make_baseline_pps_rbsp(
        first_pps_options);
    const auto first_slice = make_p_slice_rbsp(3, 4, 0, std::nullopt);
    const auto second_sps = semi_stream_probe::test::make_baseline_sps_rbsp(
        second_sps_options);
    const auto second_pps = semi_stream_probe::test::make_baseline_pps_rbsp(
        second_pps_options);
    const auto second_slice = make_p_slice_rbsp(17, 5, 2, -2);
    append_nal(timeline_bytes, 0x67, first_sps);
    append_nal(timeline_bytes, 0x68, first_pps);
    append_nal(timeline_bytes, 0x41, first_slice);
    append_nal(timeline_bytes, 0x67, second_sps);
    append_nal(timeline_bytes, 0x68, second_pps);
    append_nal(timeline_bytes, 0x41, second_slice);

    {
        std::ofstream timeline_file(timeline_path, std::ios::binary);
        timeline_file.write(
            reinterpret_cast<const char*>(timeline_bytes.data()),
            static_cast<std::streamsize>(timeline_bytes.size()));
        check(static_cast<bool>(timeline_file),
              "parameter timeline sample should be written");
    }

    const auto timeline_result =
        semi_stream_probe::application::inspect_file(
            timeline_path,
            semi_stream_probe::application::InspectOptions{});
    std::filesystem::remove(timeline_path, remove_error);
    check(timeline_result.has_value(),
          "parameter timeline sample should be inspected");
    if (timeline_result) {
        check(timeline_result->sequence_parameter_sets.size() == 2 &&
                  timeline_result->picture_parameter_sets.size() == 2,
              "both same-id parameter set versions are retained");
        check(timeline_result->slices.size() == 2,
              "both timeline slices are parsed");
        if (timeline_result->slices.size() == 2) {
            check(timeline_result->slices[0].header.frame_num == 3 &&
                      !timeline_result->slices[0]
                           .header.delta_pic_order_bottom,
                  "first Slice uses the first SPS and PPS versions");
            check(timeline_result->slices[1].header.frame_num == 17 &&
                      timeline_result->slices[1]
                              .header.delta_pic_order_bottom == -2,
                  "second Slice uses the replacement SPS and PPS versions");
        }
    }

    const auto premature_path =
        std::filesystem::current_path() /
        "semi_stream_probe_future_sps.h264";
    semi_stream_probe::ByteBuffer premature_bytes;
    append_nal(premature_bytes, 0x68, first_pps);
    append_nal(premature_bytes, 0x41, first_slice);
    append_nal(premature_bytes, 0x67, first_sps);
    {
        std::ofstream premature_file(premature_path, std::ios::binary);
        premature_file.write(
            reinterpret_cast<const char*>(premature_bytes.data()),
            static_cast<std::streamsize>(premature_bytes.size()));
        check(static_cast<bool>(premature_file),
              "future SPS sample should be written");
    }
    const auto premature_result =
        semi_stream_probe::application::inspect_file(
            premature_path,
            semi_stream_probe::application::InspectOptions{});
    std::filesystem::remove(premature_path, remove_error);
    check(!premature_result &&
              premature_result.error().code ==
                  semi_stream_probe::ParseErrorCode::parameter_set_not_found,
          "a future SPS may parse a PPS but is not active for an earlier Slice");

    if (failures != 0) {
        std::cerr << failures << " application test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_application_tests: all tests passed\n";
    return 0;
}
