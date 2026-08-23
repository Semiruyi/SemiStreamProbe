#include "semi_stream_probe/core/gop.hpp"

#include <initializer_list>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

semi_stream_probe::AccessUnit access_unit(
    std::size_t index,
    std::initializer_list<semi_stream_probe::SliceType> slice_types,
    bool idr = false) {
    semi_stream_probe::AccessUnit unit;
    unit.index = index;
    unit.first_vcl.idr = idr;
    for (const auto type : slice_types) {
        unit.slice_types_present[static_cast<std::size_t>(type)] = true;
    }
    return unit;
}

void test_access_unit_classification() {
    const auto p = access_unit(0, {semi_stream_probe::SliceType::p});
    check(semi_stream_probe::classify_access_unit(p) ==
              semi_stream_probe::AccessUnitKind::p,
          "uniform P slices classify as P");

    const auto mixed = access_unit(1,
        {semi_stream_probe::SliceType::i, semi_stream_probe::SliceType::p});
    check(semi_stream_probe::classify_access_unit(mixed) ==
              semi_stream_probe::AccessUnitKind::mixed,
          "different slice types classify as mixed");
    check(std::string_view(semi_stream_probe::access_unit_kind_name(
              semi_stream_probe::AccessUnitKind::mixed)) == "MIXED",
          "mixed kind name");
}

void test_idr_delimited_gop_statistics() {
    const std::vector<semi_stream_probe::AccessUnit> access_units{
        access_unit(0, {semi_stream_probe::SliceType::p}),
        access_unit(1, {semi_stream_probe::SliceType::i}, true),
        access_unit(2, {semi_stream_probe::SliceType::b}),
        access_unit(3, {semi_stream_probe::SliceType::b}),
        access_unit(4, {semi_stream_probe::SliceType::p}),
        access_unit(5, {semi_stream_probe::SliceType::i}, true),
        access_unit(6,
            {semi_stream_probe::SliceType::i,
             semi_stream_probe::SliceType::p}),
        access_unit(7, {semi_stream_probe::SliceType::p}),
        access_unit(8, {semi_stream_probe::SliceType::i}, true),
    };

    const auto statistics = semi_stream_probe::analyze_gop(access_units);
    check(statistics.access_unit_count == 9, "access unit count");
    check(statistics.kinds.i == 3 && statistics.kinds.p == 3 &&
              statistics.kinds.b == 2 && statistics.kinds.mixed == 1,
          "slice type counts");
    check(statistics.idr_access_unit_indices ==
              std::vector<std::size_t>({1, 5, 8}),
          "IDR access unit indices");
    check(statistics.idr_intervals ==
              std::vector<std::size_t>({4, 3}),
          "IDR intervals use access unit positions");
    check(statistics.leading_non_idr_access_units == 1,
          "leading non-IDR segment");
    check(statistics.minimum_idr_interval == 3 &&
              statistics.maximum_idr_interval == 4 &&
              statistics.average_idr_interval == 3.5,
          "IDR interval summary");
}

void test_stream_without_idr() {
    const std::vector<semi_stream_probe::AccessUnit> access_units{
        access_unit(10, {semi_stream_probe::SliceType::p}),
        access_unit(20, {semi_stream_probe::SliceType::b}),
    };
    const auto statistics = semi_stream_probe::analyze_gop(access_units);
    check(statistics.idr_access_unit_indices.empty() &&
              statistics.idr_intervals.empty() &&
              !statistics.average_idr_interval &&
              statistics.leading_non_idr_access_units == 2,
          "stream without IDR has no measurable interval");
}

} // namespace

int main() {
    test_access_unit_classification();
    test_idr_delimited_gop_statistics();
    test_stream_without_idr();

    if (failures != 0) {
        std::cerr << failures << " GOP test(s) failed\n";
        return 1;
    }
    std::cout << "semi_stream_probe_gop_tests: all tests passed\n";
    return 0;
}
