#pragma once

#include "semi_stream_probe/core/annex_b.hpp"
#include "semi_stream_probe/core/h264_syntax.hpp"
#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/parse_error.hpp"

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

struct InspectResult {
    std::size_t input_size{0};
    std::vector<InspectedNalUnit> nal_units;
    std::vector<Sps> sequence_parameter_sets;
};

[[nodiscard]] std::expected<InspectResult, ParseError>
inspect_file(const std::filesystem::path& path, const InspectOptions& options);

[[nodiscard]] std::string render_text(const InspectResult& result,
                                      const InspectOptions& options);

} // namespace semi_stream_probe::application
