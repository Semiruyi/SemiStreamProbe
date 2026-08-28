#include "semi_stream_probe/core/diagnostic.hpp"

namespace semi_stream_probe {

std::string_view to_string(DiagnosticSeverity severity) noexcept {
    switch (severity) {
    case DiagnosticSeverity::info:
        return "info";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::error:
        return "error";
    }

    return "unknown";
}

std::string_view to_string(DiagnosticCode code) noexcept {
    switch (code) {
    case DiagnosticCode::rtp_invalid_packet:
        return "RTP_INVALID_PACKET";
    case DiagnosticCode::rtp_unexpected_payload_type:
        return "RTP_UNEXPECTED_PAYLOAD_TYPE";
    case DiagnosticCode::rtp_unexpected_ssrc:
        return "RTP_UNEXPECTED_SSRC";
    case DiagnosticCode::rtp_sequence_gap:
        return "RTP_SEQUENCE_GAP";
    case DiagnosticCode::rtp_duplicate_packet:
        return "RTP_DUPLICATE_PACKET";
    case DiagnosticCode::rtp_out_of_order_packet:
        return "RTP_OUT_OF_ORDER_PACKET";
    case DiagnosticCode::h264_rtp_invalid_payload:
        return "H264_RTP_INVALID_PAYLOAD";
    case DiagnosticCode::h264_stap_a_invalid:
        return "H264_STAP_A_INVALID";
    case DiagnosticCode::h264_stap_a_nri_mismatch:
        return "H264_STAP_A_NRI_MISMATCH";
    case DiagnosticCode::h264_fu_a_missing_start:
        return "H264_FU_A_MISSING_START";
    case DiagnosticCode::h264_fu_a_sequence_gap:
        return "H264_FU_A_SEQUENCE_GAP";
    case DiagnosticCode::h264_fu_a_interrupted:
        return "H264_FU_A_INTERRUPTED";
    case DiagnosticCode::h264_fu_a_incomplete:
        return "H264_FU_A_INCOMPLETE";
    case DiagnosticCode::h264_fu_a_context_changed:
        return "H264_FU_A_CONTEXT_CHANGED";
    case DiagnosticCode::h264_syntax_invalid:
        return "H264_SYNTAX_INVALID";
    case DiagnosticCode::h264_parameter_set_not_found:
        return "H264_PARAMETER_SET_NOT_FOUND";
    case DiagnosticCode::h264_idr_incomplete:
        return "H264_IDR_INCOMPLETE";
    }

    return "UNKNOWN";
}

} // namespace semi_stream_probe
