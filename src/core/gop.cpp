#include "semi_stream_probe/core/gop.hpp"

#include <algorithm>

namespace semi_stream_probe {

namespace {

void increment_kind(AccessUnitKindCounts& counts,
                    AccessUnitKind kind) noexcept {
    switch (kind) {
    case AccessUnitKind::p:
        ++counts.p;
        break;
    case AccessUnitKind::b:
        ++counts.b;
        break;
    case AccessUnitKind::i:
        ++counts.i;
        break;
    case AccessUnitKind::sp:
        ++counts.sp;
        break;
    case AccessUnitKind::si:
        ++counts.si;
        break;
    case AccessUnitKind::mixed:
        ++counts.mixed;
        break;
    }
}

} // namespace

AccessUnitKind classify_access_unit(const AccessUnit& access_unit) noexcept {
    std::size_t present_count = 0;
    std::size_t present_index = 0;
    for (std::size_t index = 0;
         index < access_unit.slice_types_present.size(); ++index) {
        if (access_unit.slice_types_present[index]) {
            ++present_count;
            present_index = index;
        }
    }

    if (present_count != 1) {
        return AccessUnitKind::mixed;
    }
    return static_cast<AccessUnitKind>(present_index);
}

const char* access_unit_kind_name(AccessUnitKind kind) noexcept {
    switch (kind) {
    case AccessUnitKind::p:
        return "P";
    case AccessUnitKind::b:
        return "B";
    case AccessUnitKind::i:
        return "I";
    case AccessUnitKind::sp:
        return "SP";
    case AccessUnitKind::si:
        return "SI";
    case AccessUnitKind::mixed:
        return "MIXED";
    }
    return "MIXED";
}

GopStatistics analyze_gop(const std::vector<AccessUnit>& access_units) {
    GopStatistics statistics;
    statistics.access_unit_count = access_units.size();

    std::optional<std::size_t> previous_idr_position;
    for (std::size_t position = 0; position < access_units.size(); ++position) {
        const auto& access_unit = access_units[position];
        increment_kind(statistics.kinds,
                       classify_access_unit(access_unit));

        if (!access_unit.first_vcl.idr) {
            continue;
        }

        statistics.idr_access_unit_indices.push_back(access_unit.index);
        if (!previous_idr_position) {
            statistics.leading_non_idr_access_units = position;
        } else {
            statistics.idr_intervals.push_back(position -
                                               *previous_idr_position);
        }
        previous_idr_position = position;
    }

    if (!previous_idr_position) {
        statistics.leading_non_idr_access_units = access_units.size();
    }

    if (!statistics.idr_intervals.empty()) {
        const auto [minimum, maximum] = std::minmax_element(
            statistics.idr_intervals.begin(),
            statistics.idr_intervals.end());
        statistics.minimum_idr_interval = *minimum;
        statistics.maximum_idr_interval = *maximum;

        double sum = 0.0;
        for (const auto interval : statistics.idr_intervals) {
            sum += static_cast<double>(interval);
        }
        statistics.average_idr_interval =
            sum /
            static_cast<double>(statistics.idr_intervals.size());
    }

    return statistics;
}

} // namespace semi_stream_probe
