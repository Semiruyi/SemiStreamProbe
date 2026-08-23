#include "semi_stream_probe/core/slice.hpp"

#include "semi_stream_probe/core/bit_reader.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace semi_stream_probe {

namespace {

[[nodiscard]] ParseError with_slice_context(ParseError error,
                                            std::string_view field) {
    error.message = "Slice " + std::string(field) + ": " + error.message;
    return error;
}

[[nodiscard]] ParseError make_slice_error(const BitReader& reader,
                                          std::string message) {
    return ParseError{
        .code = ParseErrorCode::invalid_slice,
        .byte_offset = reader.bit_position() / 8,
        .bit_offset = reader.bit_position(),
        .message = std::move(message),
    };
}

[[nodiscard]] ParseError make_missing_parameter_set_error(
    const BitReader& reader, std::string message) {
    return ParseError{
        .code = ParseErrorCode::parameter_set_not_found,
        .byte_offset = reader.bit_position() / 8,
        .bit_offset = reader.bit_position(),
        .message = std::move(message),
    };
}

constexpr std::uint32_t maximum_active_reference_index = 31;
constexpr std::size_t maximum_memory_management_operations = 64;

[[nodiscard]] std::expected<std::uint32_t, ParseError>
read_slice_ue(BitReader& reader, std::string_view field) {
    auto value = reader.read_ue();
    if (!value) {
        return std::unexpected(
            with_slice_context(std::move(value.error()), field));
    }
    return *value;
}

[[nodiscard]] std::expected<std::int32_t, ParseError>
read_slice_se(BitReader& reader, std::string_view field) {
    auto value = reader.read_se();
    if (!value) {
        return std::unexpected(
            with_slice_context(std::move(value.error()), field));
    }
    return *value;
}

[[nodiscard]] std::expected<bool, ParseError>
read_slice_flag(BitReader& reader, std::string_view field) {
    auto value = reader.read_bit();
    if (!value) {
        return std::unexpected(
            with_slice_context(std::move(value.error()), field));
    }
    return *value;
}

[[nodiscard]] bool is_intra_slice(SliceType type) noexcept {
    return type == SliceType::i || type == SliceType::si;
}

[[nodiscard]] bool uses_reference_lists(SliceType type) noexcept {
    return type == SliceType::p || type == SliceType::sp ||
           type == SliceType::b;
}

[[nodiscard]] std::expected<std::vector<ReferencePictureListModification>,
                            ParseError>
parse_reference_picture_list_modifications(BitReader& reader,
                                           std::uint32_t active_minus1,
                                           std::string_view list_name) {
    std::vector<ReferencePictureListModification> modifications;
    modifications.reserve(static_cast<std::size_t>(active_minus1) + 1U);
    const auto maximum_modifications = active_minus1 + 1U;

    while (true) {
        auto idc = read_slice_ue(reader, "modification_of_pic_nums_idc");
        if (!idc) {
            return std::unexpected(std::move(idc.error()));
        }
        if (*idc > 3) {
            return std::unexpected(make_slice_error(
                reader, "modification_of_pic_nums_idc for " +
                            std::string(list_name) + " must be in the range 0..3"));
        }
        if (*idc == 3) {
            if (modifications.empty()) {
                return std::unexpected(make_slice_error(
                    reader, "the first reference list modification for " +
                                std::string(list_name) + " must not be the end marker"));
            }
            break;
        }
        if (modifications.size() >= maximum_modifications) {
            return std::unexpected(make_slice_error(
                reader, "reference list modifications for " +
                            std::string(list_name) +
                            " exceed the active reference count"));
        }

        ReferencePictureListModification modification;
        modification.modification_of_pic_nums_idc = *idc;
        if (*idc == 0 || *idc == 1) {
            auto difference =
                read_slice_ue(reader, "abs_diff_pic_num_minus1");
            if (!difference) {
                return std::unexpected(std::move(difference.error()));
            }
            modification.abs_diff_pic_num_minus1 = *difference;
        } else {
            auto long_term = read_slice_ue(reader, "long_term_pic_num");
            if (!long_term) {
                return std::unexpected(std::move(long_term.error()));
            }
            modification.long_term_pic_num = *long_term;
        }
        modifications.push_back(std::move(modification));
    }
    return modifications;
}

[[nodiscard]] std::expected<void, ParseError>
validate_weight_value(const BitReader& reader,
                      std::int32_t value,
                      std::string_view field) {
    if (value < -128 || value > 127) {
        return std::unexpected(make_slice_error(
            reader, std::string(field) + " must be in the range -128..127"));
    }
    return {};
}

[[nodiscard]] std::expected<std::vector<PredictionWeight>, ParseError>
parse_prediction_weight_list(BitReader& reader,
                             std::uint32_t active_minus1,
                             bool has_chroma) {
    std::vector<PredictionWeight> weights;
    weights.reserve(static_cast<std::size_t>(active_minus1) + 1U);
    for (std::uint32_t index = 0; index <= active_minus1; ++index) {
        PredictionWeight weight;
        auto luma_flag = read_slice_flag(reader, "luma_weight_flag");
        if (!luma_flag) {
            return std::unexpected(std::move(luma_flag.error()));
        }
        weight.luma_weight_flag = *luma_flag;
        if (weight.luma_weight_flag) {
            auto value = read_slice_se(reader, "luma_weight");
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto valid = validate_weight_value(reader, *value, "luma_weight");
            if (!valid) {
                return std::unexpected(std::move(valid.error()));
            }
            weight.luma_weight = *value;
            auto offset = read_slice_se(reader, "luma_offset");
            if (!offset) {
                return std::unexpected(std::move(offset.error()));
            }
            valid = validate_weight_value(reader, *offset, "luma_offset");
            if (!valid) {
                return std::unexpected(std::move(valid.error()));
            }
            weight.luma_offset = *offset;
        }

        if (has_chroma) {
            auto chroma_flag = read_slice_flag(reader, "chroma_weight_flag");
            if (!chroma_flag) {
                return std::unexpected(std::move(chroma_flag.error()));
            }
            weight.chroma_weight_flag = *chroma_flag;
            if (weight.chroma_weight_flag) {
                for (std::size_t component = 0; component < 2; ++component) {
                    auto value = read_slice_se(reader, "chroma_weight");
                    if (!value) {
                        return std::unexpected(std::move(value.error()));
                    }
                    auto valid = validate_weight_value(
                        reader, *value, "chroma_weight");
                    if (!valid) {
                        return std::unexpected(std::move(valid.error()));
                    }
                    weight.chroma_weight[component] = *value;
                    auto offset = read_slice_se(reader, "chroma_offset");
                    if (!offset) {
                        return std::unexpected(std::move(offset.error()));
                    }
                    valid = validate_weight_value(
                        reader, *offset, "chroma_offset");
                    if (!valid) {
                        return std::unexpected(std::move(valid.error()));
                    }
                    weight.chroma_offset[component] = *offset;
                }
            }
        }
        weights.push_back(std::move(weight));
    }
    return weights;
}

[[nodiscard]] std::expected<void, ParseError>
parse_decoded_reference_picture_marking(BitReader& reader,
                                        const NalHeader& nal_header,
                                        const Sps& sps,
                                        SliceHeader& header) {
    if (nal_header.nal_ref_idc == 0) {
        return {};
    }
    if (header.idr) {
        auto no_output =
            read_slice_flag(reader, "no_output_of_prior_pics_flag");
        if (!no_output) {
            return std::unexpected(std::move(no_output.error()));
        }
        header.no_output_of_prior_pics_flag = *no_output;
        auto long_term = read_slice_flag(reader, "long_term_reference_flag");
        if (!long_term) {
            return std::unexpected(std::move(long_term.error()));
        }
        if (sps.max_num_ref_frames == 0 && *long_term) {
            return std::unexpected(make_slice_error(
                reader, "long_term_reference_flag must be zero when max_num_ref_frames is zero"));
        }
        header.long_term_reference_flag = *long_term;
        return {};
    }

    auto adaptive =
        read_slice_flag(reader, "adaptive_ref_pic_marking_mode_flag");
    if (!adaptive) {
        return std::unexpected(std::move(adaptive.error()));
    }
    header.adaptive_ref_pic_marking_mode_flag = *adaptive;
    if (!*adaptive) {
        return {};
    }

    bool seen_operation4 = false;
    bool seen_operation5 = false;
    bool seen_operation6 = false;
    while (true) {
        auto operation =
            read_slice_ue(reader, "memory_management_control_operation");
        if (!operation) {
            return std::unexpected(std::move(operation.error()));
        }
        if (*operation == 0) {
            break;
        }
        if (*operation > 6) {
            return std::unexpected(make_slice_error(
                reader, "memory_management_control_operation must be in the range 0..6"));
        }
        if (header.memory_management_operations.size() >=
            maximum_memory_management_operations) {
            return std::unexpected(make_slice_error(
                reader, "too many memory management control operations"));
        }
        if ((*operation == 4 && std::exchange(seen_operation4, true)) ||
            (*operation == 5 && std::exchange(seen_operation5, true)) ||
            (*operation == 6 && std::exchange(seen_operation6, true))) {
            return std::unexpected(make_slice_error(
                reader, "MMCO operations 4, 5, and 6 may each occur at most once"));
        }

        MemoryManagementControlOperation item;
        item.operation = *operation;
        if (*operation == 1 || *operation == 3) {
            auto difference =
                read_slice_ue(reader, "difference_of_pic_nums_minus1");
            if (!difference) {
                return std::unexpected(std::move(difference.error()));
            }
            item.difference_of_pic_nums_minus1 = *difference;
        }
        if (*operation == 2) {
            auto long_term = read_slice_ue(reader, "long_term_pic_num");
            if (!long_term) {
                return std::unexpected(std::move(long_term.error()));
            }
            item.long_term_pic_num = *long_term;
        }
        if (*operation == 3 || *operation == 6) {
            auto frame_index = read_slice_ue(reader, "long_term_frame_idx");
            if (!frame_index) {
                return std::unexpected(std::move(frame_index.error()));
            }
            item.long_term_frame_idx = *frame_index;
        }
        if (*operation == 4) {
            auto maximum =
                read_slice_ue(reader, "max_long_term_frame_idx_plus1");
            if (!maximum) {
                return std::unexpected(std::move(maximum.error()));
            }
            item.max_long_term_frame_idx_plus1 = *maximum;
        }
        header.memory_management_operations.push_back(std::move(item));
    }
    return {};
}

[[nodiscard]] std::size_t ceil_log2(std::uint64_t value) noexcept {
    if (value <= 1) {
        return 0;
    }
    std::size_t bits = 0;
    --value;
    while (value != 0) {
        ++bits;
        value >>= 1U;
    }
    return bits;
}

} // namespace

std::expected<SliceHeader, ParseError>
parse_slice_header(ByteView rbsp,
                   const NalHeader& nal_header,
                   const ParameterSetRegistry& parameter_sets) {
    if (nal_header.nal_unit_type != 1 && nal_header.nal_unit_type != 5) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::invalid_slice,
            .message = "NAL unit type is not a supported coded slice (1 or 5)",
        });
    }

    BitReader reader(rbsp);
    SliceHeader header;
    header.nal_ref_idc = nal_header.nal_ref_idc;

    auto read_ue = [&reader](std::string_view field)
        -> std::expected<std::uint32_t, ParseError> {
        auto value = reader.read_ue();
        if (!value) {
            return std::unexpected(
                with_slice_context(std::move(value.error()), field));
        }
        return *value;
    };
    auto read_se = [&reader](std::string_view field)
        -> std::expected<std::int32_t, ParseError> {
        auto value = reader.read_se();
        if (!value) {
            return std::unexpected(
                with_slice_context(std::move(value.error()), field));
        }
        return *value;
    };
    auto read_flag = [&reader](std::string_view field)
        -> std::expected<bool, ParseError> {
        auto value = reader.read_bit();
        if (!value) {
            return std::unexpected(
                with_slice_context(std::move(value.error()), field));
        }
        return *value;
    };

    auto first_mb = read_ue("first_mb_in_slice");
    if (!first_mb) {
        return std::unexpected(std::move(first_mb.error()));
    }
    header.first_mb_in_slice = *first_mb;

    auto slice_type = read_ue("slice_type");
    if (!slice_type) {
        return std::unexpected(std::move(slice_type.error()));
    }
    if (*slice_type > 9) {
        return std::unexpected(make_slice_error(
            reader, "slice_type must be in the range 0..9"));
    }
    header.slice_type_code = *slice_type;
    header.all_slices_same_type = *slice_type >= 5;
    header.slice_type = static_cast<SliceType>(*slice_type % 5U);

    auto pps_id = read_ue("pic_parameter_set_id");
    if (!pps_id) {
        return std::unexpected(std::move(pps_id.error()));
    }
    if (*pps_id > 255) {
        return std::unexpected(make_slice_error(
            reader, "pic_parameter_set_id must be in the range 0..255"));
    }
    header.pic_parameter_set_id = *pps_id;

    const auto* pps = parameter_sets.find_pps(header.pic_parameter_set_id);
    if (pps == nullptr) {
        return std::unexpected(make_missing_parameter_set_error(
            reader, "Slice references missing PPS " +
                        std::to_string(header.pic_parameter_set_id)));
    }
    header.seq_parameter_set_id = pps->seq_parameter_set_id;
    const auto* sps = parameter_sets.find_sps(pps->seq_parameter_set_id);
    if (sps == nullptr) {
        return std::unexpected(make_missing_parameter_set_error(
            reader, "PPS " + std::to_string(pps->pic_parameter_set_id) +
                        " references missing SPS " +
                        std::to_string(pps->seq_parameter_set_id)));
    }

    if (sps->separate_colour_plane_flag) {
        auto colour_plane = reader.read_bits(2);
        if (!colour_plane) {
            return std::unexpected(with_slice_context(
                std::move(colour_plane.error()), "colour_plane_id"));
        }
        if (*colour_plane > 2) {
            return std::unexpected(make_slice_error(
                reader, "colour_plane_id must be in the range 0..2"));
        }
        header.colour_plane_id = static_cast<std::uint8_t>(*colour_plane);
    }

    const auto frame_num_bits =
        static_cast<std::size_t>(sps->log2_max_frame_num_minus4) + 4U;
    auto frame_num = reader.read_bits(frame_num_bits);
    if (!frame_num) {
        return std::unexpected(with_slice_context(
            std::move(frame_num.error()), "frame_num"));
    }
    header.frame_num = *frame_num;

    header.idr = nal_header.nal_unit_type == 5;
    if (header.idr && nal_header.nal_ref_idc == 0) {
        return std::unexpected(make_slice_error(
            reader, "an IDR slice must have non-zero nal_ref_idc"));
    }
    if (header.idr && header.slice_type != SliceType::i &&
        header.slice_type != SliceType::si) {
        return std::unexpected(make_slice_error(
            reader, "an IDR NAL unit must contain an I or SI slice"));
    }
    if (header.idr && header.frame_num != 0) {
        return std::unexpected(make_slice_error(
            reader, "frame_num must be zero for an IDR picture"));
    }
    if (sps->max_num_ref_frames == 0 && header.slice_type != SliceType::i &&
        header.slice_type != SliceType::si) {
        return std::unexpected(make_slice_error(
            reader, "a stream with max_num_ref_frames equal to zero must use I or SI slices"));
    }

    if (!sps->frame_mbs_only_flag) {
        auto field_pic = read_flag("field_pic_flag");
        if (!field_pic) {
            return std::unexpected(std::move(field_pic.error()));
        }
        header.field_pic_flag = *field_pic;
        if (header.field_pic_flag) {
            auto bottom_field = read_flag("bottom_field_flag");
            if (!bottom_field) {
                return std::unexpected(std::move(bottom_field.error()));
            }
            header.bottom_field_flag = *bottom_field;
        }
    }

    if (header.idr) {
        auto idr_pic_id = read_ue("idr_pic_id");
        if (!idr_pic_id) {
            return std::unexpected(std::move(idr_pic_id.error()));
        }
        if (*idr_pic_id > 65'535) {
            return std::unexpected(make_slice_error(
                reader, "idr_pic_id must be in the range 0..65535"));
        }
        header.idr_pic_id = *idr_pic_id;
    }

    if (sps->pic_order_cnt_type == 0) {
        const auto poc_bits = static_cast<std::size_t>(
                                  sps->log2_max_pic_order_cnt_lsb_minus4) +
                              4U;
        auto poc_lsb = reader.read_bits(poc_bits);
        if (!poc_lsb) {
            return std::unexpected(with_slice_context(
                std::move(poc_lsb.error()), "pic_order_cnt_lsb"));
        }
        header.pic_order_cnt_lsb = *poc_lsb;
        if (pps->bottom_field_pic_order_in_frame_present_flag &&
            !header.field_pic_flag) {
            auto delta_bottom = read_se("delta_pic_order_bottom");
            if (!delta_bottom) {
                return std::unexpected(std::move(delta_bottom.error()));
            }
            header.delta_pic_order_bottom = *delta_bottom;
        }
    } else if (sps->pic_order_cnt_type == 1 &&
               !sps->delta_pic_order_always_zero_flag) {
        auto delta0 = read_se("delta_pic_order_cnt[0]");
        if (!delta0) {
            return std::unexpected(std::move(delta0.error()));
        }
        header.delta_pic_order_cnt0 = *delta0;
        if (pps->bottom_field_pic_order_in_frame_present_flag &&
            !header.field_pic_flag) {
            auto delta1 = read_se("delta_pic_order_cnt[1]");
            if (!delta1) {
                return std::unexpected(std::move(delta1.error()));
            }
            header.delta_pic_order_cnt1 = *delta1;
        }
    }

    if (pps->redundant_pic_cnt_present_flag) {
        auto redundant_pic_cnt = read_ue("redundant_pic_cnt");
        if (!redundant_pic_cnt) {
            return std::unexpected(std::move(redundant_pic_cnt.error()));
        }
        if (*redundant_pic_cnt > 127) {
            return std::unexpected(make_slice_error(
                reader, "redundant_pic_cnt must be in the range 0..127"));
        }
        header.redundant_pic_cnt = *redundant_pic_cnt;
    }

    if (header.slice_type == SliceType::b) {
        auto direct = read_flag("direct_spatial_mv_pred_flag");
        if (!direct) {
            return std::unexpected(std::move(direct.error()));
        }
        header.direct_spatial_mv_pred_flag = *direct;
    }

    if (uses_reference_lists(header.slice_type)) {
        auto override_flag = read_flag("num_ref_idx_active_override_flag");
        if (!override_flag) {
            return std::unexpected(std::move(override_flag.error()));
        }
        header.num_ref_idx_active_override_flag = *override_flag;
        std::uint32_t l0 = pps->num_ref_idx_l0_default_active_minus1;
        std::uint32_t l1 = pps->num_ref_idx_l1_default_active_minus1;
        if (*override_flag) {
            auto value = read_ue("num_ref_idx_l0_active_minus1");
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            l0 = *value;
            if (header.slice_type == SliceType::b) {
                value = read_ue("num_ref_idx_l1_active_minus1");
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                l1 = *value;
            }
        }
        if (l0 > maximum_active_reference_index ||
            (header.slice_type == SliceType::b &&
             l1 > maximum_active_reference_index)) {
            return std::unexpected(make_slice_error(
                reader, "active reference index must be in the range 0..31"));
        }
        header.num_ref_idx_l0_active_minus1 = l0;
        if (header.slice_type == SliceType::b) {
            header.num_ref_idx_l1_active_minus1 = l1;
        }
    }

    if (!is_intra_slice(header.slice_type)) {
        auto list0_flag = read_flag("ref_pic_list_modification_flag_l0");
        if (!list0_flag) {
            return std::unexpected(std::move(list0_flag.error()));
        }
        header.ref_pic_list_modification_flag_l0 = *list0_flag;
        if (*list0_flag) {
            auto modifications = parse_reference_picture_list_modifications(
                reader, *header.num_ref_idx_l0_active_minus1, "L0");
            if (!modifications) {
                return std::unexpected(std::move(modifications.error()));
            }
            header.ref_pic_list_modifications_l0 = std::move(*modifications);
        }
        if (header.slice_type == SliceType::b) {
            auto list1_flag = read_flag("ref_pic_list_modification_flag_l1");
            if (!list1_flag) {
                return std::unexpected(std::move(list1_flag.error()));
            }
            header.ref_pic_list_modification_flag_l1 = *list1_flag;
            if (*list1_flag) {
                auto modifications = parse_reference_picture_list_modifications(
                    reader, *header.num_ref_idx_l1_active_minus1, "L1");
                if (!modifications) {
                    return std::unexpected(std::move(modifications.error()));
                }
                header.ref_pic_list_modifications_l1 =
                    std::move(*modifications);
            }
        }
    }

    const bool weighted_prediction =
        (pps->weighted_pred_flag &&
         (header.slice_type == SliceType::p ||
          header.slice_type == SliceType::sp)) ||
        (pps->weighted_bipred_idc == 1 &&
         header.slice_type == SliceType::b);
    if (weighted_prediction) {
        header.prediction_weight_table_present = true;
        auto luma_denom = read_ue("luma_log2_weight_denom");
        if (!luma_denom) {
            return std::unexpected(std::move(luma_denom.error()));
        }
        if (*luma_denom > 7) {
            return std::unexpected(make_slice_error(
                reader, "luma_log2_weight_denom must be in the range 0..7"));
        }
        header.luma_log2_weight_denom = *luma_denom;
        const bool has_chroma = !sps->separate_colour_plane_flag &&
                                sps->chroma_format_idc != 0;
        if (has_chroma) {
            auto chroma_denom = read_ue("chroma_log2_weight_denom");
            if (!chroma_denom) {
                return std::unexpected(std::move(chroma_denom.error()));
            }
            if (*chroma_denom > 7) {
                return std::unexpected(make_slice_error(
                    reader, "chroma_log2_weight_denom must be in the range 0..7"));
            }
            header.chroma_log2_weight_denom = *chroma_denom;
        }
        auto weights_l0 = parse_prediction_weight_list(
            reader, *header.num_ref_idx_l0_active_minus1, has_chroma);
        if (!weights_l0) {
            return std::unexpected(std::move(weights_l0.error()));
        }
        header.prediction_weights_l0 = std::move(*weights_l0);
        if (header.slice_type == SliceType::b) {
            auto weights_l1 = parse_prediction_weight_list(
                reader, *header.num_ref_idx_l1_active_minus1, has_chroma);
            if (!weights_l1) {
                return std::unexpected(std::move(weights_l1.error()));
            }
            header.prediction_weights_l1 = std::move(*weights_l1);
        }
    }

    auto marking = parse_decoded_reference_picture_marking(
        reader, nal_header, *sps, header);
    if (!marking) {
        return std::unexpected(std::move(marking.error()));
    }

    if (pps->entropy_coding_mode_flag &&
        !is_intra_slice(header.slice_type)) {
        auto cabac = read_ue("cabac_init_idc");
        if (!cabac) {
            return std::unexpected(std::move(cabac.error()));
        }
        if (*cabac > 2) {
            return std::unexpected(make_slice_error(
                reader, "cabac_init_idc must be in the range 0..2"));
        }
        header.cabac_init_idc = *cabac;
    }

    auto qp_delta = read_se("slice_qp_delta");
    if (!qp_delta) {
        return std::unexpected(std::move(qp_delta.error()));
    }
    const auto bit_depth_luma = static_cast<std::int64_t>(sps->bit_depth_luma);
    if (bit_depth_luma < 8) {
        return std::unexpected(make_slice_error(
            reader, "referenced SPS luma bit depth is less than 8"));
    }
    const auto qp_bd_offset_y = 6 * (bit_depth_luma - 8);
    const auto qp_y = std::int64_t{26} + pps->pic_init_qp_minus26 +
                      static_cast<std::int64_t>(*qp_delta);
    if (qp_y < -qp_bd_offset_y || qp_y > 51) {
        return std::unexpected(make_slice_error(
            reader, "slice_qp_delta produces QP Y outside the valid range"));
    }
    header.slice_qp_delta = *qp_delta;

    if (header.slice_type == SliceType::sp ||
        header.slice_type == SliceType::si) {
        if (header.slice_type == SliceType::sp) {
            auto switch_flag = read_flag("sp_for_switch_flag");
            if (!switch_flag) {
                return std::unexpected(std::move(switch_flag.error()));
            }
            header.sp_for_switch_flag = *switch_flag;
        }
        auto qs_delta = read_se("slice_qs_delta");
        if (!qs_delta) {
            return std::unexpected(std::move(qs_delta.error()));
        }
        const auto qs_y = std::int64_t{26} + pps->pic_init_qs_minus26 +
                          static_cast<std::int64_t>(*qs_delta);
        if (qs_y < 0 || qs_y > 51) {
            return std::unexpected(make_slice_error(
                reader, "slice_qs_delta produces QS Y outside the range 0..51"));
        }
        header.slice_qs_delta = *qs_delta;
    }

    if (pps->deblocking_filter_control_present_flag) {
        auto disable = read_ue("disable_deblocking_filter_idc");
        if (!disable) {
            return std::unexpected(std::move(disable.error()));
        }
        if (*disable > 2) {
            return std::unexpected(make_slice_error(
                reader, "disable_deblocking_filter_idc must be in the range 0..2"));
        }
        header.disable_deblocking_filter_idc = *disable;
        if (*disable != 1) {
            auto alpha = read_se("slice_alpha_c0_offset_div2");
            if (!alpha) {
                return std::unexpected(std::move(alpha.error()));
            }
            auto beta = read_se("slice_beta_offset_div2");
            if (!beta) {
                return std::unexpected(std::move(beta.error()));
            }
            if (*alpha < -6 || *alpha > 6 || *beta < -6 || *beta > 6) {
                return std::unexpected(make_slice_error(
                    reader, "deblocking filter offsets must be in the range -6..6"));
            }
            header.slice_alpha_c0_offset_div2 = *alpha;
            header.slice_beta_offset_div2 = *beta;
        }
    }

    if (pps->num_slice_groups_minus1 > 0 &&
        pps->slice_group_map_type >= 3 &&
        pps->slice_group_map_type <= 5) {
        const std::uint64_t width_in_mbs = sps->coded_width / 16U;
        const std::uint64_t map_unit_height_divisor =
            sps->frame_mbs_only_flag ? 16U : 32U;
        const std::uint64_t height_in_map_units =
            sps->coded_height / map_unit_height_divisor;
        const std::uint64_t pic_size_in_map_units =
            width_in_mbs * height_in_map_units;
        if (pic_size_in_map_units == 0) {
            return std::unexpected(make_slice_error(
                reader, "referenced SPS has invalid dimensions for slice groups"));
        }
        const std::uint64_t change_rate =
            static_cast<std::uint64_t>(
                pps->slice_group_change_rate_minus1) + 1U;
        const std::uint64_t maximum_cycle =
            pic_size_in_map_units / change_rate +
            (pic_size_in_map_units % change_rate != 0 ? 1U : 0U);
        if (maximum_cycle > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(make_slice_error(
                reader, "slice_group_change_cycle exceeds the supported range"));
        }
        const auto cycle_bits = ceil_log2(maximum_cycle + 1U);
        auto cycle = reader.read_bits(cycle_bits);
        if (!cycle) {
            return std::unexpected(with_slice_context(
                std::move(cycle.error()), "slice_group_change_cycle"));
        }
        if (*cycle > maximum_cycle) {
            return std::unexpected(make_slice_error(
                reader, "slice_group_change_cycle exceeds its picture-size limit"));
        }
        header.slice_group_change_cycle = *cycle;
    }

    header.header_bit_size = reader.bit_position();
    return header;
}

const char* slice_type_name(SliceType type) noexcept {
    switch (type) {
    case SliceType::p:
        return "P";
    case SliceType::b:
        return "B";
    case SliceType::i:
        return "I";
    case SliceType::sp:
        return "SP";
    case SliceType::si:
        return "SI";
    }
    return "UNKNOWN";
}

} // namespace semi_stream_probe
