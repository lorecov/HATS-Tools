#pragma once

#include "custom_catalog.hpp"

namespace sphaira::ui::menu::hats::detail {

inline bool ProcessingStepsEqual(
    const std::vector<catalog::ProcessingStep>& lhs,
    const std::vector<catalog::ProcessingStep>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        const auto& left = lhs[i];
        const auto& right = rhs[i];
        if (left.type != right.type ||
            left.target_path != right.target_path ||
            left.subfolder_name != right.subfolder_name ||
            left.source_file_pattern != right.source_file_pattern ||
            left.target_filename != right.target_filename) {
            return false;
        }
    }
    return true;
}

} // namespace sphaira::ui::menu::hats::detail
