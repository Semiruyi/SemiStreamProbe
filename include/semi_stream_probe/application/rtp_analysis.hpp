#pragma once

#include "semi_stream_probe/application/report.hpp"
#include "semi_stream_probe/core/h264_rtp_stream_analyzer.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace semi_stream_probe::application {

// The analyzer must be finished before building the final report so its last
// Access Unit and any unfinished FU-A are reflected in the result.
[[nodiscard]] AnalysisReport make_rtp_analysis_report(
    const H264RtpStreamAnalyzer& analyzer,
    std::string source,
    std::optional<std::uint64_t> duration_us = std::nullopt);

} // namespace semi_stream_probe::application
