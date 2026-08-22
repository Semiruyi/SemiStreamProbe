#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstdint>
#include <expected>
#include <string>

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

struct Pps {};

[[nodiscard]] std::expected<Sps, ParseError> parse_sps(ByteView rbsp);
[[nodiscard]] std::expected<Pps, ParseError> parse_pps(ByteView rbsp);

[[nodiscard]] const char* h264_profile_name(std::uint8_t profile_idc) noexcept;
[[nodiscard]] std::string h264_level_name(const Sps& sps);

} // namespace semi_stream_probe

