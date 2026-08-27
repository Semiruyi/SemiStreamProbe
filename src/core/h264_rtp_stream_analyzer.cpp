#include "semi_stream_probe/core/h264_rtp_stream_analyzer.hpp"

#include <optional>
#include <string>
#include <utility>

namespace semi_stream_probe {

namespace {

[[nodiscard]] DiagnosticLocation location_for(const RtpPacket& packet) {
    return DiagnosticLocation{
        .input_byte_offset = std::nullopt,
        .bit_offset = std::nullopt,
        .nal_index = std::nullopt,
        .rtp_sequence_number = packet.sequence_number,
        .ssrc = packet.ssrc,
        .rtp_timestamp = packet.timestamp,
    };
}

[[nodiscard]] DiagnosticLocation location_for(
    const H264FuAReassemblyContext& context,
    std::optional<std::uint16_t> sequence_number) {
    return DiagnosticLocation{
        .input_byte_offset = std::nullopt,
        .bit_offset = std::nullopt,
        .nal_index = std::nullopt,
        .rtp_sequence_number = sequence_number,
        .ssrc = context.ssrc,
        .rtp_timestamp = context.timestamp,
    };
}

} // namespace

H264RtpStreamAnalyzer::H264RtpStreamAnalyzer(
    H264RtpStreamAnalyzerConfig config)
    : rtp_analyzer_(config.rtp),
      fu_a_reassembler_(config.max_nal_unit_size) {}

std::vector<DepacketizedH264NalUnit> H264RtpStreamAnalyzer::push(
    ByteView datagram,
    std::chrono::microseconds arrival_time) {
    std::vector<DepacketizedH264NalUnit> output;
    auto rtp_result = rtp_analyzer_.push(datagram, arrival_time);
    copy_new_rtp_diagnostics();
    if (rtp_result.disposition != RtpPacketDisposition::accepted ||
        !rtp_result.packet) {
        return output;
    }

    RtpPacket packet = std::move(*rtp_result.packet);
    bool gap_discarded_active_fu = false;
    if (const auto context = fu_a_reassembler_.context();
        context && packet.sequence_number != context->expected_sequence_number) {
        record_fu_failure(
            DiagnosticCode::h264_fu_a_sequence_gap,
            "FU-A sequence is discontinuous",
            "the active FU-A expected RTP sequence " +
                std::to_string(context->expected_sequence_number) +
                " and received " + std::to_string(packet.sequence_number),
            "the incomplete NAL was discarded; a later FU-A start can begin a new NAL",
            *context, packet.sequence_number);
        fu_a_reassembler_.reset();
        gap_discarded_active_fu = true;
    }

    const auto payload_header = parse_h264_rtp_payload_header(packet.payload);
    if (!payload_header) {
        if (const auto context = fu_a_reassembler_.context()) {
            record_fu_failure(
                DiagnosticCode::h264_fu_a_interrupted,
                "FU-A reassembly was interrupted by an invalid payload",
                payload_header.error().message,
                "the incomplete NAL was discarded; a later FU-A start can begin a new NAL",
                *context, packet.sequence_number);
            fu_a_reassembler_.reset();
        }
        record_invalid_payload(packet,
                               DiagnosticCode::h264_rtp_invalid_payload,
                               payload_header.error().message);
        return output;
    }

    if (payload_header->kind != H264RtpPayloadKind::fu_a) {
        if (const auto context = fu_a_reassembler_.context()) {
            record_fu_failure(
                DiagnosticCode::h264_fu_a_interrupted,
                "FU-A reassembly was interrupted by another payload",
                std::string("received ") +
                    h264_rtp_payload_kind_name(payload_header->kind) +
                    " before the active FU-A completed",
                "the incomplete NAL was discarded; analysis continues with the current payload",
                *context, packet.sequence_number);
            fu_a_reassembler_.reset();
        }
    }

    switch (payload_header->kind) {
    case H264RtpPayloadKind::single_nal_unit: {
        ++statistics_.single_nal_packets;
        const auto nal = depacketize_h264_single_nal(packet);
        if (!nal) {
            record_invalid_payload(packet,
                                   DiagnosticCode::h264_rtp_invalid_payload,
                                   nal.error().message);
            return output;
        }
        append_complete_nal(output, nal->header, nal->bytes, packet,
                            packet.marker);
        return output;
    }
    case H264RtpPayloadKind::stap_a: {
        ++statistics_.stap_a_packets;
        const auto stap = depacketize_h264_stap_a(packet);
        if (!stap) {
            record_invalid_payload(packet, DiagnosticCode::h264_stap_a_invalid,
                                   stap.error().message);
            return output;
        }
        for (std::size_t index = 0; index < stap->nal_units.size(); ++index) {
            const auto& nal = stap->nal_units[index];
            const bool marker = packet.marker &&
                                index + 1U == stap->nal_units.size();
            append_complete_nal(output, nal.header, nal.bytes, packet, marker);
        }
        return output;
    }
    case H264RtpPayloadKind::fu_a: {
        ++statistics_.fu_a_packets;
        const auto fragment = parse_h264_fu_a_fragment(packet);
        if (!fragment) {
            if (const auto context = fu_a_reassembler_.context()) {
                record_fu_failure(
                    DiagnosticCode::h264_fu_a_interrupted,
                    "FU-A reassembly was interrupted by an invalid fragment",
                    fragment.error().message,
                    "the incomplete NAL was discarded; a later FU-A start can begin a new NAL",
                    *context, packet.sequence_number);
                fu_a_reassembler_.reset();
            }
            record_invalid_payload(packet,
                                   DiagnosticCode::h264_rtp_invalid_payload,
                                   fragment.error().message);
            return output;
        }

        if (gap_discarded_active_fu && !fragment->start) {
            return output;
        }

        if (fragment->start) {
            if (const auto context = fu_a_reassembler_.context()) {
                record_fu_failure(
                    DiagnosticCode::h264_fu_a_interrupted,
                    "FU-A reassembly was interrupted by a new start fragment",
                    "RTP sequence " + std::to_string(packet.sequence_number) +
                        " started a new FU-A before the previous NAL completed",
                    "the previous NAL was discarded and the new FU-A became active",
                    *context, packet.sequence_number);
                fu_a_reassembler_.reset();
            }
        } else if (!fu_a_reassembler_.in_progress()) {
            diagnostics_.push_back(Diagnostic{
                .severity = DiagnosticSeverity::error,
                .code = DiagnosticCode::h264_fu_a_missing_start,
                .summary = "FU-A continuation arrived without a start fragment",
                .evidence = "RTP sequence " +
                            std::to_string(packet.sequence_number) +
                            " carries a FU-A continuation without active state",
                .impact = "the fragment cannot be associated with a complete NAL unit",
                .recovery = "a later FU-A start can begin a new NAL",
                .location = location_for(packet),
            });
            return output;
        }

        const auto context = fu_a_reassembler_.context();
        if (context &&
            (packet.timestamp != context->timestamp ||
             packet.ssrc != context->ssrc ||
             packet.payload_type != context->payload_type ||
             fragment->indicator.nal_ref_idc != context->header.nal_ref_idc ||
             fragment->nal_unit_type != context->header.nal_unit_type)) {
            record_fu_failure(
                DiagnosticCode::h264_fu_a_context_changed,
                "FU-A context changed during NAL reassembly",
                "RTP timestamp, SSRC, payload type, NRI, or NAL type changed before the FU-A completed",
                "the incomplete NAL was discarded; a later FU-A start can begin a new NAL",
                *context, packet.sequence_number);
            fu_a_reassembler_.reset();
            return output;
        }

        auto reassembled = fu_a_reassembler_.push(packet);
        if (!reassembled) {
            if (const auto failed_context = context) {
                record_fu_failure(
                    DiagnosticCode::h264_rtp_invalid_payload,
                    "FU-A fragment could not be reassembled",
                    reassembled.error().message,
                    "the incomplete NAL was discarded; a later FU-A start can begin a new NAL",
                    *failed_context, packet.sequence_number);
            } else {
                record_invalid_payload(packet,
                                       DiagnosticCode::h264_rtp_invalid_payload,
                                       reassembled.error().message);
            }
            return output;
        }
        if (!*reassembled) {
            return output;
        }

        auto& nal = **reassembled;
        ++statistics_.completed_nal_units;
        if (nal.header.nal_unit_type == 5) {
            ++statistics_.completed_idr_nal_units;
        }
        output.push_back(DepacketizedH264NalUnit{
            .header = nal.header,
            .bytes = std::move(nal.bytes),
            .start_sequence_number = nal.start_sequence_number,
            .end_sequence_number = nal.end_sequence_number,
            .timestamp = nal.timestamp,
            .ssrc = nal.ssrc,
            .marker = nal.marker,
        });
        return output;
    }
    case H264RtpPayloadKind::stap_b:
    case H264RtpPayloadKind::mtap16:
    case H264RtpPayloadKind::mtap24:
    case H264RtpPayloadKind::fu_b:
    case H264RtpPayloadKind::reserved:
        record_invalid_payload(
            packet, DiagnosticCode::h264_rtp_invalid_payload,
            std::string("unsupported RFC 6184 payload kind: ") +
                h264_rtp_payload_kind_name(payload_header->kind));
        return output;
    }

    return output;
}

void H264RtpStreamAnalyzer::finish() {
    const auto context = fu_a_reassembler_.context();
    if (!context) {
        return;
    }
    record_fu_failure(
        DiagnosticCode::h264_fu_a_incomplete,
        "FU-A did not complete before the analysis ended",
        "FU-A starting at RTP sequence " +
            std::to_string(context->start_sequence_number) +
            " was still waiting for RTP sequence " +
            std::to_string(context->expected_sequence_number),
        "the incomplete NAL was discarded",
        *context, context->start_sequence_number);
    fu_a_reassembler_.reset();
}

const RtpSessionStatistics&
H264RtpStreamAnalyzer::rtp_statistics() const noexcept {
    return rtp_analyzer_.statistics();
}

const H264RtpStreamStatistics&
H264RtpStreamAnalyzer::h264_statistics() const noexcept {
    return statistics_;
}

std::span<const Diagnostic>
H264RtpStreamAnalyzer::diagnostics() const noexcept {
    return diagnostics_;
}

void H264RtpStreamAnalyzer::copy_new_rtp_diagnostics() {
    const auto source = rtp_analyzer_.diagnostics();
    while (copied_rtp_diagnostic_count_ < source.size()) {
        diagnostics_.push_back(source[copied_rtp_diagnostic_count_]);
        ++copied_rtp_diagnostic_count_;
    }
}

void H264RtpStreamAnalyzer::record_fu_failure(
    DiagnosticCode code,
    std::string summary,
    std::string evidence,
    std::string recovery,
    const H264FuAReassemblyContext& context,
    std::optional<std::uint16_t> sequence_number) {
    ++statistics_.incomplete_fu_a_nal_units;
    diagnostics_.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = code,
        .summary = std::move(summary),
        .evidence = std::move(evidence),
        .impact = "the current NAL unit cannot be reconstructed safely",
        .recovery = std::move(recovery),
        .location = location_for(context, sequence_number),
    });
    if (context.header.nal_unit_type != 5) {
        return;
    }
    ++statistics_.incomplete_idr_nal_units;
    diagnostics_.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = DiagnosticCode::h264_idr_incomplete,
        .summary = "An IDR NAL unit is incomplete",
        .evidence = "the discarded FU-A carried NAL unit type 5",
        .impact = "the current random-access picture is unavailable and visible corruption may continue",
        .recovery = "decodable random access requires a later intact IDR with the required parameter sets",
        .location = location_for(context, sequence_number),
    });
}

void H264RtpStreamAnalyzer::record_invalid_payload(
    const RtpPacket& packet,
    DiagnosticCode code,
    std::string evidence) {
    diagnostics_.push_back(Diagnostic{
        .severity = DiagnosticSeverity::error,
        .code = code,
        .summary = "H.264 RTP payload is invalid or unsupported",
        .evidence = std::move(evidence),
        .impact = "the payload was ignored",
        .recovery = "analysis continues with the next accepted RTP packet",
        .location = location_for(packet),
    });
}

void H264RtpStreamAnalyzer::append_complete_nal(
    std::vector<DepacketizedH264NalUnit>& output,
    NalHeader header,
    ByteView bytes,
    const RtpPacket& packet,
    bool marker) {
    ByteBuffer owned_bytes(bytes.begin(), bytes.end());
    ++statistics_.completed_nal_units;
    if (header.nal_unit_type == 5) {
        ++statistics_.completed_idr_nal_units;
    }
    output.push_back(DepacketizedH264NalUnit{
        .header = header,
        .bytes = std::move(owned_bytes),
        .start_sequence_number = packet.sequence_number,
        .end_sequence_number = packet.sequence_number,
        .timestamp = packet.timestamp,
        .ssrc = packet.ssrc,
        .marker = marker,
    });
}

} // namespace semi_stream_probe
