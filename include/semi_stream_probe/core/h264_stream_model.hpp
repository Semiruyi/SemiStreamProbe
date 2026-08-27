#pragma once

#include "semi_stream_probe/core/access_unit.hpp"
#include "semi_stream_probe/core/diagnostic.hpp"
#include "semi_stream_probe/core/gop.hpp"
#include "semi_stream_probe/core/h264_syntax.hpp"
#include "semi_stream_probe/core/parameter_sets.hpp"
#include "semi_stream_probe/core/parse_error.hpp"
#include "semi_stream_probe/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace semi_stream_probe {

struct H264NalSourceContext {
    std::optional<std::size_t> input_byte_offset;
    std::optional<std::uint16_t> rtp_sequence_number;
    std::optional<std::uint32_t> ssrc;
    std::optional<std::uint32_t> rtp_timestamp;
};

struct H264StreamModelStatistics {
    std::uint64_t nal_units{0};
    std::uint64_t slices{0};
    std::vector<Sps> sequence_parameter_sets;
    std::vector<Pps> picture_parameter_sets;
    std::vector<AccessUnit> access_units;
    GopStatistics gop;
};

class H264StreamModel {
public:
    // nal_unit includes the one-byte NAL header and owns no memory after push
    // returns. Invalid syntax becomes a diagnostic and does not stop later NALs.
    void push(ByteView nal_unit, H264NalSourceContext source = {});
    void finish();

    [[nodiscard]] const H264StreamModelStatistics&
    statistics() const noexcept;
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept;

private:
    void record_error(const ParseError& error,
                      DiagnosticCode code,
                      std::string summary,
                      std::size_t nal_index,
                      const H264NalSourceContext& source);
    void accept_non_vcl(std::size_t nal_index, const NalHeader& header);

    ParameterSetRegistry parameter_sets_;
    AccessUnitAssembler access_unit_assembler_;
    H264StreamModelStatistics statistics_;
    std::vector<Diagnostic> diagnostics_;
    bool finished_{false};
};

} // namespace semi_stream_probe
