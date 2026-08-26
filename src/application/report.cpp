#include "semi_stream_probe/application/report.hpp"

#include <iomanip>
#include <locale>
#include <sstream>

namespace semi_stream_probe::application {

namespace {

void write_json_string(std::ostringstream& output, std::string_view value) {
    output << '"';
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                output << "\\u" << std::uppercase << std::hex
                       << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(byte) << std::dec
                       << std::nouppercase << std::setfill(' ');
            } else {
                output << character;
            }
            break;
        }
    }
    output << '"';
}

void write_optional_string(std::ostringstream& output,
                           const std::optional<std::string>& value) {
    if (value) {
        write_json_string(output, *value);
    } else {
        output << "null";
    }
}

template <typename T>
void write_optional_number(std::ostringstream& output,
                           const std::optional<T>& value) {
    if (value) {
        output << *value;
    } else {
        output << "null";
    }
}

void write_resolution(std::ostringstream& output,
                      const std::optional<ResolutionReport>& resolution,
                      std::string_view indent) {
    if (!resolution) {
        output << "null";
        return;
    }
    output << "{\n"
           << indent << "  \"width\": " << resolution->width << ",\n"
           << indent << "  \"height\": " << resolution->height << '\n'
           << indent << '}';
}

void write_location(std::ostringstream& output,
                    const DiagnosticLocation& location,
                    std::string_view indent) {
    output << "{\n" << indent << "  \"input_byte_offset\": ";
    write_optional_number(output, location.input_byte_offset);
    output << ",\n" << indent << "  \"bit_offset\": ";
    write_optional_number(output, location.bit_offset);
    output << ",\n" << indent << "  \"nal_index\": ";
    write_optional_number(output, location.nal_index);
    output << ",\n" << indent << "  \"rtp_sequence_number\": ";
    write_optional_number(output, location.rtp_sequence_number);
    output << ",\n" << indent << "  \"ssrc\": ";
    write_optional_number(output, location.ssrc);
    output << ",\n" << indent << "  \"rtp_timestamp\": ";
    write_optional_number(output, location.rtp_timestamp);
    output << '\n' << indent << '}';
}

} // namespace

std::string_view to_string(AnalysisKind kind) noexcept {
    switch (kind) {
    case AnalysisKind::annex_b:
        return "annex_b";
    case AnalysisKind::rtp_session:
        return "rtp_session";
    }
    return "unknown";
}

std::string_view to_string(AnalysisStatus status) noexcept {
    switch (status) {
    case AnalysisStatus::complete:
        return "complete";
    case AnalysisStatus::partial:
        return "partial";
    }
    return "unknown";
}

std::string_view to_string(InputKind kind) noexcept {
    switch (kind) {
    case InputKind::file:
        return "file";
    case InputKind::udp:
        return "udp";
    }
    return "unknown";
}

DiagnosticSummary
summarize_diagnostics(std::span<const Diagnostic> diagnostics) noexcept {
    DiagnosticSummary summary;
    for (const auto& diagnostic : diagnostics) {
        switch (diagnostic.severity) {
        case DiagnosticSeverity::info:
            ++summary.info;
            break;
        case DiagnosticSeverity::warning:
            ++summary.warning;
            break;
        case DiagnosticSeverity::error:
            ++summary.error;
            break;
        }
    }
    return summary;
}

std::string render_text(const AnalysisReport& report) {
    std::ostringstream output;
    output << "Codec: " << report.h264.codec << '\n';
    if (report.h264.resolution) {
        output << "Resolution: " << report.h264.resolution->width << 'x'
               << report.h264.resolution->height << '\n';
    }
    if (report.h264.profile) {
        output << "Profile: " << *report.h264.profile << '\n';
    }
    if (report.h264.level) {
        output << "Level: " << *report.h264.level << '\n';
    }
    for (const auto& sps : report.h264.sequence_parameter_sets) {
        output << "SPS id: " << sps.id << '\n';
    }
    if (report.input.bytes_read) {
        output << "Input bytes: " << *report.input.bytes_read << '\n';
    }
    output << "NAL units: " << report.h264.nal_units << '\n'
           << "Slices: " << report.h264.slices << '\n'
           << "Access units: " << report.h264.access_units << '\n'
           << "AU slice types: I=" << report.h264.slice_types.i
           << " P=" << report.h264.slice_types.p
           << " B=" << report.h264.slice_types.b
           << " SP=" << report.h264.slice_types.sp
           << " SI=" << report.h264.slice_types.si
           << " Mixed=" << report.h264.slice_types.mixed << '\n'
           << "IDR access units: " << report.h264.idr_access_units << '\n';
    if (report.h264.leading_non_idr_access_units != 0) {
        output << "Leading non-IDR access units: "
               << report.h264.leading_non_idr_access_units << '\n';
    }
    if (report.h264.idr_interval_au) {
        output << std::fixed << std::setprecision(1)
               << "IDR interval: " << report.h264.idr_interval_au->average
               << " AU average (min " << report.h264.idr_interval_au->minimum
               << ", max " << report.h264.idr_interval_au->maximum << ")\n";
    } else {
        output << "IDR interval: n/a (need at least two IDR access units)\n";
    }
    output << "PPS: " << report.h264.pps << '\n';
    for (const auto& pps : report.h264.picture_parameter_sets) {
        output << "PPS id: " << pps.id << " (SPS "
               << pps.sequence_parameter_set_id << ")\n";
    }

    if (report.rtp) {
        output << "RTP packets received: " << report.rtp->packets_received
               << '\n'
               << "RTP packets lost: " << report.rtp->packets_lost << '\n'
               << "RTP duplicate packets: " << report.rtp->duplicate_packets
               << '\n'
               << "RTP out-of-order packets: "
               << report.rtp->out_of_order_packets << '\n';
        if (report.rtp->jitter) {
            output << "RTP jitter: " << std::fixed << std::setprecision(3)
                   << report.rtp->jitter->milliseconds << " ms\n";
        }
    }

    const auto diagnostic_summary = summarize_diagnostics(report.diagnostics);
    output << "Diagnostics: " << diagnostic_summary.error << " errors, "
           << diagnostic_summary.warning << " warnings\n";
    for (const auto& diagnostic : report.diagnostics) {
        output << '[' << to_string(diagnostic.severity) << "] "
               << to_string(diagnostic.code) << ": " << diagnostic.summary
               << '\n'
               << "Evidence: " << diagnostic.evidence << '\n';
        if (diagnostic.impact) {
            output << "Impact: " << *diagnostic.impact << '\n';
        }
        if (diagnostic.recovery) {
            output << "Recovery: " << *diagnostic.recovery << '\n';
        }
    }

    if (!report.h264.nal_list) {
        return output.str();
    }

    output << '\n'
           << std::left << std::setw(12) << "OFFSET" << std::setw(10) << "SIZE"
           << std::setw(16) << "TYPE" << std::setw(6) << "REF"
           << std::setw(6) << "AU" << std::setw(8) << "SLICE"
           << "FRAME_NUM\n";
    for (const auto& unit : *report.h264.nal_list) {
        std::ostringstream offset;
        offset << "0x" << std::uppercase << std::hex << std::setw(8)
               << std::setfill('0') << unit.byte_offset;
        output << std::setfill(' ') << std::left << std::setw(12) << offset.str()
               << std::setw(10) << unit.size << std::setw(16) << unit.type
               << std::setw(6) << static_cast<unsigned int>(unit.nal_ref_idc);
        if (unit.access_unit) {
            output << std::setw(6) << *unit.access_unit;
        } else {
            output << std::setw(6) << "-";
        }
        if (unit.slice_type) {
            output << std::setw(8) << *unit.slice_type;
        } else {
            output << std::setw(8) << "-";
        }
        if (unit.frame_num) {
            output << *unit.frame_num;
        } else {
            output << '-';
        }
        output << '\n';
    }
    return output.str();
}

std::string render_json(const AnalysisReport& report) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::setprecision(15);
    output << "{\n  \"schema_version\": ";
    write_json_string(output, report.schema_version);
    output << ",\n  \"generator\": {\n    \"name\": ";
    write_json_string(output, report.generator.name);
    output << ",\n    \"version\": ";
    write_json_string(output, report.generator.version);
    output << "\n  },\n  \"analysis\": {\n    \"kind\": ";
    write_json_string(output, to_string(report.analysis.kind));
    output << ",\n    \"status\": ";
    write_json_string(output, to_string(report.analysis.status));
    output << "\n  },\n  \"input\": {\n    \"kind\": ";
    write_json_string(output, to_string(report.input.kind));
    output << ",\n    \"source\": ";
    write_json_string(output, report.input.source);
    output << ",\n    \"bytes_read\": ";
    write_optional_number(output, report.input.bytes_read);
    output << ",\n    \"datagrams_received\": ";
    write_optional_number(output, report.input.datagrams_received);
    output << ",\n    \"duration_us\": ";
    write_optional_number(output, report.input.duration_us);
    output << "\n  },\n  \"h264\": {\n    \"codec\": ";
    write_json_string(output, report.h264.codec);
    output << ",\n    \"profile\": ";
    write_optional_string(output, report.h264.profile);
    output << ",\n    \"level\": ";
    write_optional_string(output, report.h264.level);
    output << ",\n    \"resolution\": ";
    write_resolution(output, report.h264.resolution, "    ");
    output << ",\n    \"nal_units\": " << report.h264.nal_units
           << ",\n    \"sps\": " << report.h264.sps
           << ",\n    \"pps\": " << report.h264.pps
           << ",\n    \"slices\": " << report.h264.slices
           << ",\n    \"access_units\": " << report.h264.access_units
           << ",\n    \"idr_access_units\": "
           << report.h264.idr_access_units
           << ",\n    \"leading_non_idr_access_units\": "
           << report.h264.leading_non_idr_access_units
           << ",\n    \"slice_types\": {\n"
           << "      \"i\": " << report.h264.slice_types.i << ",\n"
           << "      \"p\": " << report.h264.slice_types.p << ",\n"
           << "      \"b\": " << report.h264.slice_types.b << ",\n"
           << "      \"sp\": " << report.h264.slice_types.sp << ",\n"
           << "      \"si\": " << report.h264.slice_types.si << ",\n"
           << "      \"mixed\": " << report.h264.slice_types.mixed
           << "\n    },\n    \"idr_interval_au\": ";
    if (report.h264.idr_interval_au) {
        output << "{\n      \"average\": "
               << report.h264.idr_interval_au->average
               << ",\n      \"minimum\": "
               << report.h264.idr_interval_au->minimum
               << ",\n      \"maximum\": "
               << report.h264.idr_interval_au->maximum << "\n    }";
    } else {
        output << "null";
    }

    output << ",\n    \"sequence_parameter_sets\": [";
    for (std::size_t index = 0;
         index < report.h264.sequence_parameter_sets.size(); ++index) {
        const auto& sps = report.h264.sequence_parameter_sets[index];
        output << (index == 0 ? "\n" : ",\n")
               << "      {\n        \"id\": " << sps.id
               << ",\n        \"profile\": ";
        write_json_string(output, sps.profile);
        output << ",\n        \"level\": ";
        write_json_string(output, sps.level);
        output << ",\n        \"resolution\": {\n"
               << "          \"width\": " << sps.resolution.width << ",\n"
               << "          \"height\": " << sps.resolution.height << '\n'
               << "        }\n      }";
    }
    if (!report.h264.sequence_parameter_sets.empty()) {
        output << '\n' << "    ";
    }
    output << "],\n    \"picture_parameter_sets\": [";
    for (std::size_t index = 0;
         index < report.h264.picture_parameter_sets.size(); ++index) {
        const auto& pps = report.h264.picture_parameter_sets[index];
        output << (index == 0 ? "\n" : ",\n")
               << "      {\n        \"id\": " << pps.id
               << ",\n        \"sequence_parameter_set_id\": "
               << pps.sequence_parameter_set_id << "\n      }";
    }
    if (!report.h264.picture_parameter_sets.empty()) {
        output << '\n' << "    ";
    }
    output << "],\n    \"nal_list\": ";
    if (!report.h264.nal_list) {
        output << "null";
    } else {
        output << '[';
        for (std::size_t index = 0; index < report.h264.nal_list->size();
             ++index) {
            const auto& unit = (*report.h264.nal_list)[index];
            output << (index == 0 ? "\n" : ",\n")
                   << "      {\n        \"index\": " << unit.index
                   << ",\n        \"byte_offset\": " << unit.byte_offset
                   << ",\n        \"size\": " << unit.size
                   << ",\n        \"type\": ";
            write_json_string(output, unit.type);
            output << ",\n        \"nal_ref_idc\": "
                   << static_cast<unsigned int>(unit.nal_ref_idc)
                   << ",\n        \"access_unit\": ";
            write_optional_number(output, unit.access_unit);
            output << ",\n        \"slice_type\": ";
            write_optional_string(output, unit.slice_type);
            output << ",\n        \"frame_num\": ";
            write_optional_number(output, unit.frame_num);
            output << "\n      }";
        }
        if (!report.h264.nal_list->empty()) {
            output << '\n' << "    ";
        }
        output << ']';
    }
    output << "\n  },\n  \"rtp\": ";

    if (!report.rtp) {
        output << "null";
    } else {
        const auto& rtp = *report.rtp;
        output << "{\n    \"ssrc\": ";
        write_optional_number(output, rtp.ssrc);
        output << ",\n    \"payload_type\": "
               << static_cast<unsigned int>(rtp.payload_type)
               << ",\n    \"clock_rate_hz\": " << rtp.clock_rate_hz
               << ",\n    \"first_sequence_number\": ";
        write_optional_number(output, rtp.first_sequence_number);
        output << ",\n    \"last_sequence_number\": ";
        write_optional_number(output, rtp.last_sequence_number);
        output << ",\n    \"packets_received\": " << rtp.packets_received
               << ",\n    \"unique_packets_received\": "
               << rtp.unique_packets_received
               << ",\n    \"packets_expected\": " << rtp.packets_expected
               << ",\n    \"packets_lost\": " << rtp.packets_lost
               << ",\n    \"duplicate_packets\": " << rtp.duplicate_packets
               << ",\n    \"out_of_order_packets\": "
               << rtp.out_of_order_packets << ",\n    \"jitter\": ";
        if (rtp.jitter) {
            output << "{\n      \"timestamp_units\": "
                   << rtp.jitter->timestamp_units
                   << ",\n      \"milliseconds\": "
                   << rtp.jitter->milliseconds << "\n    }";
        } else {
            output << "null";
        }
        output << "\n  }";
    }

    const auto diagnostic_summary = summarize_diagnostics(report.diagnostics);
    output << ",\n  \"diagnostic_summary\": {\n"
           << "    \"info\": " << diagnostic_summary.info << ",\n"
           << "    \"warning\": " << diagnostic_summary.warning << ",\n"
           << "    \"error\": " << diagnostic_summary.error
           << "\n  },\n  \"diagnostics\": [";
    for (std::size_t index = 0; index < report.diagnostics.size(); ++index) {
        const auto& diagnostic = report.diagnostics[index];
        output << (index == 0 ? "\n" : ",\n")
               << "    {\n      \"severity\": ";
        write_json_string(output, to_string(diagnostic.severity));
        output << ",\n      \"code\": ";
        write_json_string(output, to_string(diagnostic.code));
        output << ",\n      \"summary\": ";
        write_json_string(output, diagnostic.summary);
        output << ",\n      \"evidence\": ";
        write_json_string(output, diagnostic.evidence);
        output << ",\n      \"impact\": ";
        write_optional_string(output, diagnostic.impact);
        output << ",\n      \"recovery\": ";
        write_optional_string(output, diagnostic.recovery);
        output << ",\n      \"location\": ";
        write_location(output, diagnostic.location, "      ");
        output << "\n    }";
    }
    if (!report.diagnostics.empty()) {
        output << '\n' << "  ";
    }
    output << "]\n}\n";
    return output.str();
}

} // namespace semi_stream_probe::application
