#pragma once

#include "custom_catalog.hpp"
#include "fs.hpp"
#include "manifest.hpp"

#include <string>
#include <vector>

namespace sphaira::custom::update_detail {

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

} // namespace sphaira::custom::update_detail
