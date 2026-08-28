#include "semi_stream_probe/core/annex_b.hpp"
#include "semi_stream_probe/core/h264_stream_model.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
    const semi_stream_probe::ByteView bytes{data, size};
    const auto units = semi_stream_probe::scan_annex_b(bytes);
    if (!units) {
        return 0;
    }

    semi_stream_probe::H264StreamModel model;
    for (const auto& unit : *units) {
        const semi_stream_probe::H264NalSourceContext source{
            .input_byte_offset = unit.payload_offset,
            .rtp_sequence_number = std::nullopt,
            .ssrc = std::nullopt,
            .rtp_timestamp = std::nullopt,
        };
        model.push(bytes.subspan(unit.payload_offset, unit.payload_size), source);
    }
    model.finish();

    // Exercise the finalized views as part of the fuzz surface.
    static_cast<void>(model.statistics());
    static_cast<void>(model.diagnostics());
    return 0;
}
