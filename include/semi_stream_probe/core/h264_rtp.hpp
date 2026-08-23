#pragma once

#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/rtp.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <expected>
#include <optional>
#include <vector>

namespace semi_stream_probe {

inline constexpr std::size_t default_fu_a_max_nal_unit_size =
    64U * 1024U * 1024U;

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

struct H264NalUnitView {
    NalHeader header;
    ByteView bytes;
};

struct H264StapAPacket {
    NalHeader indicator;
    std::vector<H264NalUnitView> nal_units;
};

struct H264FuAFragment {
    NalHeader indicator;
    bool start{false};
    bool end{false};
    std::uint8_t nal_unit_type{0};
    ByteView payload;
};

struct H264ReassembledNalUnit {
    NalHeader header;
    ByteBuffer bytes;
    std::uint16_t start_sequence_number{0};
    std::uint16_t end_sequence_number{0};
    std::uint32_t timestamp{0};
    std::uint32_t ssrc{0};
    bool marker{false};
};

// Classifies an RFC 6184 payload from the NAL unit type in its first byte.
[[nodiscard]] std::expected<H264RtpPayloadHeader, ParseError>
parse_h264_rtp_payload_header(ByteView payload);

// Returns the complete, zero-copy NAL unit carried by a Single NAL Unit
// packet. Aggregation packets, fragmentation units, and reserved types are
// rejected by this entry point.
[[nodiscard]] std::expected<H264NalUnitView, ParseError>
depacketize_h264_single_nal(const RtpPacket& packet);

// Splits an RFC 6184 STAP-A payload into complete, zero-copy NAL unit views.
// Each aggregation unit is encoded as a 16-bit big-endian size followed by
// one NAL unit. Nested aggregation packets and fragmentation units are
// rejected.
[[nodiscard]] std::expected<H264StapAPacket, ParseError>
depacketize_h264_stap_a(const RtpPacket& packet);

// Parses and validates one RFC 6184 FU-A fragment. The fragment payload may
// be empty, as explicitly permitted by the RFC.
[[nodiscard]] std::expected<H264FuAFragment, ParseError>
parse_h264_fu_a_fragment(const RtpPacket& packet);

class H264FuAReassembler {
public:
    explicit H264FuAReassembler(
        std::size_t max_nal_unit_size = default_fu_a_max_nal_unit_size);

    // Returns an engaged optional only when the current fragment completes a
    // NAL unit. Reassembled bytes are owned by the returned value.
    [[nodiscard]] std::expected<std::optional<H264ReassembledNalUnit>,
                                ParseError>
    push(const RtpPacket& packet);

    [[nodiscard]] bool in_progress() const noexcept;
    void reset() noexcept;

private:
    struct ActiveNalUnit {
        NalHeader header;
        ByteBuffer bytes;
        std::uint16_t start_sequence_number{0};
        std::uint16_t expected_sequence_number{0};
        std::uint32_t timestamp{0};
        std::uint32_t ssrc{0};
        std::uint8_t payload_type{0};
    };

    std::optional<ActiveNalUnit> active_;
    std::size_t max_nal_unit_size_{default_fu_a_max_nal_unit_size};
};

[[nodiscard]] const char*
h264_rtp_payload_kind_name(H264RtpPayloadKind kind) noexcept;

} // namespace semi_stream_probe
