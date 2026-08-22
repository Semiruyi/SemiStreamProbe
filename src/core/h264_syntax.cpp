#include "semi_stream_probe/core/h264_syntax.hpp"

#include "semi_stream_probe/core/bit_reader.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace semi_stream_probe {

namespace {

constexpr std::uint32_t maximum_sps_id = 31;
constexpr std::uint32_t maximum_bit_depth_minus8 = 6;
constexpr std::uint32_t maximum_log2_minus4 = 12;
constexpr std::uint32_t maximum_pic_order_cycle_length = 255;
constexpr std::uint32_t maximum_pps_id = 255;
constexpr std::uint32_t maximum_slice_groups_minus1 = 7;
constexpr std::uint32_t maximum_default_reference_index = 31;
constexpr std::uint32_t maximum_pic_size_in_map_units_minus1 = 1'048'575;

[[nodiscard]] bool has_extended_profile_fields(std::uint8_t profile_idc) noexcept {
    switch (profile_idc) {
    case 44:
    case 83:
    case 86:
    case 100:
    case 110:
    case 118:
    case 122:
    case 128:
    case 134:
    case 135:
    case 138:
    case 139:
    case 144:
    case 244:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] ParseError with_sps_context(ParseError error,
                                          std::string_view field) {
    error.message = "SPS " + std::string(field) + ": " + error.message;
    return error;
}

[[nodiscard]] ParseError make_sps_error(const BitReader& reader,
                                        std::string message) {
    return ParseError{
        .code = ParseErrorCode::invalid_sps,
        .byte_offset = reader.bit_position() / 8,
        .bit_offset = reader.bit_position(),
        .message = std::move(message),
    };
}

[[nodiscard]] ParseError with_pps_context(ParseError error,
                                          std::string_view field) {
    error.message = "PPS " + std::string(field) + ": " + error.message;
    return error;
}

[[nodiscard]] ParseError make_pps_error(const BitReader& reader,
                                        std::string message) {
    return ParseError{
        .code = ParseErrorCode::invalid_pps,
        .byte_offset = reader.bit_position() / 8,
        .bit_offset = reader.bit_position(),
        .message = std::move(message),
    };
}

[[nodiscard]] bool bit_at(ByteView bytes, std::size_t bit_position) noexcept {
    const auto byte_position = bit_position / 8;
    const auto bit_in_byte = static_cast<unsigned int>(bit_position % 8);
    return ((bytes[byte_position] >> (7U - bit_in_byte)) & 0x01U) != 0;
}

// H.264 7.3.2.10: more_rbsp_data() is false only when the remaining bits are
// exactly rbsp_stop_one_bit followed by zero alignment bits.
[[nodiscard]] bool more_rbsp_data(const BitReader& reader) noexcept {
    if (reader.bits_remaining() == 0) {
        return false;
    }
    const auto start = reader.bit_position();
    if (!bit_at(reader.bytes(), start)) {
        return true;
    }
    const auto end = start + reader.bits_remaining();
    for (auto position = start + 1; position < end; ++position) {
        if (bit_at(reader.bytes(), position)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::expected<void, ParseError>
consume_rbsp_trailing_bits(BitReader& reader, std::string_view syntax_name) {
    auto stop_bit = reader.read_bit();
    if (!stop_bit) {
        auto error = std::move(stop_bit.error());
        error.message = std::string(syntax_name) +
                        " rbsp_trailing_bits: " + error.message;
        return std::unexpected(std::move(error));
    }
    if (!*stop_bit) {
        return std::unexpected(make_pps_error(
            reader, "rbsp_stop_one_bit must be one"));
    }
    while (reader.bits_remaining() != 0) {
        auto alignment_bit = reader.read_bit();
        if (!alignment_bit) {
            return std::unexpected(std::move(alignment_bit.error()));
        }
        if (*alignment_bit) {
            return std::unexpected(make_pps_error(
                reader, "rbsp_alignment_zero_bit must be zero"));
        }
    }
    return {};
}

[[nodiscard]] std::size_t slice_group_id_bit_count(
    std::uint32_t num_slice_groups_minus1) noexcept {
    const auto group_count = num_slice_groups_minus1 + 1U;
    std::size_t count = 0;
    std::uint32_t maximum_id = group_count - 1U;
    do {
        ++count;
        maximum_id >>= 1U;
    } while (maximum_id != 0);
    return count;
}

[[nodiscard]] std::expected<void, ParseError>
skip_scaling_list(BitReader& reader,
                  std::size_t size,
                  std::string_view syntax_name) {
    std::int32_t last_scale = 8;
    std::int32_t next_scale = 8;

    for (std::size_t index = 0; index < size; ++index) {
        if (next_scale != 0) {
            auto delta_scale = reader.read_se();
            if (!delta_scale) {
                auto error = std::move(delta_scale.error());
                error.message = std::string(syntax_name) +
                                " scaling_list delta_scale: " + error.message;
                return std::unexpected(std::move(error));
            }

            auto value = (static_cast<std::int64_t>(last_scale) +
                          static_cast<std::int64_t>(*delta_scale)) %
                         256;
            if (value < 0) {
                value += 256;
            }
            next_scale = static_cast<std::int32_t>(value);
        }
        last_scale = next_scale == 0 ? last_scale : next_scale;
    }
    return {};
}

[[nodiscard]] std::expected<void, ParseError>
parse_extended_profile_fields(BitReader& reader, Sps& sps) {
    auto chroma_format_idc = reader.read_ue();
    if (!chroma_format_idc) {
        return std::unexpected(with_sps_context(
            std::move(chroma_format_idc.error()), "chroma_format_idc"));
    }
    if (*chroma_format_idc > 3) {
        return std::unexpected(make_sps_error(
            reader, "chroma_format_idc must be in the range 0..3"));
    }
    sps.chroma_format_idc = *chroma_format_idc;

    if (sps.chroma_format_idc == 3) {
        auto separate_colour_plane_flag = reader.read_bit();
        if (!separate_colour_plane_flag) {
            return std::unexpected(with_sps_context(
                std::move(separate_colour_plane_flag.error()),
                "separate_colour_plane_flag"));
        }
        sps.separate_colour_plane_flag = *separate_colour_plane_flag;
    }

    auto bit_depth_luma_minus8 = reader.read_ue();
    if (!bit_depth_luma_minus8) {
        return std::unexpected(with_sps_context(
            std::move(bit_depth_luma_minus8.error()), "bit_depth_luma_minus8"));
    }
    auto bit_depth_chroma_minus8 = reader.read_ue();
    if (!bit_depth_chroma_minus8) {
        return std::unexpected(with_sps_context(
            std::move(bit_depth_chroma_minus8.error()),
            "bit_depth_chroma_minus8"));
    }
    if (*bit_depth_luma_minus8 > maximum_bit_depth_minus8 ||
        *bit_depth_chroma_minus8 > maximum_bit_depth_minus8) {
        return std::unexpected(make_sps_error(
            reader, "bit depth minus 8 must be in the range 0..6"));
    }
    sps.bit_depth_luma =
        static_cast<std::uint8_t>(8U + *bit_depth_luma_minus8);
    sps.bit_depth_chroma =
        static_cast<std::uint8_t>(8U + *bit_depth_chroma_minus8);

    auto qpprime_y_zero_transform_bypass_flag = reader.read_bit();
    if (!qpprime_y_zero_transform_bypass_flag) {
        return std::unexpected(with_sps_context(
            std::move(qpprime_y_zero_transform_bypass_flag.error()),
            "qpprime_y_zero_transform_bypass_flag"));
    }

    auto seq_scaling_matrix_present_flag = reader.read_bit();
    if (!seq_scaling_matrix_present_flag) {
        return std::unexpected(with_sps_context(
            std::move(seq_scaling_matrix_present_flag.error()),
            "seq_scaling_matrix_present_flag"));
    }
    if (!*seq_scaling_matrix_present_flag) {
        return {};
    }

    const std::size_t scaling_list_count =
        sps.chroma_format_idc == 3 ? 12 : 8;
    for (std::size_t index = 0; index < scaling_list_count; ++index) {
        auto present = reader.read_bit();
        if (!present) {
            return std::unexpected(with_sps_context(
                std::move(present.error()), "seq_scaling_list_present_flag"));
        }
        if (*present) {
            auto skipped = skip_scaling_list(reader, index < 6 ? 16 : 64,
                                             "SPS");
            if (!skipped) {
                return std::unexpected(std::move(skipped.error()));
            }
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, ParseError>
parse_picture_order_fields(BitReader& reader, Sps& sps) {
    auto pic_order_cnt_type = reader.read_ue();
    if (!pic_order_cnt_type) {
        return std::unexpected(with_sps_context(
            std::move(pic_order_cnt_type.error()), "pic_order_cnt_type"));
    }
    if (*pic_order_cnt_type > 2) {
        return std::unexpected(make_sps_error(
            reader, "pic_order_cnt_type must be in the range 0..2"));
    }
    sps.pic_order_cnt_type = *pic_order_cnt_type;

    if (sps.pic_order_cnt_type == 0) {
        auto log2_max_pic_order_cnt_lsb_minus4 = reader.read_ue();
        if (!log2_max_pic_order_cnt_lsb_minus4) {
            return std::unexpected(with_sps_context(
                std::move(log2_max_pic_order_cnt_lsb_minus4.error()),
                "log2_max_pic_order_cnt_lsb_minus4"));
        }
        if (*log2_max_pic_order_cnt_lsb_minus4 > maximum_log2_minus4) {
            return std::unexpected(make_sps_error(
                reader, "log2_max_pic_order_cnt_lsb_minus4 exceeds 12"));
        }
        return {};
    }

    if (sps.pic_order_cnt_type == 2) {
        return {};
    }

    auto delta_pic_order_always_zero_flag = reader.read_bit();
    if (!delta_pic_order_always_zero_flag) {
        return std::unexpected(with_sps_context(
            std::move(delta_pic_order_always_zero_flag.error()),
            "delta_pic_order_always_zero_flag"));
    }
    auto offset_for_non_ref_pic = reader.read_se();
    if (!offset_for_non_ref_pic) {
        return std::unexpected(with_sps_context(
            std::move(offset_for_non_ref_pic.error()), "offset_for_non_ref_pic"));
    }
    auto offset_for_top_to_bottom_field = reader.read_se();
    if (!offset_for_top_to_bottom_field) {
        return std::unexpected(with_sps_context(
            std::move(offset_for_top_to_bottom_field.error()),
            "offset_for_top_to_bottom_field"));
    }

    auto cycle_length = reader.read_ue();
    if (!cycle_length) {
        return std::unexpected(with_sps_context(
            std::move(cycle_length.error()),
            "num_ref_frames_in_pic_order_cnt_cycle"));
    }
    if (*cycle_length > maximum_pic_order_cycle_length) {
        return std::unexpected(make_sps_error(
            reader, "pic order count cycle length exceeds 255"));
    }
    for (std::uint32_t index = 0; index < *cycle_length; ++index) {
        auto offset = reader.read_se();
        if (!offset) {
            return std::unexpected(with_sps_context(
                std::move(offset.error()), "offset_for_ref_frame"));
        }
    }
    return {};
}

[[nodiscard]] std::expected<void, ParseError>
calculate_dimensions(BitReader& reader,
                     Sps& sps,
                     std::uint32_t pic_width_in_mbs_minus1,
                     std::uint32_t pic_height_in_map_units_minus1) {
    const std::uint64_t frame_height_factor =
        sps.frame_mbs_only_flag ? 1U : 2U;
    const std::uint64_t coded_width =
        (static_cast<std::uint64_t>(pic_width_in_mbs_minus1) + 1U) * 16U;
    const std::uint64_t coded_height =
        frame_height_factor *
        (static_cast<std::uint64_t>(pic_height_in_map_units_minus1) + 1U) *
        16U;

    if (coded_width > std::numeric_limits<std::uint32_t>::max() ||
        coded_height > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(make_sps_error(
            reader, "coded dimensions exceed the supported 32-bit range"));
    }

    const std::uint32_t chroma_array_type =
        sps.separate_colour_plane_flag ? 0U : sps.chroma_format_idc;
    std::uint64_t crop_unit_x = 1;
    std::uint64_t crop_unit_y = frame_height_factor;
    if (chroma_array_type == 1) {
        crop_unit_x = 2;
        crop_unit_y = 2U * frame_height_factor;
    } else if (chroma_array_type == 2) {
        crop_unit_x = 2;
        crop_unit_y = frame_height_factor;
    } else if (chroma_array_type == 3) {
        crop_unit_x = 1;
        crop_unit_y = frame_height_factor;
    }

    const std::uint64_t horizontal_crop =
        crop_unit_x *
        (static_cast<std::uint64_t>(sps.frame_crop_left_offset) +
         static_cast<std::uint64_t>(sps.frame_crop_right_offset));
    const std::uint64_t vertical_crop =
        crop_unit_y *
        (static_cast<std::uint64_t>(sps.frame_crop_top_offset) +
         static_cast<std::uint64_t>(sps.frame_crop_bottom_offset));
    if (horizontal_crop >= coded_width || vertical_crop >= coded_height) {
        return std::unexpected(make_sps_error(
            reader, "frame cropping removes the complete coded picture"));
    }

    sps.coded_width = static_cast<std::uint32_t>(coded_width);
    sps.coded_height = static_cast<std::uint32_t>(coded_height);
    sps.width = static_cast<std::uint32_t>(coded_width - horizontal_crop);
    sps.height = static_cast<std::uint32_t>(coded_height - vertical_crop);
    return {};
}

} // namespace

std::expected<Sps, ParseError> parse_sps(ByteView rbsp) {
    BitReader reader(rbsp);
    Sps sps;

    auto profile_idc = reader.read_bits(8);
    if (!profile_idc) {
        return std::unexpected(with_sps_context(
            std::move(profile_idc.error()), "profile_idc"));
    }
    sps.profile_idc = static_cast<std::uint8_t>(*profile_idc);

    auto constraint_and_reserved = reader.read_bits(8);
    if (!constraint_and_reserved) {
        return std::unexpected(with_sps_context(
            std::move(constraint_and_reserved.error()), "constraint flags"));
    }
    if ((*constraint_and_reserved & 0x03U) != 0) {
        return std::unexpected(make_sps_error(
            reader, "reserved_zero_2bits must be zero"));
    }
    sps.constraint_set_flags =
        static_cast<std::uint8_t>(*constraint_and_reserved >> 2U);

    auto level_idc = reader.read_bits(8);
    if (!level_idc) {
        return std::unexpected(with_sps_context(
            std::move(level_idc.error()), "level_idc"));
    }
    sps.level_idc = static_cast<std::uint8_t>(*level_idc);

    auto seq_parameter_set_id = reader.read_ue();
    if (!seq_parameter_set_id) {
        return std::unexpected(with_sps_context(
            std::move(seq_parameter_set_id.error()), "seq_parameter_set_id"));
    }
    if (*seq_parameter_set_id > maximum_sps_id) {
        return std::unexpected(make_sps_error(
            reader, "seq_parameter_set_id must be in the range 0..31"));
    }
    sps.seq_parameter_set_id = *seq_parameter_set_id;

    if (has_extended_profile_fields(sps.profile_idc)) {
        auto extended = parse_extended_profile_fields(reader, sps);
        if (!extended) {
            return std::unexpected(std::move(extended.error()));
        }
    }

    auto log2_max_frame_num_minus4 = reader.read_ue();
    if (!log2_max_frame_num_minus4) {
        return std::unexpected(with_sps_context(
            std::move(log2_max_frame_num_minus4.error()),
            "log2_max_frame_num_minus4"));
    }
    if (*log2_max_frame_num_minus4 > maximum_log2_minus4) {
        return std::unexpected(make_sps_error(
            reader, "log2_max_frame_num_minus4 exceeds 12"));
    }
    sps.log2_max_frame_num_minus4 = *log2_max_frame_num_minus4;

    auto picture_order = parse_picture_order_fields(reader, sps);
    if (!picture_order) {
        return std::unexpected(std::move(picture_order.error()));
    }

    auto max_num_ref_frames = reader.read_ue();
    if (!max_num_ref_frames) {
        return std::unexpected(with_sps_context(
            std::move(max_num_ref_frames.error()), "max_num_ref_frames"));
    }
    sps.max_num_ref_frames = *max_num_ref_frames;

    auto gaps_in_frame_num_value_allowed_flag = reader.read_bit();
    if (!gaps_in_frame_num_value_allowed_flag) {
        return std::unexpected(with_sps_context(
            std::move(gaps_in_frame_num_value_allowed_flag.error()),
            "gaps_in_frame_num_value_allowed_flag"));
    }

    auto pic_width_in_mbs_minus1 = reader.read_ue();
    if (!pic_width_in_mbs_minus1) {
        return std::unexpected(with_sps_context(
            std::move(pic_width_in_mbs_minus1.error()),
            "pic_width_in_mbs_minus1"));
    }
    auto pic_height_in_map_units_minus1 = reader.read_ue();
    if (!pic_height_in_map_units_minus1) {
        return std::unexpected(with_sps_context(
            std::move(pic_height_in_map_units_minus1.error()),
            "pic_height_in_map_units_minus1"));
    }

    auto frame_mbs_only_flag = reader.read_bit();
    if (!frame_mbs_only_flag) {
        return std::unexpected(with_sps_context(
            std::move(frame_mbs_only_flag.error()), "frame_mbs_only_flag"));
    }
    sps.frame_mbs_only_flag = *frame_mbs_only_flag;
    if (!sps.frame_mbs_only_flag) {
        auto mb_adaptive_frame_field_flag = reader.read_bit();
        if (!mb_adaptive_frame_field_flag) {
            return std::unexpected(with_sps_context(
                std::move(mb_adaptive_frame_field_flag.error()),
                "mb_adaptive_frame_field_flag"));
        }
    }

    auto direct_8x8_inference_flag = reader.read_bit();
    if (!direct_8x8_inference_flag) {
        return std::unexpected(with_sps_context(
            std::move(direct_8x8_inference_flag.error()),
            "direct_8x8_inference_flag"));
    }

    auto frame_cropping_flag = reader.read_bit();
    if (!frame_cropping_flag) {
        return std::unexpected(with_sps_context(
            std::move(frame_cropping_flag.error()), "frame_cropping_flag"));
    }
    if (*frame_cropping_flag) {
        auto left = reader.read_ue();
        if (!left) {
            return std::unexpected(with_sps_context(
                std::move(left.error()), "frame_crop_left_offset"));
        }
        auto right = reader.read_ue();
        if (!right) {
            return std::unexpected(with_sps_context(
                std::move(right.error()), "frame_crop_right_offset"));
        }
        auto top = reader.read_ue();
        if (!top) {
            return std::unexpected(with_sps_context(
                std::move(top.error()), "frame_crop_top_offset"));
        }
        auto bottom = reader.read_ue();
        if (!bottom) {
            return std::unexpected(with_sps_context(
                std::move(bottom.error()), "frame_crop_bottom_offset"));
        }
        sps.frame_crop_left_offset = *left;
        sps.frame_crop_right_offset = *right;
        sps.frame_crop_top_offset = *top;
        sps.frame_crop_bottom_offset = *bottom;
    }

    auto dimensions = calculate_dimensions(
        reader, sps, *pic_width_in_mbs_minus1,
        *pic_height_in_map_units_minus1);
    if (!dimensions) {
        return std::unexpected(std::move(dimensions.error()));
    }

    auto vui_parameters_present_flag = reader.read_bit();
    if (!vui_parameters_present_flag) {
        return std::unexpected(with_sps_context(
            std::move(vui_parameters_present_flag.error()),
            "vui_parameters_present_flag"));
    }
    sps.vui_parameters_present_flag = *vui_parameters_present_flag;
    return sps;
}

std::expected<Pps, ParseError> parse_pps(ByteView rbsp,
                                        std::uint32_t chroma_format_idc) {
    if (chroma_format_idc > 3) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::invalid_pps,
            .message = "PPS referenced SPS chroma_format_idc must be in the range 0..3",
        });
    }

    BitReader reader(rbsp);
    Pps pps;

    auto read_ue = [&reader](std::string_view field)
        -> std::expected<std::uint32_t, ParseError> {
        auto value = reader.read_ue();
        if (!value) {
            return std::unexpected(with_pps_context(std::move(value.error()), field));
        }
        return *value;
    };
    auto read_se = [&reader](std::string_view field)
        -> std::expected<std::int32_t, ParseError> {
        auto value = reader.read_se();
        if (!value) {
            return std::unexpected(with_pps_context(std::move(value.error()), field));
        }
        return *value;
    };
    auto read_flag = [&reader](std::string_view field)
        -> std::expected<bool, ParseError> {
        auto value = reader.read_bit();
        if (!value) {
            return std::unexpected(with_pps_context(std::move(value.error()), field));
        }
        return *value;
    };

    auto pps_id = read_ue("pic_parameter_set_id");
    if (!pps_id) {
        return std::unexpected(std::move(pps_id.error()));
    }
    if (*pps_id > maximum_pps_id) {
        return std::unexpected(make_pps_error(
            reader, "pic_parameter_set_id must be in the range 0..255"));
    }
    pps.pic_parameter_set_id = *pps_id;

    auto sps_id = read_ue("seq_parameter_set_id");
    if (!sps_id) {
        return std::unexpected(std::move(sps_id.error()));
    }
    if (*sps_id > maximum_sps_id) {
        return std::unexpected(make_pps_error(
            reader, "seq_parameter_set_id must be in the range 0..31"));
    }
    pps.seq_parameter_set_id = *sps_id;

    auto entropy_coding = read_flag("entropy_coding_mode_flag");
    if (!entropy_coding) {
        return std::unexpected(std::move(entropy_coding.error()));
    }
    pps.entropy_coding_mode_flag = *entropy_coding;

    auto bottom_field =
        read_flag("bottom_field_pic_order_in_frame_present_flag");
    if (!bottom_field) {
        return std::unexpected(std::move(bottom_field.error()));
    }
    pps.bottom_field_pic_order_in_frame_present_flag = *bottom_field;

    auto slice_groups = read_ue("num_slice_groups_minus1");
    if (!slice_groups) {
        return std::unexpected(std::move(slice_groups.error()));
    }
    if (*slice_groups > maximum_slice_groups_minus1) {
        return std::unexpected(make_pps_error(
            reader, "num_slice_groups_minus1 must be in the range 0..7"));
    }
    pps.num_slice_groups_minus1 = *slice_groups;

    if (pps.num_slice_groups_minus1 > 0) {
        auto map_type = read_ue("slice_group_map_type");
        if (!map_type) {
            return std::unexpected(std::move(map_type.error()));
        }
        if (*map_type > 6) {
            return std::unexpected(make_pps_error(
                reader, "slice_group_map_type must be in the range 0..6"));
        }
        pps.slice_group_map_type = *map_type;

        if (pps.slice_group_map_type == 0) {
            pps.run_length_minus1.reserve(
                static_cast<std::size_t>(pps.num_slice_groups_minus1) + 1U);
            for (std::uint32_t index = 0;
                 index <= pps.num_slice_groups_minus1; ++index) {
                auto value = read_ue("run_length_minus1");
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                pps.run_length_minus1.push_back(*value);
            }
        } else if (pps.slice_group_map_type == 2) {
            pps.top_left.reserve(pps.num_slice_groups_minus1);
            pps.bottom_right.reserve(pps.num_slice_groups_minus1);
            for (std::uint32_t index = 0;
                 index < pps.num_slice_groups_minus1; ++index) {
                auto top_left = read_ue("top_left");
                if (!top_left) {
                    return std::unexpected(std::move(top_left.error()));
                }
                auto bottom_right = read_ue("bottom_right");
                if (!bottom_right) {
                    return std::unexpected(std::move(bottom_right.error()));
                }
                if (*top_left > *bottom_right) {
                    return std::unexpected(make_pps_error(
                        reader, "top_left must not exceed bottom_right"));
                }
                pps.top_left.push_back(*top_left);
                pps.bottom_right.push_back(*bottom_right);
            }
        } else if (pps.slice_group_map_type >= 3 &&
                   pps.slice_group_map_type <= 5) {
            auto direction = read_flag("slice_group_change_direction_flag");
            if (!direction) {
                return std::unexpected(std::move(direction.error()));
            }
            pps.slice_group_change_direction_flag = *direction;
            auto rate = read_ue("slice_group_change_rate_minus1");
            if (!rate) {
                return std::unexpected(std::move(rate.error()));
            }
            pps.slice_group_change_rate_minus1 = *rate;
        } else if (pps.slice_group_map_type == 6) {
            auto map_size = read_ue("pic_size_in_map_units_minus1");
            if (!map_size) {
                return std::unexpected(std::move(map_size.error()));
            }
            if (*map_size > maximum_pic_size_in_map_units_minus1) {
                return std::unexpected(make_pps_error(
                    reader, "explicit slice group map exceeds the supported limit"));
            }
            pps.pic_size_in_map_units_minus1 = *map_size;
            const auto id_bits =
                slice_group_id_bit_count(pps.num_slice_groups_minus1);
            pps.slice_group_id.reserve(static_cast<std::size_t>(*map_size) + 1U);
            for (std::uint32_t index = 0; index <= *map_size; ++index) {
                auto id = reader.read_bits(id_bits);
                if (!id) {
                    return std::unexpected(with_pps_context(
                        std::move(id.error()), "slice_group_id"));
                }
                if (*id > pps.num_slice_groups_minus1) {
                    return std::unexpected(make_pps_error(
                        reader, "slice_group_id references a nonexistent group"));
                }
                pps.slice_group_id.push_back(static_cast<std::uint8_t>(*id));
            }
        }
    }

    auto ref_l0 = read_ue("num_ref_idx_l0_default_active_minus1");
    if (!ref_l0) {
        return std::unexpected(std::move(ref_l0.error()));
    }
    auto ref_l1 = read_ue("num_ref_idx_l1_default_active_minus1");
    if (!ref_l1) {
        return std::unexpected(std::move(ref_l1.error()));
    }
    if (*ref_l0 > maximum_default_reference_index ||
        *ref_l1 > maximum_default_reference_index) {
        return std::unexpected(make_pps_error(
            reader, "default active reference index must be in the range 0..31"));
    }
    pps.num_ref_idx_l0_default_active_minus1 = *ref_l0;
    pps.num_ref_idx_l1_default_active_minus1 = *ref_l1;

    auto weighted_pred = read_flag("weighted_pred_flag");
    if (!weighted_pred) {
        return std::unexpected(std::move(weighted_pred.error()));
    }
    pps.weighted_pred_flag = *weighted_pred;
    auto weighted_bipred = reader.read_bits(2);
    if (!weighted_bipred) {
        return std::unexpected(with_pps_context(
            std::move(weighted_bipred.error()), "weighted_bipred_idc"));
    }
    if (*weighted_bipred > 2) {
        return std::unexpected(make_pps_error(
            reader, "weighted_bipred_idc must be in the range 0..2"));
    }
    pps.weighted_bipred_idc = static_cast<std::uint8_t>(*weighted_bipred);

    auto initial_qp = read_se("pic_init_qp_minus26");
    if (!initial_qp) {
        return std::unexpected(std::move(initial_qp.error()));
    }
    if (*initial_qp < -62 || *initial_qp > 25) {
        return std::unexpected(make_pps_error(
            reader, "pic_init_qp_minus26 is outside the supported H.264 range"));
    }
    pps.pic_init_qp_minus26 = *initial_qp;

    auto initial_qs = read_se("pic_init_qs_minus26");
    if (!initial_qs) {
        return std::unexpected(std::move(initial_qs.error()));
    }
    if (*initial_qs < -26 || *initial_qs > 25) {
        return std::unexpected(make_pps_error(
            reader, "pic_init_qs_minus26 must be in the range -26..25"));
    }
    pps.pic_init_qs_minus26 = *initial_qs;

    auto chroma_offset = read_se("chroma_qp_index_offset");
    if (!chroma_offset) {
        return std::unexpected(std::move(chroma_offset.error()));
    }
    if (*chroma_offset < -12 || *chroma_offset > 12) {
        return std::unexpected(make_pps_error(
            reader, "chroma_qp_index_offset must be in the range -12..12"));
    }
    pps.chroma_qp_index_offset = *chroma_offset;

    auto deblocking = read_flag("deblocking_filter_control_present_flag");
    if (!deblocking) {
        return std::unexpected(std::move(deblocking.error()));
    }
    pps.deblocking_filter_control_present_flag = *deblocking;
    auto constrained = read_flag("constrained_intra_pred_flag");
    if (!constrained) {
        return std::unexpected(std::move(constrained.error()));
    }
    pps.constrained_intra_pred_flag = *constrained;
    auto redundant = read_flag("redundant_pic_cnt_present_flag");
    if (!redundant) {
        return std::unexpected(std::move(redundant.error()));
    }
    pps.redundant_pic_cnt_present_flag = *redundant;

    if (more_rbsp_data(reader)) {
        pps.has_extension = true;
        auto transform = read_flag("transform_8x8_mode_flag");
        if (!transform) {
            return std::unexpected(std::move(transform.error()));
        }
        pps.transform_8x8_mode_flag = *transform;
        auto scaling_matrix = read_flag("pic_scaling_matrix_present_flag");
        if (!scaling_matrix) {
            return std::unexpected(std::move(scaling_matrix.error()));
        }
        pps.pic_scaling_matrix_present_flag = *scaling_matrix;
        if (pps.pic_scaling_matrix_present_flag) {
            const std::size_t list_count =
                6U + (pps.transform_8x8_mode_flag
                          ? (chroma_format_idc == 3 ? 6U : 2U)
                          : 0U);
            pps.pic_scaling_list_present_flags.reserve(list_count);
            for (std::size_t index = 0; index < list_count; ++index) {
                auto present = read_flag("pic_scaling_list_present_flag");
                if (!present) {
                    return std::unexpected(std::move(present.error()));
                }
                pps.pic_scaling_list_present_flags.push_back(*present);
                if (*present) {
                    auto skipped = skip_scaling_list(
                        reader, index < 6 ? 16 : 64, "PPS");
                    if (!skipped) {
                        return std::unexpected(std::move(skipped.error()));
                    }
                }
            }
        }

        auto second_offset = read_se("second_chroma_qp_index_offset");
        if (!second_offset) {
            return std::unexpected(std::move(second_offset.error()));
        }
        if (*second_offset < -12 || *second_offset > 12) {
            return std::unexpected(make_pps_error(
                reader, "second_chroma_qp_index_offset must be in the range -12..12"));
        }
        pps.second_chroma_qp_index_offset = *second_offset;
    } else {
        pps.second_chroma_qp_index_offset = pps.chroma_qp_index_offset;
    }

    auto trailing = consume_rbsp_trailing_bits(reader, "PPS");
    if (!trailing) {
        return std::unexpected(std::move(trailing.error()));
    }
    return pps;
}

const char* h264_profile_name(std::uint8_t profile_idc) noexcept {
    switch (profile_idc) {
    case 44:
        return "CAVLC 4:4:4 Intra";
    case 66:
        return "Baseline";
    case 77:
        return "Main";
    case 83:
        return "Scalable Baseline";
    case 86:
        return "Scalable High";
    case 88:
        return "Extended";
    case 100:
        return "High";
    case 110:
        return "High 10";
    case 118:
        return "Multiview High";
    case 122:
        return "High 4:2:2";
    case 128:
        return "Stereo High";
    case 134:
        return "MFC High";
    case 135:
        return "MFC Depth High";
    case 138:
        return "Multiview Depth High";
    case 139:
        return "Enhanced Multiview Depth High";
    case 144:
        return "High 4:4:4";
    case 244:
        return "High 4:4:4 Predictive";
    default:
        return "Unknown";
    }
}

std::string h264_level_name(const Sps& sps) {
    constexpr std::uint8_t constraint_set3_mask = 0x04;
    const bool level_1b_profile =
        sps.profile_idc == 66 || sps.profile_idc == 77 || sps.profile_idc == 88;
    if (sps.level_idc == 11 && level_1b_profile &&
        (sps.constraint_set_flags & constraint_set3_mask) != 0) {
        return "1b";
    }

    return std::to_string(sps.level_idc / 10U) + "." +
           std::to_string(sps.level_idc % 10U);
}

} // namespace semi_stream_probe
