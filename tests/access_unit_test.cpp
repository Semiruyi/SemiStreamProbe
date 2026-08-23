#include "semi_stream_probe/core/access_unit.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

semi_stream_probe::NalHeader nal(std::uint8_t type,
                                 std::uint8_t reference_idc = 0) {
    return semi_stream_probe::NalHeader{
        .nal_ref_idc = reference_idc,
        .nal_unit_type = type,
    };
}

semi_stream_probe::SliceHeader picture(std::uint32_t frame_num,
                                       std::uint32_t poc_lsb,
                                       std::uint8_t reference_idc = 2) {
    semi_stream_probe::SliceHeader header;
    header.nal_ref_idc = reference_idc;
    header.pic_parameter_set_id = 0;
    header.frame_num = frame_num;
    header.pic_order_cnt_lsb = poc_lsb;
    header.slice_type = semi_stream_probe::SliceType::p;
    return header;
}

void test_picture_boundary_conditions() {
    const auto first = picture(4, 8);
    auto candidate = first;
    candidate.first_mb_in_slice = 120;
    candidate.slice_type = semi_stream_probe::SliceType::b;
    candidate.nal_ref_idc = 3;
    check(!semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "macroblock address, slice type, and non-zero ref priority do not split");

    candidate = first;
    candidate.frame_num = 5;
    check(semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "frame_num change starts a picture");

    candidate = first;
    candidate.pic_parameter_set_id = 1;
    check(semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "PPS change starts a picture");

    candidate = first;
    candidate.nal_ref_idc = 0;
    check(semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "reference status change starts a picture");

    candidate = first;
    candidate.pic_order_cnt_lsb = 10;
    check(semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "POC change starts a picture");

    candidate = first;
    candidate.field_pic_flag = true;
    check(semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "frame and field pictures are distinct");

    auto top_field = first;
    top_field.field_pic_flag = true;
    auto bottom_field = top_field;
    bottom_field.bottom_field_flag = true;
    check(semi_stream_probe::starts_new_primary_coded_picture(top_field,
                                                              bottom_field),
          "top and bottom fields are distinct pictures");

    auto idr = first;
    idr.idr = true;
    idr.idr_pic_id = 0;
    candidate = idr;
    candidate.idr_pic_id = 1;
    check(semi_stream_probe::starts_new_primary_coded_picture(idr, candidate),
          "different IDR picture ids split access units");

    candidate = first;
    candidate.colour_plane_id = 2;
    check(!semi_stream_probe::starts_new_primary_coded_picture(first, candidate),
          "separate colour planes remain in one coded picture");
}

void test_multiple_slices_and_new_picture() {
    semi_stream_probe::AccessUnitAssembler assembler;
    check(!assembler.push(0, nal(7)), "SPS is pending before a picture");
    check(!assembler.push(1, nal(8)), "PPS is pending before a picture");

    auto first = picture(4, 8);
    check(!assembler.push(2, nal(1, 2), &first),
          "first VCL starts an access unit");
    auto second_slice = first;
    second_slice.first_mb_in_slice = 120;
    second_slice.slice_type = semi_stream_probe::SliceType::b;
    check(!assembler.push(3, nal(1, 2), &second_slice),
          "second slice stays in the same access unit");

    auto next_picture = picture(5, 10);
    auto completed = assembler.push(4, nal(1, 2), &next_picture);
    check(completed.has_value(), "new picture completes previous access unit");
    if (completed) {
        check(completed->index == 0, "first access unit index");
        check(completed->nal_indices ==
                  std::vector<std::size_t>({0, 1, 2, 3}),
              "parameter sets and both slices are grouped");
        check(completed->vcl_nal_indices ==
                  std::vector<std::size_t>({2, 3}),
              "VCL indices preserve both slices");
        check(completed->slice_types_present[
                  static_cast<std::size_t>(semi_stream_probe::SliceType::p)] &&
                  completed->slice_types_present[
                  static_cast<std::size_t>(semi_stream_probe::SliceType::b)],
              "all slice types in an access unit are retained");
    }

    auto final = assembler.finish();
    check(final && final->index == 1 &&
              final->nal_indices == std::vector<std::size_t>({4}),
          "finish emits the last access unit");
    check(!assembler.finish(), "finish is idempotent after draining");
}

void test_prefix_nals_and_trailing_nals() {
    semi_stream_probe::AccessUnitAssembler assembler;
    auto first = picture(0, 0);
    check(!assembler.push(0, nal(1, 2), &first),
          "picture starts without AUD");

    auto completed = assembler.push(1, nal(6));
    check(completed && completed->nal_indices ==
                           std::vector<std::size_t>({0}),
          "SEI after VCL begins the next access unit prefix");
    check(!assembler.push(2, nal(9)), "AUD remains pending for next picture");
    check(!assembler.push(3, nal(7)), "SPS remains pending for next picture");

    auto second = picture(1, 2);
    check(!assembler.push(4, nal(1, 2), &second),
          "pending prefix NALs attach when the next VCL arrives");
    check(!assembler.push(5, nal(12)), "filler follows the current picture");

    auto final = assembler.finish();
    check(final && final->nal_indices ==
                       std::vector<std::size_t>({1, 2, 3, 4, 5}),
          "prefix and trailing NALs are retained in order");

    semi_stream_probe::AccessUnitAssembler no_picture;
    check(!no_picture.push(0, nal(7)) && !no_picture.finish(),
          "parameter sets alone do not produce a picture access unit");
}

} // namespace

int main() {
    test_picture_boundary_conditions();
    test_multiple_slices_and_new_picture();
    test_prefix_nals_and_trailing_nals();

    if (failures != 0) {
        std::cerr << failures << " access unit test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_access_unit_tests: all tests passed\n";
    return 0;
}
