#include "semi_stream_probe/core/rtp.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace semi_stream_probe {

namespace {

constexpr Byte version_mask = 0b1100'0000;
constexpr Byte padding_mask = 0b0010'0000;
constexpr Byte extension_mask = 0b0001'0000;
constexpr Byte csrc_count_mask = 0b0000'1111;
constexpr Byte marker_mask = 0b1000'0000;
constexpr Byte payload_type_mask = 0b0111'1111;
constexpr unsigned int version_shift = 6U;
constexpr std::size_t csrc_size = 4;
constexpr std::size_t extension_header_size = 4;
constexpr std::size_t extension_word_size = 4;

[[nodiscard]] std::uint16_t read_u16_be(ByteView bytes,
                                        std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1]));
}

[[nodiscard]] std::uint32_t read_u32_be(ByteView bytes,
                                        std::size_t offset) noexcept {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

[[nodiscard]] ParseError truncated_error(std::size_t byte_offset,
                                         std::string message) {
    return ParseError{
        .code = ParseErrorCode::unexpected_end_of_data,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] ParseError invalid_rtp_error(std::size_t byte_offset,
                                           std::string message) {
    return ParseError{
        .code = ParseErrorCode::invalid_rtp,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

} // namespace

std::expected<RtpPacket, ParseError> parse_rtp_packet(ByteView packet) {
    if (packet.size() < rtp_fixed_header_size) {
        return std::unexpected(truncated_error(
            packet.size(), "RTP packet is shorter than the 12-byte fixed header"));
    }

    RtpPacket result;
    result.version = static_cast<std::uint8_t>(
        (packet[0] & version_mask) >> version_shift);
    if (result.version != 2) {
        return std::unexpected(invalid_rtp_error(
            0, "RTP version must be 2, got " + std::to_string(result.version)));
    }

    result.has_padding = (packet[0] & padding_mask) != 0;
    result.has_extension = (packet[0] & extension_mask) != 0;
    result.marker = (packet[1] & marker_mask) != 0;
    result.payload_type =
        static_cast<std::uint8_t>(packet[1] & payload_type_mask);
    result.sequence_number = read_u16_be(packet, 2);
    result.timestamp = read_u32_be(packet, 4);
    result.ssrc = read_u32_be(packet, 8);

    const auto csrc_count =
        static_cast<std::size_t>(packet[0] & csrc_count_mask);
    const auto csrc_bytes = csrc_count * csrc_size;
    if (packet.size() - rtp_fixed_header_size < csrc_bytes) {
        return std::unexpected(truncated_error(
            rtp_fixed_header_size,
            "RTP packet is truncated in its CSRC list"));
    }

    result.csrcs.reserve(csrc_count);
    auto offset = rtp_fixed_header_size;
    for (std::size_t index = 0; index < csrc_count; ++index) {
        result.csrcs.push_back(read_u32_be(packet, offset));
        offset += csrc_size;
    }

    if (result.has_extension) {
        if (packet.size() - offset < extension_header_size) {
            return std::unexpected(truncated_error(
                offset, "RTP header extension is missing its 4-byte header"));
        }

        const auto profile_identifier = read_u16_be(packet, offset);
        const auto length_words = read_u16_be(packet, offset + 2);
        offset += extension_header_size;

        const auto extension_size =
            static_cast<std::size_t>(length_words) * extension_word_size;
        if (packet.size() - offset < extension_size) {
            return std::unexpected(truncated_error(
                offset, "RTP header extension data is truncated"));
        }

        result.extension = RtpHeaderExtension{
            .profile_identifier = profile_identifier,
            .length_words = length_words,
            .data = packet.subspan(offset, extension_size),
        };
        offset += extension_size;
    }

    result.header_size = offset;
    const auto bytes_after_header = packet.size() - offset;
    if (result.has_padding) {
        if (bytes_after_header == 0) {
            return std::unexpected(invalid_rtp_error(
                packet.size(), "RTP padding bit is set but padding is absent"));
        }

        result.padding_size = static_cast<std::size_t>(packet.back());
        if (result.padding_size == 0) {
            return std::unexpected(invalid_rtp_error(
                packet.size() - 1,
                "RTP padding count in the final byte must not be zero"));
        }
        if (result.padding_size > bytes_after_header) {
            return std::unexpected(invalid_rtp_error(
                packet.size() - 1,
                "RTP padding count exceeds the bytes after the header"));
        }
    }

    result.payload =
        packet.subspan(offset, bytes_after_header - result.padding_size);
    return result;
}

} // namespace semi_stream_probe
