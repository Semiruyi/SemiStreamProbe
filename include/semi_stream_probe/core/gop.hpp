#pragma once

#include "semi_stream_probe/core/access_unit.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace semi_stream_probe {

// H.264 formally defines slice types, not a single type for every picture.
// Mixed preserves that distinction when one access unit contains more than
// one slice type.
enum class AccessUnitKind {
    p,
    b,
    i,
    sp,
    si,
    mixed,
};

struct AccessUnitKindCounts {
    std::size_t p{0};
    std::size_t b{0};
    std::size_t i{0};
    std::size_t sp{0};
    std::size_t si{0};
    std::size_t mixed{0};
};

struct GopStatistics {
    std::size_t access_unit_count{0};
    AccessUnitKindCounts kinds;
    std::vector<std::size_t> idr_access_unit_indices;
    std::vector<std::size_t> idr_intervals;
    std::size_t leading_non_idr_access_units{0};
    std::optional<std::size_t> minimum_idr_interval;
    std::optional<std::size_t> maximum_idr_interval;
    std::optional<double> average_idr_interval;
};

[[nodiscard]] AccessUnitKind
classify_access_unit(const AccessUnit& access_unit) noexcept;

[[nodiscard]] const char*
access_unit_kind_name(AccessUnitKind kind) noexcept;

[[nodiscard]] GopStatistics
analyze_gop(const std::vector<AccessUnit>& access_units);

} // namespace semi_stream_probe
