#pragma once

#include "custom_catalog.hpp"
#include "fs.hpp"
#include "manifest.hpp"
#include "ui/progress_box.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace sphaira::custom::updater {

struct DownloadedAsset {
    std::string pattern;
    std::string name;
    fs::FsPath path;
};

struct StagedFile {
    std::string relative_path;
    fs::FsPath staged_path;
};

struct PreparedComponentUpdate {
    std::string id;
    catalog::CustomComponent component;
    std::vector<catalog::AssetPattern> assets;
    manifest::Component manifest_component;
    bool is_disabled{};
    std::vector<StagedFile> staged_files;
    std::vector<std::string> conflicts;
};

// Processes downloaded assets into staged files and discovers destination conflicts.
Result ProcessDownloadedComponent(
    ui::ProgressBox* progress,
    const std::string& id,
    const std::vector<DownloadedAsset>& downloaded_assets,
    PreparedComponentUpdate& prepared);

// Installs staged files and records them in the active or disabled manifest.
Result InstallPreparedComponent(
    ui::ProgressBox* progress,
    PreparedComponentUpdate& prepared,
    const std::vector<std::string>& approved_conflicts);

// Applies release versions already fetched by the network coordinator.
bool ApplyFetchedVersions(
    const std::map<std::string, std::string>& latest_versions,
    const std::map<std::string, std::string>& previous_versions,
    size_t& updated_components);

} // namespace sphaira::custom::updater
