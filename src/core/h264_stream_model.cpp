#include "semi_stream_probe/core/h264_stream_model.hpp"

#include "semi_stream_probe/core/bit_reader.hpp"
#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/rbsp.hpp"
#include "semi_stream_probe/core/slice.hpp"

#include <optional>
#include <string>
#include <utility>

namespace semi_stream_probe {

void H264StreamModel::push(ByteView nal_unit, H264NalSourceContext source) {
    if (finished_) {
        return;
    }

    const auto nal_index =
        static_cast<std::size_t>(statistics_.nal_units++);
    const auto header = parse_nal_header(nal_unit);
    if (!header) {
        record_error(header.error(), DiagnosticCode::h264_syntax_invalid,
                     "H.264 NAL header is invalid", nal_index, source);
        return;
    }

    constexpr std::size_t nal_header_size = 1;
    const auto ebsp = nal_unit.subspan(nal_header_size);
    if (header->nal_unit_type == 7) {
        const auto rbsp = ebsp_to_rbsp(ebsp);
        if (!rbsp) {
            record_error(rbsp.error(), DiagnosticCode::h264_syntax_invalid,
                         "H.264 SPS EBSP is invalid", nal_index, source);
            accept_non_vcl(nal_index, *header);
            return;
        }
        auto sps = parse_sps(*rbsp);
        if (!sps) {
            record_error(sps.error(), DiagnosticCode::h264_syntax_invalid,
                         "H.264 SPS syntax is invalid", nal_index, source);
            accept_non_vcl(nal_index, *header);
            return;
        }
        statistics_.sequence_parameter_sets.push_back(*sps);
        parameter_sets_.store(std::move(*sps));
        accept_non_vcl(nal_index, *header);
        return;
    }

    if (header->nal_unit_type == 8) {
        const auto rbsp = ebsp_to_rbsp(ebsp);
        if (!rbsp) {
            record_error(rbsp.error(), DiagnosticCode::h264_syntax_invalid,
                         "H.264 PPS EBSP is invalid", nal_index, source);
            accept_non_vcl(nal_index, *header);
            return;
        }

        BitReader id_reader(*rbsp);
        const auto pps_id = id_reader.read_ue();
        const auto sps_id = id_reader.read_ue();
        if (!pps_id || !sps_id) {
            const auto& error = !pps_id ? pps_id.error() : sps_id.error();
            record_error(error, DiagnosticCode::h264_syntax_invalid,
                         "H.264 PPS identifiers are invalid", nal_index,
                         source);
            accept_non_vcl(nal_index, *header);
            return;
        }

        const auto* referenced_sps = parameter_sets_.find_sps(*sps_id);
        if (referenced_sps == nullptr) {
            record_error(
                ParseError{
                    .code = ParseErrorCode::parameter_set_not_found,
                    .message = "PPS " + std::to_string(*pps_id) +
                               " references missing SPS " +
                               std::to_string(*sps_id),
                },
                DiagnosticCode::h264_parameter_set_not_found,
                "H.264 PPS references an unavailable SPS", nal_index, source);
            accept_non_vcl(nal_index, *header);
            return;
        }

        auto pps = parse_pps(*rbsp, referenced_sps->chroma_format_idc);
        if (!pps) {
            record_error(pps.error(), DiagnosticCode::h264_syntax_invalid,
                         "H.264 PPS syntax is invalid", nal_index, source);
            accept_non_vcl(nal_index, *header);
            return;
        }
        statistics_.picture_parameter_sets.push_back(*pps);
        parameter_sets_.store(std::move(*pps));
        accept_non_vcl(nal_index, *header);
        return;
    }

    if (header->nal_unit_type == 1 || header->nal_unit_type == 5) {
        const auto rbsp = ebsp_to_rbsp(ebsp);
        if (!rbsp) {
            record_error(rbsp.error(), DiagnosticCode::h264_syntax_invalid,
                         "H.264 Slice EBSP is invalid", nal_index, source);
            return;
        }
        auto slice = parse_slice_header(*rbsp, *header, parameter_sets_);
        if (!slice) {
            const auto code =
                slice.error().code == ParseErrorCode::parameter_set_not_found
                    ? DiagnosticCode::h264_parameter_set_not_found
                    : DiagnosticCode::h264_syntax_invalid;
            record_error(slice.error(), code,
                         code == DiagnosticCode::h264_parameter_set_not_found
                             ? "H.264 Slice references unavailable parameter sets"
                             : "H.264 Slice syntax is invalid",
                         nal_index, source);
            return;
        }
        ++statistics_.slices;
        if (auto completed = access_unit_assembler_.push(
                nal_index, *header, &*slice)) {
            statistics_.access_units.push_back(std::move(*completed));
        }
        return;
    }

    accept_non_vcl(nal_index, *header);
}

void H264StreamModel::finish() {
    if (finished_) {
        return;
    }
    if (auto completed = access_unit_assembler_.finish()) {
        statistics_.access_units.push_back(std::move(*completed));
    }
    statistics_.gop = analyze_gop(statistics_.access_units);
    finished_ = true;
}

const H264StreamModelStatistics&
H264StreamModel::statistics() const noexcept {
    return statistics_;
}

std::span<const Diagnostic> H264StreamModel::diagnostics() const noexcept {
    return diagnostics_;
}

void H264StreamModel::record_error(
    const ParseError& error,
    DiagnosticCode code,
    std::string summary,
    std::size_t nal_index,
    const H264NalSourceContext& source) {
    diagnostics_.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = code,
        .summary = std::move(summary),
        .evidence = error.message,
        .impact = "the NAL unit was excluded from the H.264 stream model",
        .recovery = code == DiagnosticCode::h264_parameter_set_not_found
                        ? std::optional<std::string>{
                              "analysis can resume after the required SPS/PPS is received"}
                        : std::optional<std::string>{
                              "analysis continues with the next complete NAL unit"},
        .location = {
            .input_byte_offset = source.input_byte_offset,
            .bit_offset = std::nullopt,
            .nal_index = nal_index,
            .rtp_sequence_number = source.rtp_sequence_number,
            .ssrc = source.ssrc,
            .rtp_timestamp = source.rtp_timestamp,
        },
    });
}

void H264StreamModel::accept_non_vcl(std::size_t nal_index,
                                     const NalHeader& header) {
    if (auto completed =
            access_unit_assembler_.push(nal_index, header, nullptr)) {
        statistics_.access_units.push_back(std::move(*completed));
    }
}

} // namespace semi_stream_probe
