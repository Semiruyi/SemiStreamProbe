#pragma once

#include "semi_stream_probe/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace semi_stream_probe::test {

class BitWriter {
public:
    void write_bit(bool value) {
        if ((bit_count_ % 8U) == 0) {
            bytes_.push_back(0);
        }
        if (value) {
            const auto bit_in_byte = static_cast<unsigned int>(bit_count_ % 8U);
            bytes_.back() |= static_cast<Byte>(1U << (7U - bit_in_byte));
        }
        ++bit_count_;
    }

    void write_bits(std::uint64_t value, std::size_t count) {
        for (std::size_t index = count; index > 0; --index) {
            const auto shift = static_cast<unsigned int>(index - 1);
            write_bit(((value >> shift) & 1U) != 0);
        }
    }

    void write_ue(std::uint32_t value) {
        const std::uint64_t code_word = static_cast<std::uint64_t>(value) + 1U;
        std::size_t bit_length = 0;
        for (auto remaining = code_word; remaining != 0; remaining >>= 1U) {
            ++bit_length;
        }
        for (std::size_t index = 1; index < bit_length; ++index) {
            write_bit(false);
        }
        write_bits(code_word, bit_length);
    }

    void write_se(std::int32_t value) {
        const auto wide_value = static_cast<std::int64_t>(value);
        const std::uint64_t code_num = wide_value <= 0
                                           ? static_cast<std::uint64_t>(-wide_value) * 2U
                                           : static_cast<std::uint64_t>(wide_value) * 2U - 1U;
        write_ue(static_cast<std::uint32_t>(code_num));
    }

    [[nodiscard]] ByteBuffer finish_rbsp() {
        write_bit(true);
        while ((bit_count_ % 8U) != 0) {
            write_bit(false);
        }
        return std::move(bytes_);
    }

private:
    ByteBuffer bytes_;
    std::size_t bit_count_{0};
};

struct BaselineSpsOptions {
    std::uint8_t profile_idc{66};
    std::uint8_t constraint_and_reserved{0};
    std::uint8_t level_idc{40};
    std::uint32_t seq_parameter_set_id{0};
    std::uint32_t pic_order_cnt_type{0};
    std::uint32_t width_in_mbs{120};
    std::uint32_t height_in_map_units{68};
    bool frame_mbs_only_flag{true};
    std::uint32_t crop_left{0};
    std::uint32_t crop_right{0};
    std::uint32_t crop_top{0};
    std::uint32_t crop_bottom{0};
    bool vui_parameters_present_flag{false};
};

[[nodiscard]] inline ByteBuffer
make_baseline_sps_rbsp(const BaselineSpsOptions& options = {}) {
    BitWriter writer;
    writer.write_bits(options.profile_idc, 8);
    writer.write_bits(options.constraint_and_reserved, 8);
    writer.write_bits(options.level_idc, 8);
    writer.write_ue(options.seq_parameter_set_id);
    writer.write_ue(0); // log2_max_frame_num_minus4
    writer.write_ue(options.pic_order_cnt_type);
    if (options.pic_order_cnt_type == 0) {
        writer.write_ue(0); // log2_max_pic_order_cnt_lsb_minus4
    } else if (options.pic_order_cnt_type == 1) {
        writer.write_bit(false); // delta_pic_order_always_zero_flag
        writer.write_se(0);      // offset_for_non_ref_pic
        writer.write_se(0);      // offset_for_top_to_bottom_field
        writer.write_ue(0);      // num_ref_frames_in_pic_order_cnt_cycle
    }
    writer.write_ue(1);     // max_num_ref_frames
    writer.write_bit(false); // gaps_in_frame_num_value_allowed_flag
    writer.write_ue(options.width_in_mbs - 1U);
    writer.write_ue(options.height_in_map_units - 1U);
    writer.write_bit(options.frame_mbs_only_flag);
    if (!options.frame_mbs_only_flag) {
        writer.write_bit(false); // mb_adaptive_frame_field_flag
    }
    writer.write_bit(true); // direct_8x8_inference_flag

    const bool has_cropping = options.crop_left != 0 || options.crop_right != 0 ||
                              options.crop_top != 0 || options.crop_bottom != 0;
    writer.write_bit(has_cropping);
    if (has_cropping) {
        writer.write_ue(options.crop_left);
        writer.write_ue(options.crop_right);
        writer.write_ue(options.crop_top);
        writer.write_ue(options.crop_bottom);
    }
    writer.write_bit(options.vui_parameters_present_flag);
    return writer.finish_rbsp();
}

struct BaselinePpsOptions {
    std::uint32_t pic_parameter_set_id{0};
    std::uint32_t seq_parameter_set_id{0};
    bool entropy_coding_mode_flag{false};
    bool bottom_field_pic_order_in_frame_present_flag{false};
    std::int32_t chroma_qp_index_offset{0};
    bool deblocking_filter_control_present_flag{true};
};

[[nodiscard]] inline ByteBuffer
make_baseline_pps_rbsp(const BaselinePpsOptions& options = {}) {
    BitWriter writer;
    writer.write_ue(options.pic_parameter_set_id);
    writer.write_ue(options.seq_parameter_set_id);
    writer.write_bit(options.entropy_coding_mode_flag);
    writer.write_bit(options.bottom_field_pic_order_in_frame_present_flag);
    writer.write_ue(0); // num_slice_groups_minus1
    writer.write_ue(0); // num_ref_idx_l0_default_active_minus1
    writer.write_ue(0); // num_ref_idx_l1_default_active_minus1
    writer.write_bit(false); // weighted_pred_flag
    writer.write_bits(0, 2); // weighted_bipred_idc
    writer.write_se(0); // pic_init_qp_minus26
    writer.write_se(0); // pic_init_qs_minus26
    writer.write_se(options.chroma_qp_index_offset);
    writer.write_bit(options.deblocking_filter_control_present_flag);
    writer.write_bit(false); // constrained_intra_pred_flag
    writer.write_bit(false); // redundant_pic_cnt_present_flag
    return writer.finish_rbsp();
}

[[nodiscard]] inline ByteBuffer rbsp_to_ebsp(ByteView rbsp) {
    ByteBuffer ebsp;
    ebsp.reserve(rbsp.size());

    std::size_t consecutive_zero_bytes = 0;
    for (const Byte value : rbsp) {
        if (consecutive_zero_bytes == 2 && value <= 0x03) {
            ebsp.push_back(0x03);
            consecutive_zero_bytes = 0;
        }
        ebsp.push_back(value);

        if (value == 0) {
            if (consecutive_zero_bytes < 2) {
                ++consecutive_zero_bytes;
            }
        } else {
            consecutive_zero_bytes = 0;
        }
    }
    return ebsp;
}

} // namespace semi_stream_probe::test
