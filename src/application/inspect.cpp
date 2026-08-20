#include "semi_stream_probe/application/inspect.hpp"

#include "semi_stream_probe/core/types.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace semi_stream_probe::application {

std::expected<InspectResult, ParseError>
inspect_file(const std::filesystem::path& path, const InspectOptions& /*options*/) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::io_error,
            .message = "could not open input file: " + path.string(),
        });
    }

    const auto end = input.tellg();
    if (end < 0 ||
        static_cast<std::uintmax_t>(end) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max()) ||
        static_cast<std::uintmax_t>(end) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::io_error,
            .message = "could not determine input file size: " + path.string(),
        });
    }

    ByteBuffer bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::io_error,
            .message = "could not read complete input file: " + path.string(),
        });
    }

    auto locations = scan_annex_b(bytes);
    if (!locations) {
        return std::unexpected(std::move(locations.error()));
    }

    InspectResult result{
        .input_size = bytes.size(),
        .nal_units = {},
    };
    result.nal_units.reserve(locations->size());
    for (const auto& location : *locations) {
        const ByteView payload(bytes.data() + location.payload_offset,
                               location.payload_size);
        auto header = parse_nal_header(payload);
        if (!header) {
            auto error = std::move(header.error());
            error.byte_offset += location.payload_offset;
            error.nal_index = location.index;
            return std::unexpected(std::move(error));
        }
        result.nal_units.push_back(InspectedNalUnit{
            .location = location,
            .header = *header,
        });
    }

    return result;
}

std::string render_text(const InspectResult& result, const InspectOptions& options) {
    std::ostringstream output;
    output << "Codec: H.264/AVC\n"
           << "Input bytes: " << result.input_size << '\n'
           << "NAL units: " << result.nal_units.size() << '\n';

    if (!options.nal_list) {
        return output.str();
    }

    output << '\n'
           << std::left << std::setw(12) << "OFFSET" << std::setw(10) << "SIZE"
           << std::setw(16) << "TYPE" << "REF\n";
    for (const auto& unit : result.nal_units) {
        std::ostringstream offset;
        offset << "0x" << std::uppercase << std::hex << std::setw(8)
               << std::setfill('0') << unit.location.start_code_offset;
        output << std::setfill(' ') << std::left << std::setw(12) << offset.str()
               << std::setw(10) << unit.location.payload_size << std::setw(16)
               << nal_unit_type_name(unit.header.nal_unit_type)
               << static_cast<unsigned int>(unit.header.nal_ref_idc) << '\n';
    }
    return output.str();
}

} // namespace semi_stream_probe::application
