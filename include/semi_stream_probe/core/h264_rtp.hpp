#pragma once

#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/rtp.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <expected>

namespace semi_stream_probe {

enum class H264RtpPayloadKind {
    single_nal_unit,
    stap_a,
    stap_b,
    mtap16,
    mtap24,
    fu_a,
    fu_b,
    reserved,
};

struct H264RtpPayloadHeader {
    H264RtpPayloadKind kind{H264RtpPayloadKind::reserved};
    NalHeader nal_header;
};

struct H264SingleNalUnit {
    NalHeader header;
    ByteView bytes;
};

// Classifies an RFC 6184 payload from the NAL unit type in its first byte.
[[nodiscard]] std::expected<H264RtpPayloadHeader, ParseError>
parse_h264_rtp_payload_header(ByteView payload);

// Returns the complete, zero-copy NAL unit carried by a Single NAL Unit
// packet. Aggregation packets, fragmentation units, and reserved types are
// rejected by this entry point.
[[nodiscard]] std::expected<H264SingleNalUnit, ParseError>
depacketize_h264_single_nal(const RtpPacket& packet);

[[nodiscard]] const char*
h264_rtp_payload_kind_name(H264RtpPayloadKind kind) noexcept;

} // namespace semi_stream_probe
