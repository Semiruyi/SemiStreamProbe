#pragma once

#include "semi_stream_probe/core/h264_syntax.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace semi_stream_probe {

class ParameterSetRegistry {
public:
    // Returns true when an existing parameter set with the same id was replaced.
    bool store(Sps sps);
    bool store(Pps pps);

    [[nodiscard]] const Sps* find_sps(std::uint32_t id) const noexcept;
    [[nodiscard]] const Pps* find_pps(std::uint32_t id) const noexcept;

private:
    std::array<std::optional<Sps>, 32> sequence_parameter_sets_;
    std::array<std::optional<Pps>, 256> picture_parameter_sets_;
};

} // namespace semi_stream_probe
