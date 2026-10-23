#include "custom_components.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/menus/custom_component_editor.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>
#include <yyjson.h>

namespace sphaira::custom {
namespace {

struct FetchVersionsResult {
    size_t total{};
    size_t updated{};
    size_t failed{};
    bool cancelled{};
    bool fatal_error{};
};

struct UpdateComponentsResult {
    size_t total{};
    size_t updated{};
    size_t failed{};
    size_t skipped{};
    bool cancelled{};
    bool fatal_error{};
};

struct ReleaseAsset {
    std::string name;
    std::string url;
};

struct StagedFile {
    std::string relative_path;
    fs::FsPath staged_path;
};

struct FileMove {
    fs::FsPath original;
    fs::FsPath backup;
};

bool IsRepoCharacter(char character) {
    const auto value = static_cast<unsigned char>(character);
    return std::isalnum(value) || character == '-' || character == '_' || character == '.';
}

bool ParseGitHubRepo(std::string repo, std::string& owner, std::string& name) {
    constexpr std::string_view https_prefix{"https://github.com/"};
    constexpr std::string_view http_prefix{"http://github.com/"};
    if (repo.starts_with(https_prefix)) {
        repo.erase(0, https_prefix.size());
    } else if (repo.starts_with(http_prefix)) {
        repo.erase(0, http_prefix.size());
    }
    while (!repo.empty() && repo.back() == '/') {
        repo.pop_back();
    }
    if (repo.ends_with(".git")) {
        repo.resize(repo.size() - 4);
    }

    const auto slash = repo.find('/');
    if (slash == std::string::npos || slash == 0 || slash == repo.size() - 1 ||
        repo.find('/', slash + 1) != std::string::npos) {
        return false;
    }

    owner = repo.substr(0, slash);
    name = repo.substr(slash + 1);
    const auto valid_segment = [](const std::string& segment) {
        if (segment == "." || segment == "..") {
            return false;
        }
        return std::all_of(segment.begin(), segment.end(), IsRepoCharacter);
    };
    return valid_segment(owner) && valid_segment(name);
}

bool FetchLatestTag(ui::ProgressBox* progress, const std::string& repo, std::string& tag) {
    std::string owner;
    std::string name;
    if (!ParseGitHubRepo(repo, owner, name)) {
        return false;
    }

    const auto result = curl::Api().ToMemory(
        curl::Url{"https://api.github.com/repos/" + owner + "/" + name + "/releases/latest"},
        curl::Header{{"Accept", "application/vnd.github+json"}},
        curl::OnProgress{progress->OnDownloadProgressCallback()});
    if (!result.success || result.data.empty()) {
        return false;
    }

    auto* doc = yyjson_read(
        reinterpret_cast<const char*>(result.data.data()), result.data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        return false;
    }
    auto* tag_value = yyjson_obj_get(root, "tag_name");
    if (!yyjson_is_str(tag_value)) {
        return false;
    }

    const auto* tag_name = yyjson_get_str(tag_value);
    if (!tag_name || !*tag_name) {
        return false;
    }
    tag = tag_name;
    return true;
}

std::string FormatManifestVersion(const std::string& current_version, const std::string& latest_version) {
    constexpr std::string_view separator{" ---> "};
    const auto separator_pos = current_version.find(separator);
    auto installed_version = current_version.substr(0, separator_pos);
    if (installed_version.empty()) {
        installed_version = "not installed";
    }
    return installed_version + std::string{separator} + latest_version;
}

bool NormalizeRelativePath(std::string path, std::string& normalized) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.find(':') != std::string::npos) {
        return false;
    }

    std::vector<std::string> segments;
    size_t start = 0;
    while (start < path.size() && path[start] == '/') {
        ++start;
    }
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto length = (end == std::string::npos ? path.size() : end) - start;
        const auto segment = path.substr(start, length);
        if (segment == "..") {
            return false;
        }
        if (!segment.empty() && segment != ".") {
            segments.emplace_back(segment);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    normalized.clear();
    for (const auto& segment : segments) {
        if (!normalized.empty()) {
            normalized += '/';
        }
        normalized += segment;
    }
    return true;
}

bool IsSafePathSegment(const std::string& segment) {
    return !segment.empty() && segment != "." && segment != ".." &&
        std::all_of(segment.begin(), segment.end(), [](char character) {
            const auto value = static_cast<unsigned char>(character);
            return std::isalnum(value) || character == '-' || character == '_' || character == '.';
        });
}

bool MakeFsPath(const std::string& value, fs::FsPath& path) {
    if (value.empty() || value.size() >= sizeof(path.s)) {
        return false;
    }
    std::snprintf(path, sizeof(path), "%s", value.c_str());
    return true;
}

std::string JoinPath(const std::string& parent, const std::string& child) {
    if (parent.empty()) {
        return child;
    }
    if (child.empty()) {
        return parent;
    }
    return parent.back() == '/' ? parent + child : parent + "/" + child;
}

char FoldCharacter(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

bool WildcardMatch(std::string_view pattern, std::string_view value) {
    while (!pattern.empty()) {
        if (pattern.front() == '*') {
            while (!pattern.empty() && pattern.front() == '*') {
                pattern.remove_prefix(1);
            }
            if (pattern.empty()) {
                return true;
            }
            for (size_t index = 0; index <= value.size(); ++index) {
                if (WildcardMatch(pattern, value.substr(index))) {
                    return true;
                }
            }
            return false;
        }

        if (value.empty()) {
            return false;
        }
        if (pattern.front() == '?') {
            pattern.remove_prefix(1);
            value.remove_prefix(1);
            continue;
        }

        if (pattern.front() == '[') {
            const auto close = pattern.find(']');
            if (close != std::string_view::npos && close > 1) {
                auto choices = pattern.substr(1, close - 1);
                const bool negate = choices.front() == '!' || choices.front() == '^';
                if (negate) {
                    choices.remove_prefix(1);
                }
                bool matched = false;
                for (size_t index = 0; index < choices.size(); ++index) {
                    if (index + 2 < choices.size() && choices[index + 1] == '-') {
                        const auto current = FoldCharacter(value.front());
                        matched |= current >= FoldCharacter(choices[index]) &&
                            current <= FoldCharacter(choices[index + 2]);
                        index += 2;
                    } else {
                        matched |= FoldCharacter(value.front()) == FoldCharacter(choices[index]);
                    }
                }
                if (matched == negate) {
                    return false;
                }
                pattern.remove_prefix(close + 1);
                value.remove_prefix(1);
                continue;
            }
        }

        if (FoldCharacter(pattern.front()) != FoldCharacter(value.front())) {
            return false;
        }
        pattern.remove_prefix(1);
        value.remove_prefix(1);
    }
    return value.empty();
}

std::string BaseName(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string WithoutExtension(const std::string& filename) {
    const auto dot = filename.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? filename : filename.substr(0, dot);
}

std::string EncodeUrlPathSegment(const std::string& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) || character == '-' || character == '_' || character == '.' || character == '~') {
            encoded += character;
        } else {
            encoded += '%';
            encoded += hex[byte >> 4];
            encoded += hex[byte & 0x0F];
        }
    }
    return encoded;
}

bool FetchReleaseAssets(
    ui::ProgressBox* progress,
    const std::string& repo,
    const std::string& version,
    std::vector<ReleaseAsset>& assets) {
    std::string owner;
    std::string name;
    if (!ParseGitHubRepo(repo, owner, name) || version.empty()) {
        return false;
    }

    const auto url = "https://api.github.com/repos/" + owner + "/" + name +
        "/releases/tags/" + EncodeUrlPathSegment(version);
    const auto response = curl::Api().ToMemory(
        curl::Url{url},
        curl::Header{{"Accept", "application/vnd.github+json"}},
        curl::OnProgress{progress->OnDownloadProgressCallback()});
    if (!response.success || response.data.empty()) {
        return false;
    }

    auto* doc = yyjson_read(
        reinterpret_cast<const char*>(response.data.data()), response.data.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        return false;
    }
    auto* tag_value = yyjson_obj_get(root, "tag_name");
    auto* prerelease_value = yyjson_obj_get(root, "prerelease");
    auto* asset_values = yyjson_obj_get(root, "assets");
    if (!yyjson_is_str(tag_value) || !yyjson_is_bool(prerelease_value) ||
        yyjson_get_bool(prerelease_value) || !yyjson_is_arr(asset_values)) {
        return false;
    }

    const auto* tag = yyjson_get_str(tag_value);
    if (!tag || version != tag) {
        return false;
    }

    size_t index, max;
    yyjson_val* asset_value;
    yyjson_arr_foreach(asset_values, index, max, asset_value) {
        auto* name_value = yyjson_obj_get(asset_value, "name");
        auto* url_value = yyjson_obj_get(asset_value, "browser_download_url");
        if (!yyjson_is_str(name_value) || !yyjson_is_str(url_value)) {
            continue;
        }
        const auto* asset_name = yyjson_get_str(name_value);
        const auto* asset_url = yyjson_get_str(url_value);
        if (!asset_name || !*asset_name || !asset_url || !*asset_url || BaseName(asset_name) != asset_name) {
            continue;
        }
        assets.push_back({asset_name, asset_url});
    }
    return true;
}

Result ExtractZip(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& zip_path,
    const fs::FsPath& output_path) {
    return thread::TransferUnzipAll(
        progress, zip_path, filesystem, output_path,
        [&output_path](const fs::FsPath& name, fs::FsPath& path) {
            std::string relative;
            if (!NormalizeRelativePath(name.s, relative) || relative.empty()) {
                return false;
            }
            const bool directory = name.ends_with("/");
            auto output = JoinPath(output_path.s, relative);
            if (directory) {
                output += '/';
            }
            return MakeFsPath(output, path);
        });
}

Result ListFiles(fs::Fs* filesystem, const fs::FsPath& directory, std::vector<fs::FsPath>& files) {
    fs::Dir dir;
    R_TRY(filesystem->OpenDirectory(directory, FsDirOpenMode_ReadFiles | FsDirOpenMode_ReadDirs, &dir));
    std::vector<FsDirectoryEntry> entries;
    R_TRY(dir.ReadAll(entries));

    for (const auto& entry : entries) {
        const std::string name = entry.name;
        if (name == "." || name == "..") {
            continue;
        }
        fs::FsPath child;
        if (!MakeFsPath(JoinPath(directory.s, name), child)) {
            R_THROW(0x1);
        }
        if (entry.type == FsDirEntryType_Dir) {
            R_TRY(ListFiles(filesystem, child, files));
        } else if (entry.type == FsDirEntryType_File) {
            files.emplace_back(child);
        }
    }
    R_SUCCEED();
}

Result ListDirectories(fs::Fs* filesystem, const fs::FsPath& directory, std::vector<fs::FsPath>& directories) {
    fs::Dir dir;
    R_TRY(filesystem->OpenDirectory(directory, FsDirOpenMode_ReadDirs, &dir));
    std::vector<FsDirectoryEntry> entries;
    R_TRY(dir.ReadAll(entries));

    for (const auto& entry : entries) {
        const std::string name = entry.name;
        if (name == "." || name == ".." || entry.type != FsDirEntryType_Dir) {
            continue;
        }
        fs::FsPath child;
        if (!MakeFsPath(JoinPath(directory.s, name), child)) {
            R_THROW(0x1);
        }
        directories.emplace_back(child);
        R_TRY(ListDirectories(filesystem, child, directories));
    }
    R_SUCCEED();
}

bool GetRelativePath(const fs::FsPath& root, const fs::FsPath& path, std::string& relative) {
    const std::string root_string = root.s;
    const std::string path_string = path.s;
    if (path_string.size() <= root_string.size() ||
        path_string.compare(0, root_string.size(), root_string) != 0 ||
        path_string[root_string.size()] != '/') {
        return false;
    }
    return NormalizeRelativePath(path_string.substr(root_string.size() + 1), relative);
}

bool PathIsShared(
    const std::string& id,
    const std::string& path,
    const manifest::Manifest& active,
    const manifest::DisabledComponents& disabled) {
    const auto has_path = [&id, &path](const auto& components) {
        for (const auto& [other_id, component] : components) {
            if (other_id == id) {
                continue;
            }
            for (const auto& file : component.files) {
                std::string relative;
                if (NormalizeRelativePath(file, relative) && relative == path) {
                    return true;
                }
            }
        }
        return false;
    };
    return has_path(active.components) || has_path(disabled.components);
}

Result CopyFileToPath(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& source,
    const fs::FsPath& destination) {
    R_TRY(filesystem->CreateDirectoryRecursivelyWithPath(destination));
    return progress->CopyFile(filesystem, source, destination, true);
}

Result CopyDirectoryFiles(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& source_root,
    const fs::FsPath& destination_root) {
    std::vector<fs::FsPath> files;
    R_TRY(ListFiles(filesystem, source_root, files));
    for (const auto& source : files) {
        if (progress->ShouldExit()) {
            R_THROW(0x1);
        }
        std::string relative;
        if (!GetRelativePath(source_root, source, relative)) {
            R_THROW(0x1);
        }
        fs::FsPath destination;
        if (!MakeFsPath(JoinPath(destination_root.s, relative), destination)) {
            R_THROW(0x1);
        }
        R_TRY(CopyFileToPath(progress, filesystem, source, destination));
    }
    R_SUCCEED();
}

Result ProcessAsset(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const fs::FsPath& asset_path,
    const std::string& asset_name,
    const std::vector<catalog::ProcessingStep>& configured_steps,
    const fs::FsPath& output_root,
    const fs::FsPath& temporary_root,
    size_t asset_index) {
    std::vector<catalog::ProcessingStep> default_steps;
    const auto* steps = &configured_steps;
    if (steps->empty()) {
        default_steps.emplace_back();
        steps = &default_steps;
    }

    for (const auto& step : *steps) {
        if (progress->ShouldExit()) {
            R_THROW(0x1);
        }

        std::string target_relative;
        switch (step.type) {
            case catalog::StepType::UnzipToRoot: {
                R_TRY(ExtractZip(progress, filesystem, asset_path, output_root));
                break;
            }
            case catalog::StepType::UnzipToPath: {
                if (!NormalizeRelativePath(step.target_path, target_relative) || target_relative.empty()) {
                    R_THROW(0x1);
                }
                fs::FsPath destination;
                if (!MakeFsPath(JoinPath(output_root.s, target_relative), destination)) {
                    R_THROW(0x1);
                }
                R_TRY(ExtractZip(progress, filesystem, asset_path, destination));
                break;
            }
            case catalog::StepType::CopyFile:
            case catalog::StepType::CopyFileToAutoFolder: {
                if (!NormalizeRelativePath(step.target_path, target_relative) || target_relative.empty()) {
                    R_THROW(0x1);
                }
                const auto filename = BaseName(asset_name);
                if (!IsSafePathSegment(filename)) {
                    R_THROW(0x1);
                }
                if (step.type == catalog::StepType::CopyFileToAutoFolder) {
                    target_relative = JoinPath(target_relative, WithoutExtension(filename));
                }
                fs::FsPath destination;
                if (!MakeFsPath(JoinPath(JoinPath(output_root.s, target_relative), filename), destination)) {
                    R_THROW(0x1);
                }
                R_TRY(CopyFileToPath(progress, filesystem, asset_path, destination));
                break;
            }
            case catalog::StepType::FindAndRename: {
                if (step.source_file_pattern.empty() ||
                    !NormalizeRelativePath(step.target_path, target_relative) ||
                    !IsSafePathSegment(step.target_filename)) {
                    R_THROW(0x1);
                }
                std::vector<fs::FsPath> files;
                R_TRY(ListFiles(filesystem, output_root, files));
                bool found = false;
                for (const auto& source : files) {
                    const auto filename = BaseName(source.s);
                    std::string relative;
                    if (!GetRelativePath(output_root, source, relative) ||
                        (!WildcardMatch(step.source_file_pattern, filename) &&
                         !WildcardMatch(step.source_file_pattern, relative))) {
                        continue;
                    }
                    fs::FsPath destination;
                    if (!MakeFsPath(JoinPath(JoinPath(output_root.s, target_relative), step.target_filename), destination)) {
                        R_THROW(0x1);
                    }
                    if (fs::FsPath::path_equal(source.s, destination.s)) {
                        found = true;
                        break;
                    }
                    R_TRY(filesystem->CreateDirectoryRecursivelyWithPath(destination));
                    if (filesystem->FileExists(destination)) {
                        R_TRY(filesystem->DeleteFile(destination));
                    }
                    R_TRY(filesystem->RenameFile(source, destination));
                    found = true;
                    break;
                }
                if (!found) {
                    R_THROW(0x1);
                }
                break;
            }
            case catalog::StepType::DeleteFile: {
                if (!NormalizeRelativePath(step.target_path, target_relative) || target_relative.empty()) {
                    R_THROW(0x1);
                }
                std::vector<fs::FsPath> files;
                R_TRY(ListFiles(filesystem, output_root, files));
                std::vector<fs::FsPath> directories;
                R_TRY(ListDirectories(filesystem, output_root, directories));
                std::sort(directories.begin(), directories.end(), [](const auto& lhs, const auto& rhs) {
                    return lhs.size() > rhs.size();
                });
                for (const auto& file : files) {
                    std::string relative;
                    const auto filename = BaseName(file.s);
                    if (GetRelativePath(output_root, file, relative) &&
                        (WildcardMatch(target_relative, relative) ||
                         (target_relative.find('/') == std::string::npos &&
                          WildcardMatch(target_relative, filename)))) {
                        R_TRY(filesystem->DeleteFile(file));
                    }
                }
                for (const auto& directory : directories) {
                    std::string relative;
                    if (GetRelativePath(output_root, directory, relative) &&
                        (WildcardMatch(target_relative, relative) ||
                         (target_relative.find('/') == std::string::npos &&
                          WildcardMatch(target_relative, BaseName(directory.s)))) &&
                        filesystem->DirExists(directory)) {
                        R_TRY(filesystem->DeleteDirectoryRecursively(directory));
                    }
                }
                break;
            }
            case catalog::StepType::FindAndCopy: {
                if (step.source_file_pattern.empty() ||
                    !NormalizeRelativePath(step.target_path, target_relative) || target_relative.empty()) {
                    R_THROW(0x1);
                }
                fs::FsPath extracted;
                if (!MakeFsPath(JoinPath(temporary_root.s, "find-copy-" + std::to_string(asset_index)), extracted)) {
                    R_THROW(0x1);
                }
                if (filesystem->DirExists(extracted)) {
                    R_TRY(filesystem->DeleteDirectoryRecursively(extracted));
                }
                R_TRY(filesystem->CreateDirectoryRecursively(extracted));
                R_TRY(ExtractZip(progress, filesystem, asset_path, extracted));
                std::vector<fs::FsPath> files;
                R_TRY(ListFiles(filesystem, extracted, files));
                size_t copied = 0;
                for (const auto& source : files) {
                    std::string relative;
                    if (!GetRelativePath(extracted, source, relative) ||
                        (!WildcardMatch(step.source_file_pattern, BaseName(source.s)) &&
                         !WildcardMatch(step.source_file_pattern, relative))) {
                        continue;
                    }
                    fs::FsPath destination;
                    if (!MakeFsPath(JoinPath(JoinPath(output_root.s, target_relative), BaseName(source.s)), destination)) {
                        R_THROW(0x1);
                    }
                    R_TRY(CopyFileToPath(progress, filesystem, source, destination));
                    ++copied;
                }
                if (!copied) {
                    R_THROW(0x1);
                }
                break;
            }
            case catalog::StepType::UnzipSubfolderToPath: {
                std::string subfolder_pattern;
                const bool target_is_root = step.target_path.empty() ||
                    step.target_path == "sd:/" || step.target_path == "sd:\\";
                if (!NormalizeRelativePath(step.subfolder_name, subfolder_pattern) || subfolder_pattern.empty() ||
                    (!target_is_root && !NormalizeRelativePath(step.target_path, target_relative))) {
                    R_THROW(0x1);
                }
                if (target_is_root) {
                    target_relative.clear();
                }
                fs::FsPath extracted;
                if (!MakeFsPath(JoinPath(temporary_root.s, "subfolder-" + std::to_string(asset_index)), extracted)) {
                    R_THROW(0x1);
                }
                if (filesystem->DirExists(extracted)) {
                    R_TRY(filesystem->DeleteDirectoryRecursively(extracted));
                }
                R_TRY(filesystem->CreateDirectoryRecursively(extracted));
                R_TRY(ExtractZip(progress, filesystem, asset_path, extracted));

                std::vector<fs::FsPath> directories;
                R_TRY(ListDirectories(filesystem, extracted, directories));
                fs::FsPath source_directory;
                bool found = false;
                for (const auto& directory : directories) {
                    std::string relative;
                    if (GetRelativePath(extracted, directory, relative) &&
                        WildcardMatch(subfolder_pattern, relative)) {
                        source_directory = directory;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    R_THROW(0x1);
                }

                fs::FsPath destination_root;
                if (!MakeFsPath(JoinPath(output_root.s, target_relative), destination_root)) {
                    R_THROW(0x1);
                }
                R_TRY(CopyDirectoryFiles(progress, filesystem, source_directory, destination_root));
                break;
            }
        }
    }
    R_SUCCEED();
}

Result EnumerateStagedFiles(fs::Fs* filesystem, const fs::FsPath& output_root, std::vector<StagedFile>& files) {
    std::vector<fs::FsPath> paths;
    R_TRY(ListFiles(filesystem, output_root, paths));
    for (const auto& path : paths) {
        std::string relative;
        if (!GetRelativePath(output_root, path, relative)) {
            R_THROW(0x1);
        }
        files.push_back({relative, path});
    }
    std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.relative_path < rhs.relative_path;
    });
    R_SUCCEED();
}

bool ResetStagingArea(fs::Fs* filesystem) {
    constexpr fs::FsPath staging_path{"/config/hats-tools/custom-components/manager/temp"};
    if (filesystem->DirExists(staging_path) &&
        R_FAILED(filesystem->DeleteDirectoryRecursively(staging_path))) {
        return false;
    }
    return R_SUCCEEDED(filesystem->CreateDirectoryRecursively(staging_path));
}

bool InstallComponentFiles(
    ui::ProgressBox* progress,
    fs::Fs* filesystem,
    const std::string& id,
    const std::vector<StagedFile>& staged_files,
    const manifest::Manifest& active,
    const manifest::DisabledComponents& disabled,
    const manifest::Component& current_manifest_component,
    bool is_disabled,
    std::vector<std::string>& new_manifest_files,
    std::vector<FileMove>& backups,
    std::vector<fs::FsPath>& installed_files,
    const fs::FsPath& backup_root) {
    std::set<std::string> new_paths;
    for (const auto& file : staged_files) {
        new_paths.insert(file.relative_path);
    }

    for (const auto& old_file : current_manifest_component.files) {
        std::string relative;
        if (!NormalizeRelativePath(old_file, relative) || relative.empty()) {
            return false;
        }
        fs::FsPath original;
        if (is_disabled) {
            if (!IsSafePathSegment(id) ||
                !MakeFsPath(JoinPath(JoinPath(manifest::DISABLED_COMPONENTS_DIR_CUSTOM, id), relative), original)) {
                return false;
            }
        } else if (!MakeFsPath("/" + relative, original)) {
            return false;
        }
        if (filesystem->DirExists(original)) {
            new_manifest_files.push_back("/" + relative);
            continue;
        }
        if (!filesystem->FileExists(original)) {
            continue;
        }
        if (PathIsShared(id, relative, active, disabled)) {
            if (new_paths.contains(relative)) {
                return false;
            }
            new_manifest_files.push_back("/" + relative);
            continue;
        }

        fs::FsPath backup;
        if (!MakeFsPath(JoinPath(backup_root.s, "old-" + std::to_string(backups.size())), backup)) {
            return false;
        }
        if (R_FAILED(filesystem->RenameFile(original, backup))) {
            return false;
        }
        backups.push_back({original, backup});
    }

    for (const auto& staged : staged_files) {
        if (progress->ShouldExit()) {
            return false;
        }
        fs::FsPath destination;
        if (is_disabled) {
            if (!IsSafePathSegment(id) ||
                !MakeFsPath(JoinPath(JoinPath(manifest::DISABLED_COMPONENTS_DIR_CUSTOM, id), staged.relative_path), destination)) {
                return false;
            }
        } else if (!MakeFsPath("/" + staged.relative_path, destination)) {
            return false;
        }

        if (filesystem->FileExists(destination) || filesystem->DirExists(destination)) {
            return false;
        }
        installed_files.push_back(destination);
        if (R_FAILED(CopyFileToPath(progress, filesystem, staged.staged_path, destination))) {
            return false;
        }
        new_manifest_files.push_back("/" + staged.relative_path);
    }
    return true;
}

bool RollbackComponentFiles(
    fs::Fs* filesystem,
    const std::vector<FileMove>& backups,
    const std::vector<fs::FsPath>& installed_files) {
    bool success = true;
    for (auto it = installed_files.rbegin(); it != installed_files.rend(); ++it) {
        if (filesystem->FileExists(*it)) {
            success &= R_SUCCEEDED(filesystem->DeleteFile(*it));
        }
    }
    for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
        success &= R_SUCCEEDED(filesystem->CreateDirectoryRecursivelyWithPath(it->original));
        success &= R_SUCCEEDED(filesystem->RenameFile(it->backup, it->original));
    }
    return success;
}

Result RunUpdateSelectedComponents(
    ui::ProgressBox* progress,
    const std::set<std::string>& selected_ids,
    UpdateComponentsResult& result) {
    auto* app = App::GetApp();
    if (!app || !app->m_fs) {
        result.fatal_error = true;
        return 0x1;
    }
    auto* filesystem = app->m_fs;

    manifest::Manifest active;
    if (!manifest::load(active, true)) {
        result.fatal_error = true;
        return 0x1;
    }
    manifest::DisabledComponents disabled;
    if (!manifest::loadDisabled(disabled, true)) {
        result.fatal_error = true;
        return 0x1;
    }
    result.total = selected_ids.size();

    constexpr fs::FsPath temporary_root{"/config/hats-tools/custom-components/manager/temp"};
    constexpr fs::FsPath output_root{"/config/hats-tools/custom-components/manager/temp/output"};
    constexpr fs::FsPath backup_base{"/config/hats-tools/custom-components/manager/update-backup"};
    if (filesystem->DirExists(backup_base)) {
        bool empty = false;
        if (R_FAILED(filesystem->IsDirEmpty(backup_base, &empty)) || !empty ||
            R_FAILED(filesystem->DeleteDirectory(backup_base))) {
            result.fatal_error = true;
            return 0x1;
        }
    }

    size_t index = 0;
    for (const auto& id : selected_ids) {
        if (progress->ShouldExit()) {
            result.cancelled = true;
            break;
        }
        ++index;
        progress->NewTransfer("Preparing " + id + " (" + std::to_string(index) + "/" +
                              std::to_string(selected_ids.size()) + ")");
        if (!IsSafePathSegment(id)) {
            ++result.failed;
            continue;
        }

        auto active_it = active.components.find(id);
        auto disabled_it = disabled.components.find(id);
        const bool is_disabled = active_it == active.components.end() &&
            disabled_it != disabled.components.end();
        if (active_it == active.components.end() && !is_disabled) {
            ++result.failed;
            continue;
        }
        const auto old_manifest_component = is_disabled ? disabled_it->second : active_it->second;

        catalog::CustomComponent loaded_component;
        std::vector<catalog::AssetPattern> asset_patterns;
        if (!catalog::LoadComponent(id, loaded_component, asset_patterns)) {
            ++result.failed;
            continue;
        }
        if (loaded_component.source_type != "github_release" ||
            loaded_component.latest_version.empty() ||
            loaded_component.version == loaded_component.latest_version) {
            ++result.skipped;
            continue;
        }

        std::vector<ReleaseAsset> release_assets;
        if (!FetchReleaseAssets(progress, loaded_component.repo, loaded_component.latest_version, release_assets)) {
            if (progress->ShouldExit()) {
                result.cancelled = true;
                break;
            }
            ++result.failed;
            continue;
        }

        if (!ResetStagingArea(filesystem) || R_FAILED(filesystem->CreateDirectoryRecursively(output_root))) {
            ++result.failed;
            continue;
        }

        bool component_ready = true;
        size_t asset_index = 0;
        for (const auto& pattern : asset_patterns) {
            const auto match = std::find_if(release_assets.begin(), release_assets.end(), [&pattern](const auto& asset) {
                return WildcardMatch(pattern.pattern, asset.name);
            });
            if (match == release_assets.end()) {
                component_ready = false;
                break;
            }

            fs::FsPath downloaded_asset;
            const auto download_path = JoinPath(temporary_root.s, "asset-" + std::to_string(asset_index));
            if (!MakeFsPath(download_path, downloaded_asset)) {
                component_ready = false;
                break;
            }
            progress->NewTransfer("Downloading " + match->name);
            const auto download = curl::Api().ToFile(
                curl::Url{match->url},
                curl::Path{downloaded_asset},
                curl::OnProgress{progress->OnDownloadProgressCallback()});
            if (!download.success || progress->ShouldExit() ||
                R_FAILED(ProcessAsset(progress, filesystem, downloaded_asset, match->name,
                                      pattern.processing_steps, output_root, temporary_root, asset_index))) {
                component_ready = false;
                break;
            }
            ++asset_index;
        }

        std::vector<StagedFile> staged_files;
        if (component_ready &&
            (R_FAILED(EnumerateStagedFiles(filesystem, output_root, staged_files)) || staged_files.empty())) {
            component_ready = false;
        }
        if (!component_ready) {
            if (progress->ShouldExit()) {
                result.cancelled = true;
                break;
            }
            ++result.failed;
            continue;
        }

        fs::FsPath backup_root;
        if (!MakeFsPath(JoinPath(backup_base.s, id), backup_root) ||
            R_FAILED(filesystem->CreateDirectoryRecursively(backup_root))) {
            ++result.failed;
            continue;
        }

        const auto original_active = active;
        const auto original_disabled = disabled;
        const auto original_component = loaded_component;
        std::vector<std::string> new_manifest_files;
        std::vector<FileMove> backups;
        std::vector<fs::FsPath> installed_files;
        if (!InstallComponentFiles(progress, filesystem, id, staged_files, active, disabled,
                                   old_manifest_component, is_disabled, new_manifest_files,
                                   backups, installed_files, backup_root)) {
            if (!RollbackComponentFiles(filesystem, backups, installed_files)) {
                result.fatal_error = true;
                ++result.failed;
                break;
            }
            if (filesystem->DirExists(backup_root) &&
                R_FAILED(filesystem->DeleteDirectoryRecursively(backup_root))) {
                result.fatal_error = true;
                ++result.failed;
                break;
            }
            ++result.failed;
            if (progress->ShouldExit()) {
                result.cancelled = true;
                break;
            }
            continue;
        }

        auto& updated_manifest_component = is_disabled ? disabled.components.at(id) : active.components.at(id);
        updated_manifest_component.name = loaded_component.name;
        updated_manifest_component.version = loaded_component.latest_version;
        updated_manifest_component.category = loaded_component.category;
        updated_manifest_component.repo = loaded_component.repo;
        updated_manifest_component.files = std::move(new_manifest_files);
        loaded_component.version = loaded_component.latest_version;

        const bool manifest_saved = is_disabled
            ? manifest::saveDisabled(disabled, true)
            : manifest::save(active, true);
        const bool catalog_saved = manifest_saved &&
            catalog::UpdateComponent(id, loaded_component, asset_patterns);
        if (!manifest_saved || !catalog_saved) {
            active = original_active;
            disabled = original_disabled;
            if (is_disabled) {
                result.fatal_error |= !manifest::saveDisabled(disabled, true);
            } else {
                result.fatal_error |= !manifest::save(active, true);
            }
            result.fatal_error |= !catalog::UpdateComponent(id, original_component, asset_patterns);
            result.fatal_error |= !RollbackComponentFiles(filesystem, backups, installed_files);
            if (!result.fatal_error && filesystem->DirExists(backup_root)) {
                result.fatal_error |= R_FAILED(filesystem->DeleteDirectoryRecursively(backup_root));
            }
            ++result.failed;
            result.fatal_error = true;
            break;
        }

        if (filesystem->DirExists(backup_root) &&
            R_FAILED(filesystem->DeleteDirectoryRecursively(backup_root))) {
            result.fatal_error = true;
            ++result.failed;
            break;
        }
        ++result.updated;
    }

    if (!result.fatal_error && !ResetStagingArea(filesystem)) {
        result.fatal_error = true;
    }
    if (filesystem->DirExists(backup_base)) {
        bool empty = false;
        if (R_FAILED(filesystem->IsDirEmpty(backup_base, &empty)) || !empty ||
            R_FAILED(filesystem->DeleteDirectory(backup_base))) {
            result.fatal_error = true;
        }
    }
    if (result.cancelled) {
        return 0x1;
    }
    return result.failed || result.fatal_error ? 0x1 : 0;
}

Result RunFetchVersions(ui::ProgressBox* progress, FetchVersionsResult& result) {
    std::vector<catalog::GitHubVersionEntry> entries;
    if (!catalog::LoadGitHubVersionEntries(entries)) {
        result.fatal_error = true;
        return 0x1;
    }
    result.total = entries.size();

    std::map<std::string, std::string> latest_versions;
    std::map<std::string, std::string> previous_versions;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (progress->ShouldExit()) {
            result.cancelled = true;
            break;
        }

        const auto& entry = entries[i];
        progress->NewTransfer("Checking " + entry.repo + " (" +
                              std::to_string(i + 1) + "/" + std::to_string(entries.size()) + ")");
        std::string tag;
        if (!FetchLatestTag(progress, entry.repo, tag)) {
            if (progress->ShouldExit()) {
                result.cancelled = true;
                break;
            }
            ++result.failed;
            continue;
        }
        latest_versions.emplace(entry.id, std::move(tag));
        previous_versions.emplace(entry.id, entry.latest_version);
    }

    if (latest_versions.empty()) {
        return result.failed || result.cancelled ? 0x1 : 0;
    }

    manifest::Manifest custom_manifest;
    if (!manifest::load(custom_manifest, true)) {
        ++result.failed;
        result.fatal_error = true;
        return 0x1;
    }
    manifest::DisabledComponents disabled_components;
    if (!manifest::loadDisabled(disabled_components, true)) {
        ++result.failed;
        result.fatal_error = true;
        return 0x1;
    }

    const auto original_manifest = custom_manifest;
    const auto original_disabled = disabled_components;
    bool manifest_changed = false;
    bool disabled_changed = false;
    size_t matched_components = 0;
    for (const auto& [id, latest_version] : latest_versions) {
        if (auto it = custom_manifest.components.find(id); it != custom_manifest.components.end()) {
            const auto updated = FormatManifestVersion(it->second.version, latest_version);
            if (updated != it->second.version) {
                it->second.version = updated;
                manifest_changed = true;
            }
            ++matched_components;
        } else if (auto it = disabled_components.components.find(id); it != disabled_components.components.end()) {
            const auto updated = FormatManifestVersion(it->second.version, latest_version);
            if (updated != it->second.version) {
                it->second.version = updated;
                disabled_changed = true;
            }
            ++matched_components;
        } else {
            ++result.failed;
        }
    }

    if (manifest_changed && !manifest::save(custom_manifest, true)) {
        ++result.failed;
        result.fatal_error = true;
        (void)manifest::save(original_manifest, true);
        return 0x1;
    }
    if (disabled_changed && !manifest::saveDisabled(disabled_components, true)) {
        ++result.failed;
        result.fatal_error = true;
        if (manifest_changed) {
            (void)manifest::save(original_manifest, true);
        }
        (void)manifest::saveDisabled(original_disabled, true);
        return 0x1;
    }

    if (!catalog::SaveLatestVersions(latest_versions)) {
        ++result.failed;
        result.fatal_error = true;
        if (manifest_changed) {
            (void)manifest::save(original_manifest, true);
        }
        if (disabled_changed) {
            (void)manifest::saveDisabled(original_disabled, true);
        }
        (void)catalog::SaveLatestVersions(previous_versions);
        return 0x1;
    }

    result.updated = matched_components;
    return result.failed || result.cancelled ? 0x1 : 0;
}

} // namespace

bool FetchVersions(const std::map<std::string, manifest::Component>&) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    auto result = std::make_shared<FetchVersionsResult>();
    App::Push<ui::ProgressBox>(
        0, "Checking", "Custom component versions",
        [result](auto progress) -> Result {
            return RunFetchVersions(progress, *result);
        },
        [result](Result rc) {
            if (result->fatal_error) {
                App::Notify("Failed to fetch custom component versions");
            } else if (result->total == 0) {
                App::Notify("No GitHub custom components to check");
            } else if (R_SUCCEEDED(rc)) {
                App::Notify("Fetched versions for " + std::to_string(result->updated) + " custom component(s)");
            } else {
                App::Notify("Fetched versions for " + std::to_string(result->updated) + " of " +
                            std::to_string(result->total) + " component(s); " +
                            std::to_string(result->failed) + " failed" +
                            (result->cancelled ? " (cancelled)" : ""));
            }
        });
    return true;
}

bool UpdateAllComponents(const std::map<std::string, manifest::Component>&) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    App::Notify("Update All Components (Stub)");
    return true;
}

bool UpdateSelectedComponents(const std::set<std::string>& selected_ids) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    if (selected_ids.empty()) {
        App::Notify("Select at least one custom component to update");
        return false;
    }

    auto result = std::make_shared<UpdateComponentsResult>();
    App::Push<ui::ProgressBox>(
        0, "Updating", "Custom components",
        [result, selected_ids](auto progress) -> Result {
            return RunUpdateSelectedComponents(progress, selected_ids, *result);
        },
        [result](Result rc) {
            if (result->fatal_error) {
                App::Notify("Failed to update custom components");
            } else if (result->updated == 0 && result->failed == 0) {
                App::Notify("No selected custom components have an available update");
            } else if (R_SUCCEEDED(rc)) {
                App::Notify("Updated " + std::to_string(result->updated) +
                            " custom component(s); " + std::to_string(result->skipped) + " skipped");
            } else {
                App::Notify("Updated " + std::to_string(result->updated) + " of " +
                            std::to_string(result->total) + " component(s); " +
                            std::to_string(result->failed) + " failed, " +
                            std::to_string(result->skipped) + " skipped" +
                            (result->cancelled ? " (cancelled)" : ""));
            }
        });
    return true;
}

bool AddNewComponent() {
    if (auto* app = App::GetApp()) {
        app->m_touch_info.is_tap = false;
    }

    App::Push<ui::menu::hats::CustomComponentEditor>(
        catalog::CustomComponent{}, true, [](const ui::menu::hats::CustomComponentEditorDraft& draft) {
            auto component = draft.component;
            if (component.source_type == "direct_url") {
                component.processing_steps = draft.direct_url_steps;
            }

            std::vector<catalog::AssetPattern> assets;
            assets.reserve(draft.assets.size());
            for (const auto& asset : draft.assets) {
                assets.push_back({asset.pattern, asset.processing_steps});
            }

            if (!EnsureDirectories()) {
                App::Notify("Failed to prepare custom component directories");
                return false;
            }

            manifest::Manifest custom_manifest;
            if (manifest::exists(true) && !manifest::load(custom_manifest, true)) {
                App::Notify("Failed to load custom_manifest.json");
                return false;
            }
            if (custom_manifest.components.contains(component.id)) {
                App::Notify("Component ID already exists in custom_manifest.json");
                return false;
            }

            manifest::Component manifest_component;
            manifest_component.id = component.id;
            manifest_component.name = component.name;
            manifest_component.version = "not installed";
            manifest_component.category = component.category;
            manifest_component.repo = component.repo;
            manifest_component.files.clear();
            custom_manifest.components.emplace(manifest_component.id, std::move(manifest_component));

            if (!manifest::save(custom_manifest, true)) {
                App::Notify("Failed to save custom_manifest.json");
                return false;
            }

            if (!catalog::AddComponent(component, assets)) {
                custom_manifest.components.erase(component.id);
                if (!manifest::save(custom_manifest, true)) {
                    App::Notify("Failed to save components.json and rollback custom_manifest.json");
                    return false;
                }
                App::Notify("Failed to save components.json. The manifest entry was rolled back.");
                return false;
            }

            App::Notify("Custom component saved");
            return true;
        });
    return true;
}

bool ModifySelectedComponent(const std::string& component_id) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    catalog::CustomComponent component;
    std::vector<catalog::AssetPattern> assets;
    if (!catalog::LoadComponent(component_id, component, assets)) {
        App::Notify("Failed to load custom component from components.json");
        return false;
    }

    if (auto* app = App::GetApp()) {
        app->m_touch_info.is_tap = false;
    }

    std::vector<ui::menu::hats::CustomComponentEditorDraft::Asset> editor_assets;
    editor_assets.reserve(assets.size());
    for (const auto& asset : assets) {
        editor_assets.push_back({asset.pattern, asset.processing_steps});
    }

    App::Push<ui::menu::hats::CustomComponentEditor>(
        std::move(component), false, [component_id](const ui::menu::hats::CustomComponentEditorDraft& draft) {
            auto updated_component = draft.component;
            if (updated_component.source_type == "direct_url") {
                updated_component.processing_steps = draft.direct_url_steps;
            }

            std::vector<catalog::AssetPattern> updated_assets;
            updated_assets.reserve(draft.assets.size());
            for (const auto& asset : draft.assets) {
                updated_assets.push_back({asset.pattern, asset.processing_steps});
            }

            manifest::Manifest custom_manifest;
            if (!manifest::load(custom_manifest, true)) {
                App::Notify("Failed to load custom_manifest.json");
                return false;
            }
            const auto manifest_component = custom_manifest.components.find(component_id);
            if (manifest_component == custom_manifest.components.end()) {
                App::Notify("Custom component was not found in custom_manifest.json");
                return false;
            }
            if (updated_component.id != component_id &&
                custom_manifest.components.contains(updated_component.id)) {
                App::Notify("Component ID already exists in custom_manifest.json");
                return false;
            }

            const auto original_manifest_component = manifest_component->second;
            auto updated_manifest_component = original_manifest_component;
            updated_manifest_component.id = updated_component.id;
            updated_manifest_component.name = updated_component.name;
            updated_manifest_component.category = updated_component.category;
            updated_manifest_component.repo = updated_component.repo;
            custom_manifest.components.erase(manifest_component);
            custom_manifest.components.emplace(updated_manifest_component.id, std::move(updated_manifest_component));

            if (!manifest::save(custom_manifest, true)) {
                App::Notify("Failed to save custom_manifest.json");
                return false;
            }

            if (!catalog::UpdateComponent(component_id, updated_component, updated_assets)) {
                custom_manifest.components.erase(updated_component.id);
                custom_manifest.components.emplace(component_id, original_manifest_component);
                if (!manifest::save(custom_manifest, true)) {
                    App::Notify("Failed to save components.json and rollback custom_manifest.json");
                    return false;
                }
                App::Notify("Failed to save components.json. The custom manifest update was rolled back.");
                return false;
            }

            App::Notify("Custom component updated");
            return true;
        }, std::move(editor_assets));
    return true;
}

bool EnsureDirectories() {
    constexpr fs::FsPath paths[]{
        "/config/hats-tools/custom-components/manager/",
        "/config/hats-tools/custom-components/manager/temp",
    };

    auto* app = App::GetApp();
    if (!app || !app->m_fs) {
        return false;
    }

    for (const auto& path : paths) {
        if (!app->m_fs->DirExists(path)) {
            if (const auto rc = app->m_fs->CreateDirectoryRecursively(path); R_FAILED(rc)) {
                App::PushErrorBox(rc, "Failed to create custom component directory");
                return false;
            }
        }
    }

    return true;
}

bool CleanStagingArea() {
    constexpr fs::FsPath staging_path{"/config/hats-tools/custom-components/manager/temp"};

    auto* app = App::GetApp();
    if (!app || !app->m_fs) {
        return false;
    }

    if (app->m_fs->DirExists(staging_path)) {
        if (const auto rc = app->m_fs->DeleteDirectoryRecursively(staging_path); R_FAILED(rc)) {
            App::PushErrorBox(rc, "Failed to clean custom component staging directory");
            return false;
        }
    }

    if (const auto rc = app->m_fs->CreateDirectoryRecursively(staging_path); R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to recreate custom component staging directory");
        return false;
    }

    return true;
}

bool EnsureCatalogExists() {
    constexpr u8 empty_catalog[]{'{', '}', '\n'};
    constexpr fs::FsPath catalog_path{catalog::CATALOG_PATH};

    auto* app = App::GetApp();
    if (!app || !app->m_fs) {
        return false;
    }

    if (app->m_fs->FileExists(catalog_path)) {
        return true;
    }

    if (!EnsureDirectories()) {
        return false;
    }

    if (const auto rc = app->m_fs->write_entire_file(catalog_path, empty_catalog); R_FAILED(rc)) {
        App::PushErrorBox(rc, "Failed to create custom component catalog");
        return false;
    }

    return true;
}

} // namespace sphaira::custom
