#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace semi_stream_probe {

enum class DiagnosticSeverity {
    info,
    warning,
    error,
};

enum class DiagnosticCode {
    rtp_invalid_packet,
    rtp_unexpected_payload_type,
    rtp_unexpected_ssrc,
    rtp_sequence_gap,
    rtp_duplicate_packet,
    rtp_out_of_order_packet,
    h264_rtp_invalid_payload,
    h264_stap_a_invalid,
    h264_fu_a_missing_start,
    h264_fu_a_sequence_gap,
    h264_fu_a_interrupted,
    h264_fu_a_incomplete,
    h264_fu_a_context_changed,
    h264_parameter_set_not_found,
    h264_idr_incomplete,
};

struct DiagnosticLocation {
    std::optional<std::size_t> input_byte_offset;
    std::optional<std::size_t> bit_offset;
    std::optional<std::size_t> nal_index;
    std::optional<std::uint16_t> rtp_sequence_number;
    std::optional<std::uint32_t> ssrc;
    std::optional<std::uint32_t> rtp_timestamp;
};

struct Diagnostic {
    DiagnosticSeverity severity{DiagnosticSeverity::info};
    DiagnosticCode code{DiagnosticCode::rtp_invalid_packet};
    std::string summary;
    std::string evidence;
    std::optional<std::string> impact;
    std::optional<std::string> recovery;
    DiagnosticLocation location;
};

[[nodiscard]] std::string_view
to_string(DiagnosticSeverity severity) noexcept;

// Diagnostic code strings are part of the v0.1.0 JSON contract and must not
// change when user-facing wording changes.
[[nodiscard]] std::string_view to_string(DiagnosticCode code) noexcept;

} // namespace semi_stream_probe
