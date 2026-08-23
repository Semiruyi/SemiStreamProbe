#include "semi_stream_probe/core/parameter_sets.hpp"

#include <cstddef>
#include <utility>

namespace semi_stream_probe {

bool ParameterSetRegistry::store(Sps sps) {
    const auto index = static_cast<std::size_t>(sps.seq_parameter_set_id);
    if (index >= sequence_parameter_sets_.size()) {
        return false;
    }
    const bool replaced = sequence_parameter_sets_[index].has_value();
    sequence_parameter_sets_[index] = std::move(sps);
    return replaced;
}

bool ParameterSetRegistry::store(Pps pps) {
    const auto index = static_cast<std::size_t>(pps.pic_parameter_set_id);
    if (index >= picture_parameter_sets_.size()) {
        return false;
    }
    const bool replaced = picture_parameter_sets_[index].has_value();
    picture_parameter_sets_[index] = std::move(pps);
    return replaced;
}

const Sps* ParameterSetRegistry::find_sps(std::uint32_t id) const noexcept {
    if (id >= sequence_parameter_sets_.size()) {
        return nullptr;
    }
    const auto& value = sequence_parameter_sets_[static_cast<std::size_t>(id)];
    return value ? &*value : nullptr;
}

const Pps* ParameterSetRegistry::find_pps(std::uint32_t id) const noexcept {
    if (id >= picture_parameter_sets_.size()) {
        return nullptr;
    }
    const auto& value = picture_parameter_sets_[static_cast<std::size_t>(id)];
    return value ? &*value : nullptr;
}

} // namespace semi_stream_probe
