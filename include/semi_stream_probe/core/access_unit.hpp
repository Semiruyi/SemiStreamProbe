#pragma once

#include "semi_stream_probe/core/nal.hpp"
#include "semi_stream_probe/core/slice.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace semi_stream_probe {

inline constexpr std::size_t slice_type_count = 5;

// One access unit contains the NAL units associated with one primary coded
// picture. In progressive material this normally corresponds to one frame.
struct AccessUnit {
    std::size_t index{0};
    std::vector<std::size_t> nal_indices;
    std::vector<std::size_t> vcl_nal_indices;
    std::array<bool, slice_type_count> slice_types_present{};
    SliceHeader first_vcl;
};

// Implements the first-VCL-NAL comparison from H.264 7.4.1.2.4. Macroblock
// address and slice type are deliberately not picture-boundary conditions.
[[nodiscard]] bool starts_new_primary_coded_picture(
    const SliceHeader& first_vcl,
    const SliceHeader& candidate) noexcept;

// Stateful so Annex-B files and a future RTP depacketizer can feed the same
// deterministic access-unit model. The caller must pass parsed SliceHeader
// data for the supported VCL NAL types 1 and 5.
class AccessUnitAssembler {
public:
    [[nodiscard]] std::optional<AccessUnit>
    push(std::size_t nal_index,
         const NalHeader& nal_header,
         const SliceHeader* slice_header = nullptr);

    [[nodiscard]] std::optional<AccessUnit> finish();

private:
    [[nodiscard]] std::optional<AccessUnit> complete_current();
    void begin_picture(std::size_t nal_index, const SliceHeader& slice_header);

    std::size_t next_index_{0};
    std::vector<std::size_t> pending_prefix_nals_;
    std::optional<AccessUnit> current_;
};

} // namespace semi_stream_probe
