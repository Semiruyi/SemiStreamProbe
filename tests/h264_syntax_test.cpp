#include "semi_stream_probe/core/h264_syntax.hpp"

#include "h264_test_data.hpp"

#include <array>
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

void test_baseline_sps_with_cropping() {
    semi_stream_probe::test::BaselineSpsOptions options;
    options.crop_bottom = 4;
    const auto rbsp = semi_stream_probe::test::make_baseline_sps_rbsp(options);
    const auto result = semi_stream_probe::parse_sps(rbsp);

    check(result.has_value(), "cropped Baseline SPS should parse");
    if (!result) {
        return;
    }
    check(result->profile_idc == 66, "Baseline profile_idc");
    check(result->level_idc == 40, "Baseline level_idc");
    check(result->seq_parameter_set_id == 0, "Baseline SPS id");
    check(result->chroma_format_idc == 1, "Baseline default chroma format");
    check(result->bit_depth_luma == 8, "Baseline default luma bit depth");
    check(result->coded_width == 1920 && result->coded_height == 1088,
          "Baseline coded dimensions");
    check(result->width == 1920 && result->height == 1080,
          "Baseline cropped dimensions");
    check(std::string_view(semi_stream_probe::h264_profile_name(
              result->profile_idc)) == "Baseline",
          "Baseline profile name");
    check(semi_stream_probe::h264_level_name(*result) == "4.0",
          "Baseline level name");
}

void test_high_profile_scaling_list_and_poc_type_one() {
    semi_stream_probe::test::BitWriter writer;
    writer.write_bits(100, 8); // High profile
    writer.write_bits(0, 8);   // constraint flags and reserved_zero_2bits
    writer.write_bits(31, 8);  // Level 3.1
    writer.write_ue(3);        // seq_parameter_set_id
    writer.write_ue(1);        // chroma_format_idc: 4:2:0
    writer.write_ue(2);        // bit_depth_luma_minus8: 10-bit
    writer.write_ue(2);        // bit_depth_chroma_minus8: 10-bit
    writer.write_bit(false);   // qpprime_y_zero_transform_bypass_flag
    writer.write_bit(true);    // seq_scaling_matrix_present_flag
    for (std::size_t index = 0; index < 8; ++index) {
        const bool present = index == 0;
        writer.write_bit(present);
        if (present) {
            for (std::size_t item = 0; item < 16; ++item) {
                writer.write_se(0);
            }
        }
    }
    writer.write_ue(0);       // log2_max_frame_num_minus4
    writer.write_ue(1);       // pic_order_cnt_type
    writer.write_bit(true);   // delta_pic_order_always_zero_flag
    writer.write_se(-1);      // offset_for_non_ref_pic
    writer.write_se(2);       // offset_for_top_to_bottom_field
    writer.write_ue(2);       // num_ref_frames_in_pic_order_cnt_cycle
    writer.write_se(-2);
    writer.write_se(3);
    writer.write_ue(4);       // max_num_ref_frames
    writer.write_bit(false);  // gaps_in_frame_num_value_allowed_flag
    writer.write_ue(79);      // 80 macroblocks = 1280 pixels
    writer.write_ue(44);      // 45 map units = 720 pixels
    writer.write_bit(true);   // frame_mbs_only_flag
    writer.write_bit(true);   // direct_8x8_inference_flag
    writer.write_bit(false);  // frame_cropping_flag
    writer.write_bit(true);   // vui_parameters_present_flag

    const auto result = semi_stream_probe::parse_sps(writer.finish_rbsp());
    check(result.has_value(), "High profile SPS should parse");
    if (!result) {
        return;
    }
    check(result->profile_idc == 100, "High profile_idc");
    check(result->seq_parameter_set_id == 3, "High profile SPS id");
    check(result->chroma_format_idc == 1, "High profile chroma format");
    check(result->bit_depth_luma == 10 && result->bit_depth_chroma == 10,
          "High profile bit depths");
    check(result->pic_order_cnt_type == 1, "High profile POC type");
    check(result->delta_pic_order_always_zero_flag,
          "High profile POC delta always zero flag");
    check(result->max_num_ref_frames == 4, "High profile reference frame count");
    check(result->width == 1280 && result->height == 720,
          "High profile dimensions");
    check(result->vui_parameters_present_flag, "High profile VUI presence");
    check(std::string_view(semi_stream_probe::h264_profile_name(
              result->profile_idc)) == "High",
          "High profile name");
    check(semi_stream_probe::h264_level_name(*result) == "3.1",
          "High profile level name");
}

void test_interlaced_height() {
    semi_stream_probe::test::BaselineSpsOptions options;
    options.width_in_mbs = 45;
    options.height_in_map_units = 18;
    options.frame_mbs_only_flag = false;

    const auto result = semi_stream_probe::parse_sps(
        semi_stream_probe::test::make_baseline_sps_rbsp(options));
    check(result && result->width == 720 && result->height == 576,
          "interlaced SPS dimensions");
    if (result) {
        check(!result->frame_mbs_only_flag, "interlaced frame flag");
    }
}

void test_invalid_and_truncated_sps() {
    constexpr std::array<semi_stream_probe::Byte, 3> truncated{66, 0, 30};
    const auto truncated_result = semi_stream_probe::parse_sps(truncated);
    check(!truncated_result, "truncated SPS should fail");
    if (!truncated_result) {
        check(truncated_result.error().code ==
                  semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
              "truncated SPS error code");
        check(truncated_result.error().message.find("seq_parameter_set_id") !=
                  std::string::npos,
              "truncated SPS field context");
    }

    semi_stream_probe::test::BaselineSpsOptions reserved_options;
    reserved_options.constraint_and_reserved = 0x01;
    const auto reserved_result = semi_stream_probe::parse_sps(
        semi_stream_probe::test::make_baseline_sps_rbsp(reserved_options));
    check(!reserved_result, "non-zero reserved SPS bits should fail");
    if (!reserved_result) {
        check(reserved_result.error().code ==
                  semi_stream_probe::ParseErrorCode::invalid_sps,
              "reserved SPS bits error code");
    }

    semi_stream_probe::test::BaselineSpsOptions crop_options;
    crop_options.width_in_mbs = 1;
    crop_options.height_in_map_units = 1;
    crop_options.crop_right = 8;
    const auto crop_result = semi_stream_probe::parse_sps(
        semi_stream_probe::test::make_baseline_sps_rbsp(crop_options));
    check(!crop_result, "crop removing the full picture should fail");
    if (!crop_result) {
        check(crop_result.error().code ==
                  semi_stream_probe::ParseErrorCode::invalid_sps,
              "invalid crop error code");
    }
}

void test_level_1b_name() {
    semi_stream_probe::Sps sps{
        .profile_idc = 66,
        .constraint_set_flags = 0x04,
        .level_idc = 11,
    };
    check(semi_stream_probe::h264_level_name(sps) == "1b", "Level 1b name");
}

} // namespace

int main() {
    test_baseline_sps_with_cropping();
    test_high_profile_scaling_list_and_poc_type_one();
    test_interlaced_height();
    test_invalid_and_truncated_sps();
    test_level_1b_name();

    if (failures != 0) {
        std::cerr << failures << " H.264 syntax test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_h264_syntax_tests: all tests passed\n";
    return 0;
}
