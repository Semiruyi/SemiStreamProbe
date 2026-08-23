#include "semi_stream_probe/core/h264_rtp.hpp"

#include <algorithm>
#include <cstddef>
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

[[nodiscard]] std::uint16_t read_u16_be(ByteView bytes,
                                        std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

[[nodiscard]] ParseError with_rtp_context(ParseError error,
                                          const RtpPacket& packet,
                                          std::size_t byte_offset) {
    error.byte_offset = byte_offset;
    error.rtp_sequence_number = packet.sequence_number;
    return error;
}

[[nodiscard]] ParseError make_h264_rtp_error(
    const RtpPacket& packet,
    std::size_t byte_offset,
    std::string message,
    ParseErrorCode code = ParseErrorCode::invalid_h264_rtp_payload) {
    return ParseError{
        .code = code,
        .byte_offset = byte_offset,
        .rtp_sequence_number = packet.sequence_number,
        .message = std::move(message),
    };
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

std::expected<H264NalUnitView, ParseError>
depacketize_h264_single_nal(const RtpPacket& packet) {
    const auto payload_header = parse_h264_rtp_payload_header(packet.payload);
    if (!payload_header) {
        return std::unexpected(
            with_rtp_context(payload_header.error(), packet, 0));
    }

    if (payload_header->kind != H264RtpPayloadKind::single_nal_unit) {
        return std::unexpected(make_h264_rtp_error(
            packet, 0,
            std::string("expected Single NAL Unit RTP payload, got ") +
                h264_rtp_payload_kind_name(payload_header->kind)));
    }

    return H264NalUnitView{
        .header = payload_header->nal_header,
        .bytes = packet.payload,
    };
}

std::expected<H264StapAPacket, ParseError>
depacketize_h264_stap_a(const RtpPacket& packet) {
    const auto payload_header = parse_h264_rtp_payload_header(packet.payload);
    if (!payload_header) {
        return std::unexpected(
            with_rtp_context(payload_header.error(), packet, 0));
    }

    if (payload_header->kind != H264RtpPayloadKind::stap_a) {
        return std::unexpected(make_h264_rtp_error(
            packet, 0, std::string("expected STAP-A RTP payload, got ") +
                           h264_rtp_payload_kind_name(payload_header->kind)));
    }

    H264StapAPacket result{
        .indicator = payload_header->nal_header,
        .nal_units = {},
    };
    auto offset = std::size_t{1};
    if (offset == packet.payload.size()) {
        return std::unexpected(make_h264_rtp_error(
            packet, offset, "STAP-A does not contain an aggregation unit"));
    }

    std::uint8_t maximum_nal_ref_idc = 0;
    while (offset < packet.payload.size()) {
        if (packet.payload.size() - offset < 2) {
            return std::unexpected(make_h264_rtp_error(
                packet, offset, "STAP-A aggregation unit size is truncated",
                ParseErrorCode::unexpected_end_of_data));
        }

        const auto nal_size =
            static_cast<std::size_t>(read_u16_be(packet.payload, offset));
        offset += 2;
        if (nal_size == 0) {
            return std::unexpected(make_h264_rtp_error(
                packet, offset - 2,
                "STAP-A aggregation unit must not contain an empty NAL unit"));
        }
        if (packet.payload.size() - offset < nal_size) {
            return std::unexpected(make_h264_rtp_error(
                packet, offset,
                "STAP-A NAL unit is shorter than its declared size",
                ParseErrorCode::unexpected_end_of_data));
        }

        const auto nal_bytes = packet.payload.subspan(offset, nal_size);
        const auto nal_header = parse_nal_header(nal_bytes);
        if (!nal_header) {
            return std::unexpected(
                with_rtp_context(nal_header.error(), packet, offset));
        }

        const auto kind = payload_kind(nal_header->nal_unit_type);
        if (kind != H264RtpPayloadKind::single_nal_unit) {
            return std::unexpected(make_h264_rtp_error(
                packet, offset,
                std::string("STAP-A cannot contain ") +
                    h264_rtp_payload_kind_name(kind)));
        }

        maximum_nal_ref_idc =
            std::max(maximum_nal_ref_idc, nal_header->nal_ref_idc);
        result.nal_units.push_back(H264NalUnitView{
            .header = *nal_header,
            .bytes = nal_bytes,
        });
        offset += nal_size;
    }

    if (result.indicator.nal_ref_idc != maximum_nal_ref_idc) {
        return std::unexpected(make_h264_rtp_error(
            packet, 0,
            "STAP-A NRI must equal the maximum NRI of its contained NAL units"));
    }

    return result;
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
