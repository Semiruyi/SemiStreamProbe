#include "semi_stream_probe/core/h264_rtp.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace semi_stream_probe {

namespace {

constexpr Byte fu_start_mask = 0b1000'0000;
constexpr Byte fu_end_mask = 0b0100'0000;
constexpr Byte fu_reserved_mask = 0b0010'0000;
constexpr Byte fu_type_mask = 0b0001'1111;
constexpr Byte nal_header_prefix_mask = 0b1110'0000;

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

[[nodiscard]] std::uint16_t
next_sequence_number(std::uint16_t sequence_number) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(sequence_number) + 1U);
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
        .maximum_nal_ref_idc = 0,
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

    result.maximum_nal_ref_idc = maximum_nal_ref_idc;
    return result;
}

std::expected<H264FuAFragment, ParseError>
parse_h264_fu_a_fragment(const RtpPacket& packet) {
    const auto payload_header = parse_h264_rtp_payload_header(packet.payload);
    if (!payload_header) {
        return std::unexpected(
            with_rtp_context(payload_header.error(), packet, 0));
    }
    if (payload_header->kind != H264RtpPayloadKind::fu_a) {
        return std::unexpected(make_h264_rtp_error(
            packet, 0, std::string("expected FU-A RTP payload, got ") +
                           h264_rtp_payload_kind_name(payload_header->kind)));
    }
    if (packet.payload.size() < 2) {
        return std::unexpected(make_h264_rtp_error(
            packet, packet.payload.size(), "FU-A header is truncated",
            ParseErrorCode::unexpected_end_of_data));
    }

    const auto fu_header = packet.payload[1];
    const auto start = (fu_header & fu_start_mask) != 0;
    const auto end = (fu_header & fu_end_mask) != 0;
    if ((fu_header & fu_reserved_mask) != 0) {
        return std::unexpected(make_h264_rtp_error(
            packet, 1, "FU-A reserved bit must be zero"));
    }
    if (start && end) {
        return std::unexpected(make_h264_rtp_error(
            packet, 1, "FU-A Start and End bits must not both be set"));
    }

    const auto nal_unit_type =
        static_cast<std::uint8_t>(fu_header & fu_type_mask);
    if (payload_kind(nal_unit_type) !=
        H264RtpPayloadKind::single_nal_unit) {
        return std::unexpected(make_h264_rtp_error(
            packet, 1,
            "FU-A header must identify an original NAL unit type from 1 to 23"));
    }
    if (packet.marker && !end) {
        return std::unexpected(make_h264_rtp_error(
            packet, 1,
            "RTP marker bit must not be set before the final FU-A fragment"));
    }

    return H264FuAFragment{
        .indicator = payload_header->nal_header,
        .start = start,
        .end = end,
        .nal_unit_type = nal_unit_type,
        .payload = packet.payload.subspan(2),
    };
}

H264FuAReassembler::H264FuAReassembler(std::size_t max_nal_unit_size)
    : max_nal_unit_size_(max_nal_unit_size) {}

std::expected<std::optional<H264ReassembledNalUnit>, ParseError>
H264FuAReassembler::push(const RtpPacket& packet) {
    const auto fragment = parse_h264_fu_a_fragment(packet);
    if (!fragment) {
        reset();
        return std::unexpected(fragment.error());
    }

    if (fragment->start) {
        if (active_) {
            reset();
            return std::unexpected(make_h264_rtp_error(
                packet, 1,
                "FU-A start arrived before the previous NAL unit completed"));
        }

        if (max_nal_unit_size_ == 0 ||
            fragment->payload.size() > max_nal_unit_size_ - 1) {
            return std::unexpected(make_h264_rtp_error(
                packet, 2,
                "FU-A reassembled NAL unit exceeds the configured size limit"));
        }

        const auto reconstructed_header = static_cast<Byte>(
            (packet.payload[0] & nal_header_prefix_mask) |
            fragment->nal_unit_type);
        ActiveNalUnit active{
            .header = NalHeader{
                .forbidden_zero_bit = false,
                .nal_ref_idc = fragment->indicator.nal_ref_idc,
                .nal_unit_type = fragment->nal_unit_type,
            },
            .bytes = {reconstructed_header},
            .start_sequence_number = packet.sequence_number,
            .expected_sequence_number =
                next_sequence_number(packet.sequence_number),
            .timestamp = packet.timestamp,
            .ssrc = packet.ssrc,
            .payload_type = packet.payload_type,
        };
        active.bytes.insert(active.bytes.end(), fragment->payload.begin(),
                            fragment->payload.end());
        active_ = std::move(active);
        return std::optional<H264ReassembledNalUnit>{};
    }

    if (!active_) {
        return std::unexpected(make_h264_rtp_error(
            packet, 1, "FU-A continuation arrived without a start fragment"));
    }

    if (packet.sequence_number != active_->expected_sequence_number) {
        const auto expected = active_->expected_sequence_number;
        reset();
        return std::unexpected(make_h264_rtp_error(
            packet, 0,
            "FU-A sequence discontinuity: expected " +
                std::to_string(expected) + ", got " +
                std::to_string(packet.sequence_number)));
    }
    if (packet.ssrc != active_->ssrc) {
        reset();
        return std::unexpected(make_h264_rtp_error(
            packet, 0, "FU-A SSRC changed during NAL unit reassembly"));
    }
    if (packet.timestamp != active_->timestamp) {
        reset();
        return std::unexpected(make_h264_rtp_error(
            packet, 0,
            "FU-A RTP timestamp changed during NAL unit reassembly"));
    }
    if (packet.payload_type != active_->payload_type) {
        reset();
        return std::unexpected(make_h264_rtp_error(
            packet, 0,
            "FU-A RTP payload type changed during NAL unit reassembly"));
    }
    if (fragment->indicator.nal_ref_idc != active_->header.nal_ref_idc ||
        fragment->nal_unit_type != active_->header.nal_unit_type) {
        reset();
        return std::unexpected(make_h264_rtp_error(
            packet, 0,
            "FU-A indicator or original NAL unit type changed during reassembly"));
    }
    if (fragment->payload.size() >
        max_nal_unit_size_ - active_->bytes.size()) {
        reset();
        return std::unexpected(make_h264_rtp_error(
            packet, 2,
            "FU-A reassembled NAL unit exceeds the configured size limit"));
    }

    active_->bytes.insert(active_->bytes.end(), fragment->payload.begin(),
                          fragment->payload.end());
    active_->expected_sequence_number =
        next_sequence_number(packet.sequence_number);
    if (!fragment->end) {
        return std::optional<H264ReassembledNalUnit>{};
    }

    H264ReassembledNalUnit completed{
        .header = active_->header,
        .bytes = std::move(active_->bytes),
        .start_sequence_number = active_->start_sequence_number,
        .end_sequence_number = packet.sequence_number,
        .timestamp = active_->timestamp,
        .ssrc = active_->ssrc,
        .marker = packet.marker,
    };
    reset();
    return std::optional<H264ReassembledNalUnit>{std::move(completed)};
}

bool H264FuAReassembler::in_progress() const noexcept {
    return active_.has_value();
}

std::optional<H264FuAReassemblyContext>
H264FuAReassembler::context() const noexcept {
    if (!active_) {
        return std::nullopt;
    }
    return H264FuAReassemblyContext{
        .header = active_->header,
        .start_sequence_number = active_->start_sequence_number,
        .expected_sequence_number = active_->expected_sequence_number,
        .timestamp = active_->timestamp,
        .ssrc = active_->ssrc,
        .payload_type = active_->payload_type,
    };
}

void H264FuAReassembler::reset() noexcept {
    active_.reset();
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
