#pragma once

#include "semi_stream_probe/core/parse_error.hpp"

#include <expected>
#include <filesystem>
#include <string>

namespace semi_stream_probe::application {

struct InspectOptions {
    bool nal_list{false};
};

struct InspectResult {
    std::size_t input_size{0};
};

// Application-layer boundary. File loading and report orchestration will be
// added after the core parser is implemented.
[[nodiscard]] std::expected<InspectResult, ParseError>
inspect_file(const std::filesystem::path& path, const InspectOptions& options);

[[nodiscard]] std::string render_text(const InspectResult& result,
                                      const InspectOptions& options);

} // namespace semi_stream_probe::application
