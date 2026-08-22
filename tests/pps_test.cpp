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

void write_common_suffix(semi_stream_probe::test::BitWriter& writer) {
    writer.write_ue(1);      // num_ref_idx_l0_default_active_minus1
    writer.write_ue(0);      // num_ref_idx_l1_default_active_minus1
    writer.write_bit(true);  // weighted_pred_flag
    writer.write_bits(2, 2); // weighted_bipred_idc
    writer.write_se(-2);     // pic_init_qp_minus26
    writer.write_se(1);      // pic_init_qs_minus26
    writer.write_se(-1);     // chroma_qp_index_offset
    writer.write_bit(true);  // deblocking_filter_control_present_flag
    writer.write_bit(false); // constrained_intra_pred_flag
    writer.write_bit(true);  // redundant_pic_cnt_present_flag
}

semi_stream_probe::test::BitWriter make_pps_prefix(
    std::uint32_t num_slice_groups_minus1) {
    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(4); // pic_parameter_set_id
    writer.write_ue(2); // seq_parameter_set_id
    writer.write_bit(true);  // entropy_coding_mode_flag
    writer.write_bit(false); // bottom_field_pic_order_in_frame_present_flag
    writer.write_ue(num_slice_groups_minus1);
    return writer;
}

void test_baseline_pps() {
    semi_stream_probe::test::BaselinePpsOptions options;
    options.pic_parameter_set_id = 7;
    options.seq_parameter_set_id = 3;
    options.entropy_coding_mode_flag = true;
    options.chroma_qp_index_offset = -2;

    const auto result = semi_stream_probe::parse_pps(
        semi_stream_probe::test::make_baseline_pps_rbsp(options));
    check(result.has_value(), "baseline PPS should parse");
    if (!result) {
        return;
    }
    check(result->pic_parameter_set_id == 7, "PPS id");
    check(result->seq_parameter_set_id == 3, "referenced SPS id");
    check(result->entropy_coding_mode_flag, "CABAC flag");
    check(result->num_slice_groups_minus1 == 0, "single slice group");
    check(result->chroma_qp_index_offset == -2, "chroma QP offset");
    check(result->second_chroma_qp_index_offset == -2,
          "missing extension inherits first chroma QP offset");
    check(!result->has_extension, "baseline PPS has no extension");
}

void test_slice_group_map_types() {
    {
        auto writer = make_pps_prefix(1);
        writer.write_ue(0); // interleaved map
        writer.write_ue(2);
        writer.write_ue(4);
        write_common_suffix(writer);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(result && result->run_length_minus1.size() == 2 &&
                  result->run_length_minus1[0] == 2 &&
                  result->run_length_minus1[1] == 4,
              "slice group map type 0");
    }
    {
        auto writer = make_pps_prefix(1);
        writer.write_ue(1); // dispersed map has no additional syntax
        write_common_suffix(writer);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(result && result->slice_group_map_type == 1,
              "slice group map type 1");
    }
    {
        auto writer = make_pps_prefix(2);
        writer.write_ue(2); // foreground rectangles
        writer.write_ue(0);
        writer.write_ue(10);
        writer.write_ue(11);
        writer.write_ue(20);
        write_common_suffix(writer);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(result && result->top_left.size() == 2 &&
                  result->bottom_right[1] == 20,
              "slice group map type 2");
    }
    for (std::uint32_t map_type = 3; map_type <= 5; ++map_type) {
        auto writer = make_pps_prefix(1);
        writer.write_ue(map_type);
        writer.write_bit(true);
        writer.write_ue(8);
        write_common_suffix(writer);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(result && result->slice_group_map_type == map_type &&
                  result->slice_group_change_direction_flag &&
                  result->slice_group_change_rate_minus1 == 8,
              "dynamic slice group map type");
    }
    {
        auto writer = make_pps_prefix(2); // three groups need two ID bits
        writer.write_ue(6);
        writer.write_ue(3); // four map units
        writer.write_bits(0, 2);
        writer.write_bits(1, 2);
        writer.write_bits(2, 2);
        writer.write_bits(0, 2);
        write_common_suffix(writer);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(result && result->slice_group_id.size() == 4 &&
                  result->slice_group_id[2] == 2,
              "explicit slice group map type 6");
    }
}

void test_pps_extension_and_scaling_lists() {
    auto writer = make_pps_prefix(0);
    write_common_suffix(writer);
    writer.write_bit(true); // transform_8x8_mode_flag
    writer.write_bit(true); // pic_scaling_matrix_present_flag
    for (std::size_t index = 0; index < 12; ++index) {
        const bool present = index == 0;
        writer.write_bit(present);
        if (present) {
            for (std::size_t item = 0; item < 16; ++item) {
                writer.write_se(0);
            }
        }
    }
    writer.write_se(-2); // second_chroma_qp_index_offset

    const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp(), 3);
    check(result.has_value(), "4:4:4 PPS extension should parse");
    if (!result) {
        return;
    }
    check(result->has_extension && result->transform_8x8_mode_flag,
          "PPS transform extension");
    check(result->pic_scaling_matrix_present_flag &&
              result->pic_scaling_list_present_flags.size() == 12,
          "4:4:4 PPS scaling list count");
    check(result->second_chroma_qp_index_offset == -2,
          "second chroma QP offset");
}

void test_invalid_and_truncated_pps() {
    {
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(256);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_pps,
              "out-of-range PPS id should fail");
    }
    {
        auto writer = make_pps_prefix(1);
        writer.write_ue(7);
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_pps,
              "invalid slice group map type should fail");
    }
    {
        auto writer = make_pps_prefix(2);
        writer.write_ue(6);
        writer.write_ue(0);
        writer.write_bits(3, 2); // group 3 does not exist
        const auto result = semi_stream_probe::parse_pps(writer.finish_rbsp());
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_pps,
              "invalid explicit slice group id should fail");
    }
    {
        constexpr std::array<semi_stream_probe::Byte, 1> truncated{0x80};
        const auto result = semi_stream_probe::parse_pps(truncated);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
              "truncated PPS should fail safely");
        if (!result) {
            check(result.error().message.find("seq_parameter_set_id") !=
                      std::string::npos,
                  "truncated PPS field context");
        }
    }
}

} // namespace

int main() {
    test_baseline_pps();
    test_slice_group_map_types();
    test_pps_extension_and_scaling_lists();
    test_invalid_and_truncated_pps();

    if (failures != 0) {
        std::cerr << failures << " PPS test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_pps_tests: all tests passed\n";
    return 0;
}
