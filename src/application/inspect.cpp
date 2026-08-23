#include "semi_stream_probe/application/inspect.hpp"

#include "semi_stream_probe/core/bit_reader.hpp"
#include "semi_stream_probe/core/parameter_sets.hpp"
#include "semi_stream_probe/core/rbsp.hpp"
#include "semi_stream_probe/core/slice.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace semi_stream_probe::application {

namespace {

struct ParsedSpsUnit {
    std::size_t nal_index{0};
    Sps syntax;
};

struct ParsedPpsUnit {
    std::size_t nal_index{0};
    Pps syntax;
};

[[nodiscard]] const Sps* find_sps_context(
    const std::vector<ParsedSpsUnit>& sequence_parameter_sets,
    std::size_t pps_nal_index,
    std::uint32_t sps_id) noexcept {
    const Sps* latest_preceding = nullptr;
    const Sps* earliest_following = nullptr;
    for (const auto& event : sequence_parameter_sets) {
        if (event.syntax.seq_parameter_set_id != sps_id) {
            continue;
        }
        if (event.nal_index <= pps_nal_index) {
            latest_preceding = &event.syntax;
        } else if (earliest_following == nullptr) {
            earliest_following = &event.syntax;
        }
    }
    return latest_preceding != nullptr ? latest_preceding : earliest_following;
}

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

[[nodiscard]] ParseError translate_rbsp_error(ParseError error,
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
        .picture_parameter_sets = {},
        .slices = {},
        .access_units = {},
        .gop_statistics = {},
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

    std::vector<ParsedSpsUnit> parsed_sps_units;
    for (const auto& unit : result.nal_units) {
        if (unit.header.nal_unit_type != 7) {
            continue;
        }
        constexpr std::size_t nal_header_size = 1;
        const auto& location = unit.location;
        const ByteView payload(bytes.data() + location.payload_offset,
                               location.payload_size);
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
            return std::unexpected(translate_rbsp_error(
                std::move(sps.error()), location, ebsp));
        }
        result.sequence_parameter_sets.push_back(*sps);
        parsed_sps_units.push_back(ParsedSpsUnit{
            .nal_index = location.index,
            .syntax = *sps,
        });
    }

    // PPS extension syntax depends on the referenced SPS chroma format. Prefer
    // the latest SPS active before this PPS; a following SPS is only a parsing
    // fallback for out-of-order parameter-set delivery. Activation still occurs
    // later in original NAL order.
    std::vector<ParsedPpsUnit> parsed_pps_units;
    for (const auto& unit : result.nal_units) {
        if (unit.header.nal_unit_type != 8) {
            continue;
        }
        constexpr std::size_t nal_header_size = 1;
        const auto& location = unit.location;
        const ByteView payload(bytes.data() + location.payload_offset,
                               location.payload_size);
        const auto ebsp = payload.subspan(nal_header_size);
        auto rbsp = ebsp_to_rbsp(ebsp);
        if (!rbsp) {
            auto error = std::move(rbsp.error());
            error.byte_offset += location.payload_offset + nal_header_size;
            error.nal_index = location.index;
            return std::unexpected(std::move(error));
        }

        std::uint32_t chroma_format_idc = 1;
        BitReader id_reader(*rbsp);
        auto ignored_pps_id = id_reader.read_ue();
        if (!ignored_pps_id) {
            auto error = std::move(ignored_pps_id.error());
            error.message = "could not read pic_parameter_set_id: " +
                            error.message;
            return std::unexpected(translate_rbsp_error(
                std::move(error), location, ebsp));
        }
        auto referenced_sps_id = id_reader.read_ue();
        if (!referenced_sps_id) {
            auto error = std::move(referenced_sps_id.error());
            error.message = "could not read PPS seq_parameter_set_id: " +
                            error.message;
            return std::unexpected(translate_rbsp_error(
                std::move(error), location, ebsp));
        }
        const auto* referenced_sps = find_sps_context(
            parsed_sps_units, location.index, *referenced_sps_id);
        if (referenced_sps == nullptr) {
            return std::unexpected(translate_rbsp_error(ParseError{
                .code = ParseErrorCode::parameter_set_not_found,
                .byte_offset = id_reader.bit_position() / 8,
                .bit_offset = id_reader.bit_position(),
                .message = "PPS references missing SPS " +
                           std::to_string(*referenced_sps_id),
            }, location, ebsp));
        }
        chroma_format_idc = referenced_sps->chroma_format_idc;

        auto pps = parse_pps(*rbsp, chroma_format_idc);
        if (!pps) {
            return std::unexpected(translate_rbsp_error(
                std::move(pps.error()), location, ebsp));
        }
        result.picture_parameter_sets.push_back(*pps);
        parsed_pps_units.push_back(ParsedPpsUnit{
            .nal_index = location.index,
            .syntax = *pps,
        });
    }

    // Replay parameter-set definitions in decoding order. A Slice now sees the
    // versions active at its own NAL position instead of the final definitions
    // found at end of file.
    ParameterSetRegistry active_parameter_sets;
    std::size_t next_sps_unit = 0;
    std::size_t next_pps_unit = 0;
    for (const auto& unit : result.nal_units) {
        if (unit.header.nal_unit_type == 7) {
            active_parameter_sets.store(
                parsed_sps_units[next_sps_unit].syntax);
            ++next_sps_unit;
            continue;
        }
        if (unit.header.nal_unit_type == 8) {
            active_parameter_sets.store(
                parsed_pps_units[next_pps_unit].syntax);
            ++next_pps_unit;
            continue;
        }
        if (unit.header.nal_unit_type != 1 && unit.header.nal_unit_type != 5) {
            continue;
        }
        constexpr std::size_t nal_header_size = 1;
        const auto& location = unit.location;
        const ByteView payload(bytes.data() + location.payload_offset,
                               location.payload_size);
        const auto ebsp = payload.subspan(nal_header_size);
        auto rbsp = ebsp_to_rbsp(ebsp);
        if (!rbsp) {
            auto error = std::move(rbsp.error());
            error.byte_offset += location.payload_offset + nal_header_size;
            error.nal_index = location.index;
            return std::unexpected(std::move(error));
        }
        auto slice = parse_slice_header(*rbsp,
                                        unit.header,
                                        active_parameter_sets);
        if (!slice) {
            return std::unexpected(translate_rbsp_error(
                std::move(slice.error()), location, ebsp));
        }
        result.slices.push_back(InspectedSlice{
            .nal_index = location.index,
            .header = *slice,
        });
    }

    AccessUnitAssembler assembler;
    std::size_t next_slice = 0;
    for (const auto& unit : result.nal_units) {
        const SliceHeader* slice_header = nullptr;
        if (next_slice < result.slices.size() &&
            result.slices[next_slice].nal_index == unit.location.index) {
            slice_header = &result.slices[next_slice].header;
            ++next_slice;
        }

        auto completed = assembler.push(unit.location.index,
                                        unit.header,
                                        slice_header);
        if (completed) {
            result.access_units.push_back(std::move(*completed));
        }
    }
    if (auto completed = assembler.finish()) {
        result.access_units.push_back(std::move(*completed));
    }
    result.gop_statistics = analyze_gop(result.access_units);

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
           << "NAL units: " << result.nal_units.size() << '\n'
           << "Slices: " << result.slices.size() << '\n'
           << "Access units: " << result.access_units.size() << '\n'
           << "AU slice types: I=" << result.gop_statistics.kinds.i
           << " P=" << result.gop_statistics.kinds.p
           << " B=" << result.gop_statistics.kinds.b
           << " SP=" << result.gop_statistics.kinds.sp
           << " SI=" << result.gop_statistics.kinds.si
           << " Mixed=" << result.gop_statistics.kinds.mixed << '\n'
           << "IDR access units: "
           << result.gop_statistics.idr_access_unit_indices.size() << '\n';
    if (result.gop_statistics.average_idr_interval) {
        output << std::fixed << std::setprecision(1)
               << "IDR interval: "
               << *result.gop_statistics.average_idr_interval
               << " AU average (min "
               << *result.gop_statistics.minimum_idr_interval
               << ", max "
               << *result.gop_statistics.maximum_idr_interval << ")\n";
    } else {
        output << "IDR interval: n/a (need at least two IDR access units)\n";
    }
    if (result.gop_statistics.leading_non_idr_access_units != 0) {
        output << "Leading non-IDR access units: "
               << result.gop_statistics.leading_non_idr_access_units << '\n';
    }
    output << "PPS: " << result.picture_parameter_sets.size() << '\n';
    for (const auto& pps : result.picture_parameter_sets) {
        output << "PPS id: " << pps.pic_parameter_set_id
               << " (SPS " << pps.seq_parameter_set_id << ")\n";
    }

    if (!options.nal_list) {
        return output.str();
    }

    std::vector<const AccessUnit*> access_unit_by_nal(result.nal_units.size(),
                                                      nullptr);
    for (const auto& access_unit : result.access_units) {
        for (const auto nal_index : access_unit.nal_indices) {
            if (nal_index < access_unit_by_nal.size()) {
                access_unit_by_nal[nal_index] = &access_unit;
            }
        }
    }
    std::vector<const InspectedSlice*> slice_by_nal(result.nal_units.size(),
                                                    nullptr);
    for (const auto& slice : result.slices) {
        if (slice.nal_index < slice_by_nal.size()) {
            slice_by_nal[slice.nal_index] = &slice;
        }
    }

    output << '\n'
           << std::left << std::setw(12) << "OFFSET" << std::setw(10) << "SIZE"
           << std::setw(16) << "TYPE" << std::setw(6) << "REF"
           << std::setw(6) << "AU" << std::setw(8) << "SLICE"
           << "FRAME_NUM\n";
    for (const auto& unit : result.nal_units) {
        std::ostringstream offset;
        offset << "0x" << std::uppercase << std::hex << std::setw(8)
               << std::setfill('0') << unit.location.start_code_offset;
        output << std::setfill(' ') << std::left << std::setw(12) << offset.str()
               << std::setw(10) << unit.location.payload_size << std::setw(16)
               << nal_unit_type_name(unit.header.nal_unit_type)
               << std::setw(6)
               << static_cast<unsigned int>(unit.header.nal_ref_idc);
        const auto* access_unit =
            unit.location.index < access_unit_by_nal.size()
                ? access_unit_by_nal[unit.location.index]
                : nullptr;
        if (access_unit == nullptr) {
            output << std::setw(6) << "-";
        } else {
            output << std::setw(6) << access_unit->index;
        }
        const auto* inspected_slice = unit.location.index < slice_by_nal.size()
                                          ? slice_by_nal[unit.location.index]
                                          : nullptr;
        if (inspected_slice == nullptr) {
            output << std::setw(8) << "-" << "-\n";
        } else {
            output << std::setw(8)
                   << slice_type_name(inspected_slice->header.slice_type)
                   << inspected_slice->header.frame_num << '\n';
        }
    }
    return output.str();
}

} // namespace semi_stream_probe::application
