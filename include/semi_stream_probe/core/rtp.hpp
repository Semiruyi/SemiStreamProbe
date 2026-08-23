#pragma once

#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

namespace semi_stream_probe {

inline constexpr std::size_t rtp_fixed_header_size = 12;

struct RtpHeaderExtension {
    std::uint16_t profile_identifier{0};
    std::uint16_t length_words{0};
    ByteView data;
};

struct RtpPacket {
    std::uint8_t version{0};
    bool has_padding{false};
    bool has_extension{false};
    bool marker{false};
    std::uint8_t payload_type{0};
    std::uint16_t sequence_number{0};
    std::uint32_t timestamp{0};
    std::uint32_t ssrc{0};
    std::vector<std::uint32_t> csrcs;
    std::optional<RtpHeaderExtension> extension;
    std::size_t header_size{0};
    std::size_t padding_size{0};
    ByteView payload;
};

// Parses one complete RTP packet as defined by RFC 3550 sections 5.1 and
// 5.3.1. Returned ByteViews refer to the caller-owned packet buffer.
[[nodiscard]] std::expected<RtpPacket, ParseError>
parse_rtp_packet(ByteView packet);

} // namespace semi_stream_probe
