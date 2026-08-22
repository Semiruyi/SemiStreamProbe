#include "semi_stream_probe/application/inspect.hpp"

#include "semi_stream_probe/core/rbsp.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace semi_stream_probe::application {

namespace {

[[nodiscard]] std::size_t rbsp_offset_to_ebsp_offset(ByteView ebsp,
                                                     std::size_t rbsp_offset) {
    std::size_t rbsp_index = 0;
    std::size_t consecutive_zero_bytes = 0;
    for (std::size_t index = 0; index < ebsp.size(); ++index) {
        const Byte value = ebsp[index];
        if (value == 0x03 && consecutive_zero_bytes == 2 &&
            index + 1 < ebsp.size() && ebsp[index + 1] <= 0x03) {
            consecutive_zero_bytes = 0;
            continue;
        }

        if (rbsp_index == rbsp_offset) {
            return index;
        }
        ++rbsp_index;

        if (value == 0x00) {
            if (consecutive_zero_bytes < 2) {
                ++consecutive_zero_bytes;
            }
        } else {
            consecutive_zero_bytes = 0;
        }
    }
    return ebsp.size();
}

[[nodiscard]] ParseError translate_sps_error(ParseError error,
                                             const NalUnitRef& location,
                                             ByteView ebsp) {
    constexpr std::size_t nal_header_size = 1;
    constexpr std::size_t bits_per_byte = 8;

    const auto rbsp_byte_offset = error.bit_offset / bits_per_byte;
    const auto bit_in_byte = error.bit_offset % bits_per_byte;
    const auto ebsp_byte_offset =
        rbsp_offset_to_ebsp_offset(ebsp, rbsp_byte_offset);
    const auto source_byte_offset =
        location.payload_offset + nal_header_size + ebsp_byte_offset;

    error.byte_offset = source_byte_offset;
    error.bit_offset = source_byte_offset * bits_per_byte + bit_in_byte;
    error.nal_index = location.index;
    return error;
}

} // namespace

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
        .sequence_parameter_sets = {},
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

        if (header->nal_unit_type == 7) {
            constexpr std::size_t nal_header_size = 1;
            const auto ebsp = payload.subspan(nal_header_size);
            auto rbsp = ebsp_to_rbsp(ebsp);
            if (!rbsp) {
                auto error = std::move(rbsp.error());
                error.byte_offset += location.payload_offset + nal_header_size;
                error.nal_index = location.index;
                return std::unexpected(std::move(error));
            }

            auto sps = parse_sps(*rbsp);
            if (!sps) {
                return std::unexpected(translate_sps_error(
                    std::move(sps.error()), location, ebsp));
            }
            result.sequence_parameter_sets.push_back(*sps);
        }
    }

    return result;
}

std::string render_text(const InspectResult& result, const InspectOptions& options) {
    std::ostringstream output;
    output << "Codec: H.264/AVC\n";
    if (!result.sequence_parameter_sets.empty()) {
        const auto& sps = result.sequence_parameter_sets.front();
        output << "Resolution: " << sps.width << 'x' << sps.height << '\n'
               << "Profile: " << h264_profile_name(sps.profile_idc) << '\n'
               << "Level: " << h264_level_name(sps) << '\n'
               << "SPS id: " << sps.seq_parameter_set_id << '\n';
    }
    output << "Input bytes: " << result.input_size << '\n'
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
