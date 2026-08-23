#include "semi_stream_probe/core/h264_rtp.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace semi_stream_probe {

namespace {

[[nodiscard]] H264RtpPayloadKind
payload_kind(std::uint8_t nal_unit_type) noexcept {
    if (nal_unit_type >= 1 && nal_unit_type <= 23) {
        return H264RtpPayloadKind::single_nal_unit;
    }

    switch (nal_unit_type) {
    case 24:
        return H264RtpPayloadKind::stap_a;
    case 25:
        return H264RtpPayloadKind::stap_b;
    case 26:
        return H264RtpPayloadKind::mtap16;
    case 27:
        return H264RtpPayloadKind::mtap24;
    case 28:
        return H264RtpPayloadKind::fu_a;
    case 29:
        return H264RtpPayloadKind::fu_b;
    default:
        return H264RtpPayloadKind::reserved;
    }
}

} // namespace

std::expected<H264RtpPayloadHeader, ParseError>
parse_h264_rtp_payload_header(ByteView payload) {
    if (payload.empty()) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::invalid_h264_rtp_payload,
            .message = "H.264 RTP payload is empty",
        });
    }

    const auto header = parse_nal_header(payload);
    if (!header) {
        return std::unexpected(header.error());
    }

    return H264RtpPayloadHeader{
        .kind = payload_kind(header->nal_unit_type),
        .nal_header = *header,
    };
}

std::expected<H264SingleNalUnit, ParseError>
depacketize_h264_single_nal(const RtpPacket& packet) {
    const auto payload_header = parse_h264_rtp_payload_header(packet.payload);
    if (!payload_header) {
        auto error = payload_header.error();
        error.rtp_sequence_number = packet.sequence_number;
        return std::unexpected(std::move(error));
    }

    if (payload_header->kind != H264RtpPayloadKind::single_nal_unit) {
        return std::unexpected(ParseError{
            .code = ParseErrorCode::invalid_h264_rtp_payload,
            .rtp_sequence_number = packet.sequence_number,
            .message = std::string("expected Single NAL Unit RTP payload, got ") +
                       h264_rtp_payload_kind_name(payload_header->kind),
        });
    }

    return H264SingleNalUnit{
        .header = payload_header->nal_header,
        .bytes = packet.payload,
    };
}

const char* h264_rtp_payload_kind_name(H264RtpPayloadKind kind) noexcept {
    switch (kind) {
    case H264RtpPayloadKind::single_nal_unit:
        return "Single NAL Unit";
    case H264RtpPayloadKind::stap_a:
        return "STAP-A";
    case H264RtpPayloadKind::stap_b:
        return "STAP-B";
    case H264RtpPayloadKind::mtap16:
        return "MTAP16";
    case H264RtpPayloadKind::mtap24:
        return "MTAP24";
    case H264RtpPayloadKind::fu_a:
        return "FU-A";
    case H264RtpPayloadKind::fu_b:
        return "FU-B";
    case H264RtpPayloadKind::reserved:
        return "Reserved";
    }

    return "Unknown";
}

} // namespace semi_stream_probe
