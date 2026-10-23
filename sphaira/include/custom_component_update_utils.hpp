#pragma once

#include "custom_component_update_types.hpp"
#include "ui/progress_box.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sphaira::custom::update_detail {

bool NormalizeRelativePath(std::string path, std::string& normalized);
bool IsSafePathSegment(const std::string& segment);
bool MakeFsPath(const std::string& value, fs::FsPath& path);
std::string JoinPath(const std::string& parent, const std::string& child);
bool WildcardMatch(std::string_view pattern, std::string_view value);
std::string BaseName(const std::string& path);
std::string WithoutExtension(const std::string& filename);

Result ExtractZip(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& zip_path,
    const fs::FsPath& output_path);
Result ListFiles(fs::Fs* filesystem, const fs::FsPath& directory, std::vector<fs::FsPath>& files);
Result ListDirectories(fs::Fs* filesystem, const fs::FsPath& directory, std::vector<fs::FsPath>& directories);
bool GetRelativePath(const fs::FsPath& root, const fs::FsPath& path, std::string& relative);
bool PathIsShared(
    const std::string& id,
    const std::string& path,
    const manifest::Manifest& active,
    const manifest::DisabledComponents& disabled);
Result CopyFileToPath(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& source,
    const fs::FsPath& destination);
Result CopyDirectoryFiles(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& source_root,
    const fs::FsPath& destination_root);
bool GetComponentDestination(
    const PreparedComponentUpdate& prepared_update,
    const std::string& relative,
    fs::FsPath& path);

} // namespace sphaira::custom::update_detail
