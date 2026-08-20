#include "semi_stream_probe/application/inspect.hpp"

namespace semi_stream_probe::application {

std::expected<InspectResult, ParseError>
inspect_file(const std::filesystem::path& /*path*/, const InspectOptions& /*options*/) {
    return std::unexpected(make_not_implemented_error("inspect application"));
}

std::string render_text(const InspectResult& /*result*/, const InspectOptions& /*options*/) {
    return "SemiStreamProbe skeleton: inspection is not implemented yet.\n";
}

} // namespace semi_stream_probe::application
