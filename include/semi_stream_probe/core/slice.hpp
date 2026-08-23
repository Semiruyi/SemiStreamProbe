#pragma once

#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/parameter_sets.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <array>
#include <vector>

namespace semi_stream_probe {

enum class SliceType : std::uint8_t {
    p,
    b,
    i,
    sp,
    si,
};

struct ReferencePictureListModification {
    std::uint32_t modification_of_pic_nums_idc{0};
    std::optional<std::uint32_t> abs_diff_pic_num_minus1;
    std::optional<std::uint32_t> long_term_pic_num;
};

struct PredictionWeight {
    bool luma_weight_flag{false};
    std::optional<std::int32_t> luma_weight;
    std::optional<std::int32_t> luma_offset;
    bool chroma_weight_flag{false};
    std::array<std::optional<std::int32_t>, 2> chroma_weight;
    std::array<std::optional<std::int32_t>, 2> chroma_offset;
};

struct MemoryManagementControlOperation {
    std::uint32_t operation{0};
    std::optional<std::uint32_t> difference_of_pic_nums_minus1;
    std::optional<std::uint32_t> long_term_pic_num;
    std::optional<std::uint32_t> long_term_frame_idx;
    std::optional<std::uint32_t> max_long_term_frame_idx_plus1;
};

struct SliceHeader {
    std::size_t header_bit_size{0};
    std::uint8_t nal_ref_idc{0};
    std::uint32_t first_mb_in_slice{0};
    std::uint32_t slice_type_code{0};
    SliceType slice_type{SliceType::p};
    bool all_slices_same_type{false};
    std::uint32_t pic_parameter_set_id{0};
    std::uint32_t seq_parameter_set_id{0};
    std::optional<std::uint8_t> colour_plane_id;
    std::uint32_t frame_num{0};
    bool field_pic_flag{false};
    bool bottom_field_flag{false};
    bool idr{false};
    std::optional<std::uint32_t> idr_pic_id;
    std::optional<std::uint32_t> pic_order_cnt_lsb;
    std::optional<std::int32_t> delta_pic_order_bottom;
    std::optional<std::int32_t> delta_pic_order_cnt0;
    std::optional<std::int32_t> delta_pic_order_cnt1;
    std::optional<std::uint32_t> redundant_pic_cnt;

    std::optional<bool> direct_spatial_mv_pred_flag;
    std::optional<bool> num_ref_idx_active_override_flag;
    std::optional<std::uint32_t> num_ref_idx_l0_active_minus1;
    std::optional<std::uint32_t> num_ref_idx_l1_active_minus1;
    bool ref_pic_list_modification_flag_l0{false};
    bool ref_pic_list_modification_flag_l1{false};
    std::vector<ReferencePictureListModification>
        ref_pic_list_modifications_l0;
    std::vector<ReferencePictureListModification>
        ref_pic_list_modifications_l1;

    bool prediction_weight_table_present{false};
    std::optional<std::uint32_t> luma_log2_weight_denom;
    std::optional<std::uint32_t> chroma_log2_weight_denom;
    std::vector<PredictionWeight> prediction_weights_l0;
    std::vector<PredictionWeight> prediction_weights_l1;

    std::optional<bool> no_output_of_prior_pics_flag;
    std::optional<bool> long_term_reference_flag;
    std::optional<bool> adaptive_ref_pic_marking_mode_flag;
    std::vector<MemoryManagementControlOperation> memory_management_operations;

    std::optional<std::uint32_t> cabac_init_idc;
    std::int32_t slice_qp_delta{0};
    std::optional<bool> sp_for_switch_flag;
    std::optional<std::int32_t> slice_qs_delta;
    std::optional<std::uint32_t> disable_deblocking_filter_idc;
    std::optional<std::int32_t> slice_alpha_c0_offset_div2;
    std::optional<std::int32_t> slice_beta_offset_div2;
    std::optional<std::uint32_t> slice_group_change_cycle;
};

// Parses the complete base H.264 slice_header() for NAL types 1 and 5. Slice
// data, data partitioning, and scalable/multiview extension headers are outside
// this parser's scope.
[[nodiscard]] std::expected<SliceHeader, ParseError>
parse_slice_header(ByteView rbsp,
                   const NalHeader& nal_header,
                   const ParameterSetRegistry& parameter_sets);

[[nodiscard]] const char* slice_type_name(SliceType type) noexcept;

} // namespace semi_stream_probe
