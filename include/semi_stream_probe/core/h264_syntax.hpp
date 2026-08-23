#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace semi_stream_probe {

struct Sps {
    std::uint8_t profile_idc{0};
    // Compact constraint_set0..5 flags. Bit 5 is constraint_set0_flag.
    std::uint8_t constraint_set_flags{0};
    std::uint8_t level_idc{0};
    std::uint32_t seq_parameter_set_id{0};

    std::uint32_t chroma_format_idc{1};
    bool separate_colour_plane_flag{false};
    std::uint8_t bit_depth_luma{8};
    std::uint8_t bit_depth_chroma{8};

    std::uint32_t log2_max_frame_num_minus4{0};
    std::uint32_t pic_order_cnt_type{0};
    std::uint32_t log2_max_pic_order_cnt_lsb_minus4{0};
    bool delta_pic_order_always_zero_flag{false};
    std::uint32_t max_num_ref_frames{0};
    bool frame_mbs_only_flag{true};

    std::uint32_t frame_crop_left_offset{0};
    std::uint32_t frame_crop_right_offset{0};
    std::uint32_t frame_crop_top_offset{0};
    std::uint32_t frame_crop_bottom_offset{0};

    std::uint32_t coded_width{0};
    std::uint32_t coded_height{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    bool vui_parameters_present_flag{false};
};

struct Pps {
    std::uint32_t pic_parameter_set_id{0};
    std::uint32_t seq_parameter_set_id{0};
    bool entropy_coding_mode_flag{false};
    bool bottom_field_pic_order_in_frame_present_flag{false};

    std::uint32_t num_slice_groups_minus1{0};
    std::uint32_t slice_group_map_type{0};
    std::vector<std::uint32_t> run_length_minus1;
    std::vector<std::uint32_t> top_left;
    std::vector<std::uint32_t> bottom_right;
    bool slice_group_change_direction_flag{false};
    std::uint32_t slice_group_change_rate_minus1{0};
    std::uint32_t pic_size_in_map_units_minus1{0};
    std::vector<std::uint8_t> slice_group_id;

    std::uint32_t num_ref_idx_l0_default_active_minus1{0};
    std::uint32_t num_ref_idx_l1_default_active_minus1{0};
    bool weighted_pred_flag{false};
    std::uint8_t weighted_bipred_idc{0};
    std::int32_t pic_init_qp_minus26{0};
    std::int32_t pic_init_qs_minus26{0};
    std::int32_t chroma_qp_index_offset{0};
    bool deblocking_filter_control_present_flag{false};
    bool constrained_intra_pred_flag{false};
    bool redundant_pic_cnt_present_flag{false};

    bool has_extension{false};
    bool transform_8x8_mode_flag{false};
    bool pic_scaling_matrix_present_flag{false};
    std::vector<bool> pic_scaling_list_present_flags;
    std::int32_t second_chroma_qp_index_offset{0};
};

[[nodiscard]] std::expected<Sps, ParseError> parse_sps(ByteView rbsp);
// chroma_format_idc comes from the referenced SPS and determines the number of
// optional 8x8 scaling lists in a PPS extension.
[[nodiscard]] std::expected<Pps, ParseError>
parse_pps(ByteView rbsp, std::uint32_t chroma_format_idc = 1);

[[nodiscard]] const char* h264_profile_name(std::uint8_t profile_idc) noexcept;
[[nodiscard]] std::string h264_level_name(const Sps& sps);

} // namespace semi_stream_probe

