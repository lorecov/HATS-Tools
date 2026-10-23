#pragma once

#include "custom_component_update_types.hpp"
#include "ui/progress_box.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sphaira::custom::updater {

// Processes downloaded assets into staged files and discovers destination conflicts.
Result ProcessDownloadedComponent(
    ui::ProgressBox* progress,
    const std::string& id,
    const std::vector<update_detail::DownloadedAsset>& downloaded_assets,
    update_detail::PreparedComponentUpdate& prepared);

// Installs staged files and records them in the active or disabled manifest.
Result InstallPreparedComponent(
    ui::ProgressBox* progress,
    update_detail::PreparedComponentUpdate& prepared,
    const std::vector<std::string>& approved_conflicts);

// Applies release versions already fetched by the network coordinator.
bool ApplyFetchedVersions(
    const std::map<std::string, std::string>& latest_versions,
    const std::map<std::string, std::string>& previous_versions,
    size_t& updated_components);

} // namespace sphaira::custom::updater
