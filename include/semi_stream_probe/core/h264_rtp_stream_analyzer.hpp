#pragma once

#include "semi_stream_probe/core/diagnostic.hpp"
#include "semi_stream_probe/core/h264_rtp.hpp"
#include "semi_stream_probe/core/rtp_stream_analyzer.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace semi_stream_probe {

struct H264RtpStreamAnalyzerConfig {
    RtpStreamAnalyzerConfig rtp;
    std::size_t max_nal_unit_size{default_fu_a_max_nal_unit_size};
};

struct H264RtpStreamStatistics {
    std::uint64_t single_nal_packets{0};
    std::uint64_t stap_a_packets{0};
    std::uint64_t fu_a_packets{0};
    std::uint64_t completed_nal_units{0};
    std::uint64_t completed_idr_nal_units{0};
    std::uint64_t incomplete_fu_a_nal_units{0};
    std::uint64_t incomplete_idr_nal_units{0};
};

struct DepacketizedH264NalUnit {
    NalHeader header;
    ByteBuffer bytes;
    std::uint16_t start_sequence_number{0};
    std::uint16_t end_sequence_number{0};
    std::uint32_t timestamp{0};
    std::uint32_t ssrc{0};
    bool marker{false};
};

class H264RtpStreamAnalyzer {
public:
    explicit H264RtpStreamAnalyzer(H264RtpStreamAnalyzerConfig config = {});

    // Returned NAL units own their bytes and remain valid after the input
    // datagram is released.
    [[nodiscard]] std::vector<DepacketizedH264NalUnit>
    push(ByteView datagram, std::chrono::microseconds arrival_time);

    // Records and discards an unfinished FU-A at end of input. Idempotent.
    void finish();

    [[nodiscard]] const RtpSessionStatistics&
    rtp_statistics() const noexcept;
    [[nodiscard]] const H264RtpStreamStatistics&
    h264_statistics() const noexcept;
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept;

private:
    void copy_new_rtp_diagnostics();
    void record_fu_failure(DiagnosticCode code,
                           std::string summary,
                           std::string evidence,
                           std::string recovery,
                           const H264FuAReassemblyContext& context,
                           std::optional<std::uint16_t> sequence_number);
    void record_invalid_payload(const RtpPacket& packet,
                                DiagnosticCode code,
                                std::string evidence);
    void append_complete_nal(std::vector<DepacketizedH264NalUnit>& output,
                             NalHeader header,
                             ByteView bytes,
                             const RtpPacket& packet,
                             bool marker);

    RtpStreamAnalyzer rtp_analyzer_;
    H264FuAReassembler fu_a_reassembler_;
    H264RtpStreamStatistics statistics_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t copied_rtp_diagnostic_count_{0};
};

} // namespace semi_stream_probe
