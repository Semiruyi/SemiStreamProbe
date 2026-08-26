#pragma once

#include "semi_stream_probe/application/report.hpp"
#include "semi_stream_probe/core/access_unit.hpp"
#include "semi_stream_probe/core/annex_b.hpp"
#include "semi_stream_probe/core/gop.hpp"
#include "semi_stream_probe/core/h264_syntax.hpp"
#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/slice.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace semi_stream_probe::application {

struct InspectOptions {
    bool nal_list{false};
};

struct InspectedNalUnit {
    NalUnitRef location;
    NalHeader header;
};

struct InspectedSlice {
    std::size_t nal_index{0};
    SliceHeader header;
};

struct InspectResult {
    std::size_t input_size{0};
    std::vector<InspectedNalUnit> nal_units;
    std::vector<Sps> sequence_parameter_sets;
    std::vector<Pps> picture_parameter_sets;
    std::vector<InspectedSlice> slices;
    std::vector<AccessUnit> access_units;
    GopStatistics gop_statistics;
};

[[nodiscard]] std::expected<InspectResult, ParseError>
inspect_file(const std::filesystem::path& path, const InspectOptions& options);

[[nodiscard]] AnalysisReport
make_annex_b_report(const InspectResult& result,
                    const std::filesystem::path& path,
                    const InspectOptions& options);

} // namespace semi_stream_probe::application
