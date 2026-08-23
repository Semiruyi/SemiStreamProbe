#include "semi_stream_probe/core/access_unit.hpp"

#include <utility>

namespace semi_stream_probe {

namespace {

[[nodiscard]] bool is_supported_vcl(std::uint8_t nal_unit_type) noexcept {
    return nal_unit_type == 1 || nal_unit_type == 5;
}

[[nodiscard]] constexpr std::size_t slice_type_index(SliceType type) noexcept {
    return static_cast<std::size_t>(type);
}

// These NAL units precede the primary coded picture of an access unit. When
// one appears after VCL data, the current access unit is complete and the NAL
// is retained for the next picture.
[[nodiscard]] bool starts_access_unit_prefix(
    std::uint8_t nal_unit_type) noexcept {
    return nal_unit_type == 6 || nal_unit_type == 7 ||
           nal_unit_type == 8 || nal_unit_type == 9 ||
           (nal_unit_type >= 14 && nal_unit_type <= 18);
}

} // namespace

bool starts_new_primary_coded_picture(
    const SliceHeader& first_vcl,
    const SliceHeader& candidate) noexcept {
    if (first_vcl.frame_num != candidate.frame_num ||
        first_vcl.pic_parameter_set_id != candidate.pic_parameter_set_id ||
        first_vcl.field_pic_flag != candidate.field_pic_flag) {
        return true;
    }

    if (first_vcl.field_pic_flag && candidate.field_pic_flag &&
        first_vcl.bottom_field_flag != candidate.bottom_field_flag) {
        return true;
    }

    const bool first_is_reference = first_vcl.nal_ref_idc != 0;
    const bool candidate_is_reference = candidate.nal_ref_idc != 0;
    if (first_is_reference != candidate_is_reference) {
        return true;
    }

    if (first_vcl.pic_order_cnt_lsb != candidate.pic_order_cnt_lsb ||
        first_vcl.delta_pic_order_bottom !=
            candidate.delta_pic_order_bottom ||
        first_vcl.delta_pic_order_cnt0 != candidate.delta_pic_order_cnt0 ||
        first_vcl.delta_pic_order_cnt1 != candidate.delta_pic_order_cnt1) {
        return true;
    }

    if (first_vcl.idr != candidate.idr) {
        return true;
    }
    return first_vcl.idr && candidate.idr &&
           first_vcl.idr_pic_id != candidate.idr_pic_id;
}

std::optional<AccessUnit>
AccessUnitAssembler::push(std::size_t nal_index,
                          const NalHeader& nal_header,
                          const SliceHeader* slice_header) {
    if (is_supported_vcl(nal_header.nal_unit_type) && slice_header != nullptr) {
        if (!current_) {
            begin_picture(nal_index, *slice_header);
            return std::nullopt;
        }

        if (starts_new_primary_coded_picture(current_->first_vcl,
                                             *slice_header)) {
            auto completed = complete_current();
            begin_picture(nal_index, *slice_header);
            return completed;
        }

        current_->nal_indices.push_back(nal_index);
        current_->vcl_nal_indices.push_back(nal_index);
        current_->slice_types_present[slice_type_index(
            slice_header->slice_type)] = true;
        return std::nullopt;
    }

    if (starts_access_unit_prefix(nal_header.nal_unit_type)) {
        auto completed = complete_current();
        pending_prefix_nals_.push_back(nal_index);
        return completed;
    }

    if (current_) {
        current_->nal_indices.push_back(nal_index);
    } else {
        pending_prefix_nals_.push_back(nal_index);
    }
    return std::nullopt;
}

std::optional<AccessUnit> AccessUnitAssembler::finish() {
    return complete_current();
}

std::optional<AccessUnit> AccessUnitAssembler::complete_current() {
    if (!current_) {
        return std::nullopt;
    }
    auto completed = std::move(current_);
    current_.reset();
    return completed;
}

void AccessUnitAssembler::begin_picture(
    std::size_t nal_index,
    const SliceHeader& slice_header) {
    AccessUnit access_unit{
        .index = next_index_,
        .nal_indices = std::move(pending_prefix_nals_),
        .vcl_nal_indices = {},
        .slice_types_present = {},
        .first_vcl = slice_header,
    };
    ++next_index_;
    pending_prefix_nals_.clear();
    access_unit.nal_indices.push_back(nal_index);
    access_unit.vcl_nal_indices.push_back(nal_index);
    access_unit.slice_types_present[slice_type_index(
        slice_header.slice_type)] = true;
    current_ = std::move(access_unit);
}

} // namespace semi_stream_probe
