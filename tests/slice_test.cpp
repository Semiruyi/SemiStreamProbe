#include "semi_stream_probe/core/parameter_sets.hpp"
#include "semi_stream_probe/core/slice.hpp"

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

semi_stream_probe::NalHeader nal_header(std::uint8_t type,
                                        std::uint8_t reference_idc = 3) {
    return semi_stream_probe::NalHeader{
        .nal_ref_idc = reference_idc,
        .nal_unit_type = type,
    };
}

void test_parameter_set_registry() {
    semi_stream_probe::ParameterSetRegistry registry;
    semi_stream_probe::Sps sps;
    sps.seq_parameter_set_id = 2;
    sps.width = 640;
    check(!registry.store(sps), "first SPS registration is not replacement");
    check(registry.find_sps(2) != nullptr && registry.find_sps(2)->width == 640,
          "registered SPS lookup");
    sps.width = 1280;
    check(registry.store(sps), "second SPS registration replaces same id");
    check(registry.find_sps(2)->width == 1280, "replacement SPS value");
    check(registry.find_sps(32) == nullptr, "out-of-range SPS lookup");

    semi_stream_probe::Pps pps;
    pps.pic_parameter_set_id = 9;
    pps.seq_parameter_set_id = 2;
    check(!registry.store(pps), "first PPS registration is not replacement");
    check(registry.find_pps(9) != nullptr &&
              registry.find_pps(9)->seq_parameter_set_id == 2,
          "registered PPS lookup");
    check(registry.find_pps(256) == nullptr, "out-of-range PPS lookup");
}

void test_idr_frame_slice() {
    semi_stream_probe::ParameterSetRegistry registry;
    semi_stream_probe::Sps sps;
    sps.seq_parameter_set_id = 3;
    sps.log2_max_frame_num_minus4 = 0;
    sps.pic_order_cnt_type = 0;
    sps.log2_max_pic_order_cnt_lsb_minus4 = 0;
    sps.frame_mbs_only_flag = true;
    registry.store(sps);
    semi_stream_probe::Pps pps;
    pps.pic_parameter_set_id = 4;
    pps.seq_parameter_set_id = 3;
    registry.store(pps);

    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0);      // first_mb_in_slice
    writer.write_ue(7);      // I slice, all slices same type
    writer.write_ue(4);      // pic_parameter_set_id
    writer.write_bits(0, 4); // frame_num
    writer.write_ue(2);      // idr_pic_id
    writer.write_bits(6, 4); // pic_order_cnt_lsb
    writer.write_bit(false); // no_output_of_prior_pics_flag
    writer.write_bit(false); // long_term_reference_flag
    writer.write_se(0);      // slice_qp_delta

    const auto result = semi_stream_probe::parse_slice_header(
        writer.finish_rbsp(), nal_header(5), registry);
    check(result.has_value(), "IDR frame slice should parse");
    if (!result) {
        return;
    }
    check(result->first_mb_in_slice == 0, "IDR first macroblock");
    check(result->slice_type == semi_stream_probe::SliceType::i &&
              result->all_slices_same_type,
          "IDR I slice classification");
    check(result->pic_parameter_set_id == 4 &&
              result->seq_parameter_set_id == 3,
          "IDR parameter set references");
    check(result->frame_num == 0 && result->idr, "IDR frame number and flag");
    check(result->idr_pic_id == 2, "IDR picture id");
    check(result->pic_order_cnt_lsb == 6, "IDR picture order count");
    check(std::string_view(semi_stream_probe::slice_type_name(
              result->slice_type)) == "I",
          "I slice type name");
    check(std::string_view(semi_stream_probe::slice_type_name(
              semi_stream_probe::SliceType::sp)) == "SP" &&
              std::string_view(semi_stream_probe::slice_type_name(
                  semi_stream_probe::SliceType::si)) == "SI",
          "SP and SI slice type names");

    semi_stream_probe::test::BitWriter si_writer;
    si_writer.write_ue(0);
    si_writer.write_ue(4);      // SI is also permitted in an IDR NAL
    si_writer.write_ue(4);
    si_writer.write_bits(0, 4);
    si_writer.write_ue(3);
    si_writer.write_bits(0, 4);
    si_writer.write_bit(false); // no_output_of_prior_pics_flag
    si_writer.write_bit(false); // long_term_reference_flag
    si_writer.write_se(0);      // slice_qp_delta
    si_writer.write_se(0);      // slice_qs_delta
    const auto si_result = semi_stream_probe::parse_slice_header(
        si_writer.finish_rbsp(), nal_header(5), registry);
    check(si_result && si_result->slice_type == semi_stream_probe::SliceType::si,
          "IDR SI slice should parse");
}

void test_field_slice_with_poc_type_one() {
    semi_stream_probe::ParameterSetRegistry registry;
    semi_stream_probe::Sps sps;
    sps.seq_parameter_set_id = 1;
    sps.log2_max_frame_num_minus4 = 0;
    sps.pic_order_cnt_type = 1;
    sps.delta_pic_order_always_zero_flag = false;
    sps.max_num_ref_frames = 1;
    sps.frame_mbs_only_flag = false;
    registry.store(sps);
    semi_stream_probe::Pps pps;
    pps.pic_parameter_set_id = 5;
    pps.seq_parameter_set_id = 1;
    pps.bottom_field_pic_order_in_frame_present_flag = true;
    pps.redundant_pic_cnt_present_flag = true;
    registry.store(pps);

    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(12);     // a later slice in the picture
    writer.write_ue(0);      // P slice
    writer.write_ue(5);      // pic_parameter_set_id
    writer.write_bits(3, 4); // frame_num
    writer.write_bit(true);  // field_pic_flag
    writer.write_bit(true);  // bottom_field_flag
    writer.write_se(-2);     // delta_pic_order_cnt[0]
    writer.write_ue(3);      // redundant_pic_cnt
    writer.write_bit(false); // num_ref_idx_active_override_flag
    writer.write_bit(false); // ref_pic_list_modification_flag_l0
    writer.write_bit(false); // adaptive_ref_pic_marking_mode_flag
    writer.write_se(0);      // slice_qp_delta

    const auto result = semi_stream_probe::parse_slice_header(
        writer.finish_rbsp(), nal_header(1, 2), registry);
    check(result.has_value(), "field P slice should parse");
    if (!result) {
        return;
    }
    check(result->first_mb_in_slice == 12,
          "multi-slice first macroblock is preserved");
    check(result->slice_type == semi_stream_probe::SliceType::p,
          "P slice classification");
    check(result->field_pic_flag && result->bottom_field_flag,
          "bottom field flags");
    check(result->delta_pic_order_cnt0 == -2 &&
              !result->delta_pic_order_cnt1.has_value(),
          "field picture POC delta");
    check(result->redundant_pic_cnt == 3, "redundant picture count");
}

void test_separate_colour_plane_and_bottom_delta() {
    semi_stream_probe::ParameterSetRegistry registry;
    semi_stream_probe::Sps sps;
    sps.seq_parameter_set_id = 0;
    sps.separate_colour_plane_flag = true;
    sps.pic_order_cnt_type = 0;
    sps.max_num_ref_frames = 1;
    sps.frame_mbs_only_flag = true;
    registry.store(sps);
    semi_stream_probe::Pps pps;
    pps.bottom_field_pic_order_in_frame_present_flag = true;
    registry.store(pps);

    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0);
    writer.write_ue(1);      // B slice
    writer.write_ue(0);
    writer.write_bits(2, 2); // colour_plane_id
    writer.write_bits(1, 4); // frame_num
    writer.write_bits(4, 4); // pic_order_cnt_lsb
    writer.write_se(-1);     // delta_pic_order_bottom
    writer.write_bit(true);  // direct_spatial_mv_pred_flag
    writer.write_bit(false); // num_ref_idx_active_override_flag
    writer.write_bit(false); // ref_pic_list_modification_flag_l0
    writer.write_bit(false); // ref_pic_list_modification_flag_l1
    writer.write_bit(false); // adaptive_ref_pic_marking_mode_flag
    writer.write_se(0);      // slice_qp_delta

    const auto result = semi_stream_probe::parse_slice_header(
        writer.finish_rbsp(), nal_header(1), registry);
    check(result && result->colour_plane_id == 2 &&
              result->delta_pic_order_bottom == -1 &&
              result->slice_type == semi_stream_probe::SliceType::b,
          "separate colour plane slice fields");
}

void test_p_slice_reference_lists_weights_and_mmco() {
    semi_stream_probe::ParameterSetRegistry registry;
    semi_stream_probe::Sps sps;
    sps.seq_parameter_set_id = 2;
    sps.pic_order_cnt_type = 0;
    sps.max_num_ref_frames = 4;
    sps.frame_mbs_only_flag = true;
    registry.store(sps);
    semi_stream_probe::Pps pps;
    pps.pic_parameter_set_id = 6;
    pps.seq_parameter_set_id = 2;
    pps.entropy_coding_mode_flag = true;
    pps.weighted_pred_flag = true;
    pps.deblocking_filter_control_present_flag = true;
    registry.store(pps);

    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0);
    writer.write_ue(5);      // P, all slices same type
    writer.write_ue(6);
    writer.write_bits(2, 4); // frame_num
    writer.write_bits(3, 4); // pic_order_cnt_lsb
    writer.write_bit(true);  // num_ref_idx_active_override_flag
    writer.write_ue(1);      // two active L0 references
    writer.write_bit(true);  // ref_pic_list_modification_flag_l0
    writer.write_ue(0);      // subtract short-term picture number
    writer.write_ue(1);      // abs_diff_pic_num_minus1
    writer.write_ue(2);      // long-term reference
    writer.write_ue(3);      // long_term_pic_num
    writer.write_ue(3);      // list modification end marker
    writer.write_ue(2);      // luma_log2_weight_denom
    writer.write_ue(1);      // chroma_log2_weight_denom
    writer.write_bit(true);  // L0[0] luma weight present
    writer.write_se(2);
    writer.write_se(-1);
    writer.write_bit(true);  // L0[0] chroma weights present
    writer.write_se(1);
    writer.write_se(0);
    writer.write_se(1);
    writer.write_se(0);
    writer.write_bit(false); // L0[1] default luma weight
    writer.write_bit(false); // L0[1] default chroma weight
    writer.write_bit(true);  // adaptive_ref_pic_marking_mode_flag
    writer.write_ue(1);      // MMCO 1
    writer.write_ue(0);      // difference_of_pic_nums_minus1
    writer.write_ue(3);      // MMCO 3
    writer.write_ue(1);      // difference_of_pic_nums_minus1
    writer.write_ue(2);      // long_term_frame_idx
    writer.write_ue(0);      // MMCO end marker
    writer.write_ue(2);      // cabac_init_idc
    writer.write_se(-3);     // slice_qp_delta
    writer.write_ue(0);      // deblocking enabled
    writer.write_se(2);      // slice_alpha_c0_offset_div2
    writer.write_se(-2);     // slice_beta_offset_div2

    const auto result = semi_stream_probe::parse_slice_header(
        writer.finish_rbsp(), nal_header(1, 3), registry);
    check(result.has_value(), "feature-rich P slice should parse");
    if (!result) {
        return;
    }
    check(result->num_ref_idx_active_override_flag == true &&
              result->num_ref_idx_l0_active_minus1 == 1,
          "active L0 reference override");
    check(result->ref_pic_list_modifications_l0.size() == 2 &&
              result->ref_pic_list_modifications_l0[0]
                      .abs_diff_pic_num_minus1 == 1 &&
              result->ref_pic_list_modifications_l0[1].long_term_pic_num == 3,
          "L0 reference list modifications");
    check(result->prediction_weight_table_present &&
              result->luma_log2_weight_denom == 2 &&
              result->chroma_log2_weight_denom == 1 &&
              result->prediction_weights_l0.size() == 2 &&
              result->prediction_weights_l0[0].luma_weight == 2 &&
              !result->prediction_weights_l0[1].luma_weight_flag,
          "P slice prediction weight table");
    check(result->adaptive_ref_pic_marking_mode_flag == true &&
              result->memory_management_operations.size() == 2 &&
              result->memory_management_operations[0].operation == 1 &&
              result->memory_management_operations[1].operation == 3 &&
              result->memory_management_operations[1].long_term_frame_idx == 2,
          "adaptive reference picture marking and MMCO");
    check(result->cabac_init_idc == 2 && result->slice_qp_delta == -3,
          "CABAC initialization and slice QP");
    check(result->disable_deblocking_filter_idc == 0 &&
              result->slice_alpha_c0_offset_div2 == 2 &&
              result->slice_beta_offset_div2 == -2,
          "deblocking filter controls");
    check(result->header_bit_size > 0, "complete slice header bit size");
}

void test_b_slice_list1_and_weight_table() {
    semi_stream_probe::ParameterSetRegistry registry;
    semi_stream_probe::Sps sps;
    sps.seq_parameter_set_id = 4;
    sps.chroma_format_idc = 0;
    sps.pic_order_cnt_type = 0;
    sps.max_num_ref_frames = 2;
    registry.store(sps);
    semi_stream_probe::Pps pps;
    pps.pic_parameter_set_id = 7;
    pps.seq_parameter_set_id = 4;
    pps.weighted_bipred_idc = 1;
    registry.store(pps);

    semi_stream_probe::test::BitWriter writer;
    writer.write_ue(0);
    writer.write_ue(1);      // B slice
    writer.write_ue(7);
    writer.write_bits(1, 4); // frame_num
    writer.write_bits(2, 4); // pic_order_cnt_lsb
    writer.write_bit(false); // direct_spatial_mv_pred_flag
    writer.write_bit(false); // num_ref_idx_active_override_flag
    writer.write_bit(false); // L0 modification flag
    writer.write_bit(true);  // L1 modification flag
    writer.write_ue(2);
    writer.write_ue(4);      // long_term_pic_num
    writer.write_ue(3);      // end marker
    writer.write_ue(0);      // luma_log2_weight_denom
    writer.write_bit(false); // L0[0] luma weight
    writer.write_bit(true);  // L1[0] luma weight
    writer.write_se(1);
    writer.write_se(0);
    writer.write_se(0);      // slice_qp_delta

    const auto result = semi_stream_probe::parse_slice_header(
        writer.finish_rbsp(), nal_header(1, 0), registry);
    check(result && result->direct_spatial_mv_pred_flag == false &&
              result->ref_pic_list_modifications_l1.size() == 1 &&
              result->ref_pic_list_modifications_l1[0].long_term_pic_num == 4 &&
              result->prediction_weights_l0.size() == 1 &&
              result->prediction_weights_l1.size() == 1 &&
              result->prediction_weights_l1[0].luma_weight == 1,
          "B slice L1 modification and explicit weight");
}

void test_sp_slice_and_dynamic_slice_groups() {
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        sps.seq_parameter_set_id = 5;
        sps.max_num_ref_frames = 1;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        pps.pic_parameter_set_id = 8;
        pps.seq_parameter_set_id = 5;
        registry.store(pps);

        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(3);      // SP slice
        writer.write_ue(8);
        writer.write_bits(1, 4);
        writer.write_bits(1, 4);
        writer.write_bit(false); // reference count override
        writer.write_bit(false); // L0 modification flag
        writer.write_se(0);      // slice_qp_delta
        writer.write_bit(true);  // sp_for_switch_flag
        writer.write_se(-2);     // slice_qs_delta

        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1, 0), registry);
        check(result && result->sp_for_switch_flag == true &&
                  result->slice_qs_delta == -2,
              "SP switching and QS fields");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        sps.seq_parameter_set_id = 6;
        sps.coded_width = 32;
        sps.coded_height = 32;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        pps.pic_parameter_set_id = 9;
        pps.seq_parameter_set_id = 6;
        pps.num_slice_groups_minus1 = 1;
        pps.slice_group_map_type = 3;
        pps.slice_group_change_rate_minus1 = 1; // rate = 2 map units
        registry.store(pps);

        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(2);      // I slice
        writer.write_ue(9);
        writer.write_bits(0, 4);
        writer.write_bits(0, 4);
        writer.write_se(0);      // slice_qp_delta
        writer.write_bits(2, 2); // ceil(log2(ceil(4/2) + 1)) = 2

        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1, 0), registry);
        check(result && result->slice_group_change_cycle == 2,
              "dynamic FMO slice group change cycle");
    }
}

void test_invalid_slice_header_tail() {
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        sps.max_num_ref_frames = 1;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        registry.store(pps);
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(0);      // P slice
        writer.write_ue(0);
        writer.write_bits(0, 4);
        writer.write_bits(0, 4);
        writer.write_bit(false); // reference count override
        writer.write_bit(true);  // L0 modification flag
        writer.write_ue(3);      // invalid as first modification
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1, 0), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "empty reference list modification should fail");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        sps.max_num_ref_frames = 1;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        registry.store(pps);
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(2);      // I slice
        writer.write_ue(0);
        writer.write_bits(0, 4);
        writer.write_bits(0, 4);
        writer.write_bit(true);  // adaptive marking
        writer.write_ue(7);      // invalid MMCO
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1, 3), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "out-of-range MMCO should fail");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        pps.deblocking_filter_control_present_flag = true;
        registry.store(pps);
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(2);      // I slice
        writer.write_ue(0);
        writer.write_bits(0, 4);
        writer.write_bits(0, 4);
        writer.write_se(0);      // slice_qp_delta
        writer.write_ue(0);      // deblocking enabled
        writer.write_se(7);      // invalid alpha offset
        writer.write_se(0);
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1, 0), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "out-of-range deblocking offset should fail");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        sps.max_num_ref_frames = 1;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        pps.entropy_coding_mode_flag = true;
        registry.store(pps);
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(0);      // P slice
        writer.write_ue(0);
        writer.write_bits(0, 4);
        writer.write_bits(0, 4);
        writer.write_bit(false); // reference count override
        writer.write_bit(false); // L0 modification flag
        writer.write_ue(3);      // invalid cabac_init_idc
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1, 0), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "out-of-range CABAC initialization should fail");
    }
}

void test_invalid_slice_and_parameter_references() {
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(2);
        writer.write_ue(13);
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::parameter_set_not_found,
              "missing PPS should be reported");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Pps pps;
        pps.pic_parameter_set_id = 4;
        pps.seq_parameter_set_id = 3;
        registry.store(pps);
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(2);
        writer.write_ue(4);
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::parameter_set_not_found,
              "missing referenced SPS should be reported");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(10);
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(1), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "invalid slice type should fail");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        constexpr std::array<semi_stream_probe::Byte, 1> truncated{0x80};
        const auto result = semi_stream_probe::parse_slice_header(
            truncated, nal_header(1), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::unexpected_end_of_data,
              "truncated slice header should fail safely");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        const auto result = semi_stream_probe::parse_slice_header(
            {}, nal_header(6), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "non-VCL NAL should be rejected by slice parser");
    }
    {
        semi_stream_probe::ParameterSetRegistry registry;
        semi_stream_probe::Sps sps;
        registry.store(sps);
        semi_stream_probe::Pps pps;
        registry.store(pps);
        semi_stream_probe::test::BitWriter writer;
        writer.write_ue(0);
        writer.write_ue(0);      // P slice is invalid inside an IDR NAL
        writer.write_ue(0);
        writer.write_bits(0, 4);
        const auto result = semi_stream_probe::parse_slice_header(
            writer.finish_rbsp(), nal_header(5), registry);
        check(!result && result.error().code ==
                             semi_stream_probe::ParseErrorCode::invalid_slice,
              "IDR NAL containing a non-I slice should fail");
    }
}

} // namespace

int main() {
    test_parameter_set_registry();
    test_idr_frame_slice();
    test_field_slice_with_poc_type_one();
    test_separate_colour_plane_and_bottom_delta();
    test_p_slice_reference_lists_weights_and_mmco();
    test_b_slice_list1_and_weight_table();
    test_sp_slice_and_dynamic_slice_groups();
    test_invalid_slice_header_tail();
    test_invalid_slice_and_parameter_references();

    if (failures != 0) {
        std::cerr << failures << " slice test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_slice_tests: all tests passed\n";
    return 0;
}
