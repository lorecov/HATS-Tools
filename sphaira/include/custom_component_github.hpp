#pragma once

#include "fs.hpp"
#include "ui/progress_box.hpp"

#include <string>
#include <vector>

namespace sphaira::custom::github {

struct ReleaseAsset {
    std::string name;
    std::string download_url;
};

bool FetchLatestReleaseTag(ui::ProgressBox* progress, const std::string& repository, std::string& tag);
bool FetchReleaseAssets(
    ui::ProgressBox* progress,
    const std::string& repository,
    const std::string& version,
    std::vector<ReleaseAsset>& assets);
bool DownloadReleaseAsset(ui::ProgressBox* progress, const std::string& url, const fs::FsPath& destination);
bool AssetMatchesPattern(const std::string& pattern, const std::string& asset_name);

} // namespace sphaira::custom::github
