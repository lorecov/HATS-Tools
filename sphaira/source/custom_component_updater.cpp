#include "custom_component_updater.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "fs.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace sphaira::custom::updater {
namespace {

struct FileMove {
    fs::FsPath original;
    fs::FsPath backup;
    bool is_directory{};
};

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
    // Archive paths must not escape the staging or destination root.
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
                if (NormalizeRelativePath(file, relative) &&
                    fs::FsPath::path_equal(relative, path)) {
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
        // Assets without explicit steps are treated as archives extracted to the root.
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

bool GetComponentDestination(
    const PreparedComponentUpdate& prepared_update,
    const std::string& relative,
    fs::FsPath& path) {
    if (prepared_update.is_disabled) {
        return MakeFsPath(
            JoinPath(JoinPath(manifest::DISABLED_COMPONENTS_DIR_CUSTOM, prepared_update.id), relative), path);
    }
    return MakeFsPath("/" + relative, path);
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
    const std::set<std::string>& approved_conflicts,
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
        backups.push_back({original, backup, false});
    }

    // Move approved collisions aside before copying any staged files.
    for (const auto& conflict : approved_conflicts) {
        if (PathIsShared(id, conflict, active, disabled)) {
            return false;
        }
        fs::FsPath original;
        if (is_disabled) {
            if (!MakeFsPath(JoinPath(JoinPath(manifest::DISABLED_COMPONENTS_DIR_CUSTOM, id), conflict), original)) {
                return false;
            }
        } else if (!MakeFsPath("/" + conflict, original)) {
            return false;
        }
        const bool is_directory = filesystem->DirExists(original);
        if (!is_directory && !filesystem->FileExists(original)) {
            continue;
        }
        fs::FsPath backup;
        if (!MakeFsPath(JoinPath(backup_root.s, "old-" + std::to_string(backups.size())), backup)) {
            return false;
        }
        const auto rc = is_directory
            ? filesystem->RenameDirectory(original, backup)
            : filesystem->RenameFile(original, backup);
        if (R_FAILED(rc)) {
            return false;
        }
        backups.push_back({original, backup, is_directory});
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
    // Remove newly installed files first, then restore backups in reverse order.
    for (auto it = installed_files.rbegin(); it != installed_files.rend(); ++it) {
        if (filesystem->FileExists(*it)) {
            success &= R_SUCCEEDED(filesystem->DeleteFile(*it));
        }
    }
    for (auto it = backups.rbegin(); it != backups.rend(); ++it) {
        success &= R_SUCCEEDED(filesystem->CreateDirectoryRecursivelyWithPath(it->original));
        success &= R_SUCCEEDED(it->is_directory
            ? filesystem->RenameDirectory(it->backup, it->original)
            : filesystem->RenameFile(it->backup, it->original));
    }
    return success;
}

Result InstallPreparedComponentImpl(
    ui::ProgressBox* progress,
    PreparedComponentUpdate& prepared_update,
    const std::vector<std::string>& approved_conflicts) {
    auto* app = App::GetApp();
    if (!app || !app->m_fs) {
        return 0x1;
    }
    auto* filesystem = app->m_fs.get();

    manifest::Manifest active;
    manifest::DisabledComponents disabled;
    if (!manifest::load(active, true) || !manifest::loadDisabled(disabled, true)) {
        return 0x1;
    }

    constexpr fs::FsPath backup_base{"/config/hats-tools/custom-components/manager/update-backup"};
    if (!filesystem->DirExists(backup_base) &&
        R_FAILED(filesystem->CreateDirectoryRecursively(backup_base))) {
        return 0x1;
    }
    fs::FsPath backup_root;
    if (!MakeFsPath(JoinPath(backup_base.s, prepared_update.id), backup_root) ||
        filesystem->DirExists(backup_root) ||
        R_FAILED(filesystem->CreateDirectoryRecursively(backup_root))) {
        return 0x1;
    }

    catalog::CustomComponent original_component;
    std::vector<catalog::AssetPattern> current_assets;
    if (!catalog::LoadComponent(prepared_update.id, original_component, current_assets)) {
        filesystem->DeleteDirectoryRecursively(backup_root);
        return 0x1;
    }

    auto manifest_entry = prepared_update.is_disabled
        ? disabled.components.find(prepared_update.id)
        : active.components.find(prepared_update.id);
    if ((prepared_update.is_disabled && manifest_entry == disabled.components.end()) ||
        (!prepared_update.is_disabled && manifest_entry == active.components.end())) {
        filesystem->DeleteDirectoryRecursively(backup_root);
        return 0x1;
    }
    const auto previous_active = active;
    const auto previous_disabled = disabled;
    const auto previous_manifest_component = manifest_entry->second;

    std::vector<std::string> new_manifest_files;
    std::vector<FileMove> backups;
    std::vector<fs::FsPath> installed_files;
    const std::set<std::string> approved(approved_conflicts.begin(), approved_conflicts.end());
    if (!InstallComponentFiles(progress, filesystem, prepared_update.id, prepared_update.staged_files,
                               active, disabled, previous_manifest_component,
                               prepared_update.is_disabled, approved, new_manifest_files,
                               backups, installed_files, backup_root)) {
        const bool restored = RollbackComponentFiles(filesystem, backups, installed_files);
        if (!restored) {
            return 0x2;
        }
        const auto cleanup = filesystem->DeleteDirectoryRecursively(backup_root);
        return R_SUCCEEDED(cleanup) ? 0x1 : 0x2;
    }

    auto& updated_component = prepared_update.is_disabled
        ? disabled.components.at(prepared_update.id)
        : active.components.at(prepared_update.id);
    updated_component.name = prepared_update.component.name;
    updated_component.version = prepared_update.component.latest_version;
    updated_component.category = prepared_update.component.category;
    updated_component.repo = prepared_update.component.repo;
    updated_component.files = std::move(new_manifest_files);
    prepared_update.component.version = prepared_update.component.latest_version;

    const bool manifest_saved = prepared_update.is_disabled
        ? manifest::saveDisabled(disabled, true)
        : manifest::save(active, true);
    const bool catalog_saved = manifest_saved &&
        catalog::UpdateComponent(prepared_update.id, prepared_update.component, prepared_update.assets);
    if (!manifest_saved || !catalog_saved) {
        bool restored = prepared_update.is_disabled
            ? manifest::saveDisabled(previous_disabled, true)
            : manifest::save(previous_active, true);
        restored &= catalog::UpdateComponent(prepared_update.id, original_component, current_assets);
        restored &= RollbackComponentFiles(filesystem, backups, installed_files);
        if (!restored) {
            return 0x2;
        }
        const auto cleanup = filesystem->DeleteDirectoryRecursively(backup_root);
        return R_SUCCEEDED(cleanup) ? 0x1 : 0x2;
    }

    if (R_FAILED(filesystem->DeleteDirectoryRecursively(backup_root))) {
        return 0x2;
    }
    return 0;
}



} // namespace

Result ProcessDownloadedComponent(
    ui::ProgressBox* progress,
    const std::string& id,
    const std::vector<DownloadedAsset>& downloaded_assets,
    PreparedComponentUpdate& prepared) {
    auto* app = App::GetApp();
    if (!progress || !app || !app->m_fs || !IsSafePathSegment(id)) {
        return 0x1;
    }
    auto* filesystem = app->m_fs.get();
    prepared.id = id;

    manifest::Manifest active;
    manifest::DisabledComponents disabled;
    if (!manifest::load(active, true) || !manifest::loadDisabled(disabled, true)) {
        return 0x1;
    }
    const auto active_it = active.components.find(id);
    const auto disabled_it = disabled.components.find(id);
    prepared.is_disabled = active_it == active.components.end() && disabled_it != disabled.components.end();
    if (active_it == active.components.end() && !prepared.is_disabled) {
        return 0x1;
    }
    prepared.manifest_component = prepared.is_disabled ? disabled_it->second : active_it->second;
    if (!catalog::LoadComponent(id, prepared.component, prepared.assets) ||
        prepared.component.source_type != "github_release" ||
        prepared.component.latest_version.empty()) {
        return 0x1;
    }

    constexpr fs::FsPath temporary_root{"/config/hats-tools/custom-components/manager/temp"};
    constexpr fs::FsPath output_root{"/config/hats-tools/custom-components/manager/temp/output"};
    if (filesystem->DirExists(output_root) &&
        R_FAILED(filesystem->DeleteDirectoryRecursively(output_root))) {
        return 0x1;
    }
    if (R_FAILED(filesystem->CreateDirectoryRecursively(output_root))) {
        return 0x1;
    }

    size_t asset_index = 0;
    for (const auto& downloaded : downloaded_assets) {
        if (progress->ShouldExit()) {
            return 0x1;
        }
        const auto pattern = std::find_if(prepared.assets.begin(), prepared.assets.end(), [&downloaded](const auto& asset) {
            return asset.pattern == downloaded.pattern;
        });
        if (pattern == prepared.assets.end() || downloaded.name.empty() ||
            !filesystem->FileExists(downloaded.path)) {
            return 0x1;
        }
        if (R_FAILED(ProcessAsset(progress, filesystem, downloaded.path, downloaded.name,
                                  pattern->processing_steps, output_root, temporary_root, asset_index++))) {
            return 0x1;
        }
    }
    if (downloaded_assets.size() != prepared.assets.size() ||
        R_FAILED(EnumerateStagedFiles(filesystem, output_root, prepared.staged_files)) ||
        prepared.staged_files.empty()) {
        return 0x1;
    }

    std::set<std::string> owned_paths;
    for (const auto& file : prepared.manifest_component.files) {
        std::string relative;
        if (!NormalizeRelativePath(file, relative)) {
            return 0x1;
        }
        owned_paths.insert(relative);
    }
    for (const auto& staged : prepared.staged_files) {
        fs::FsPath destination;
        if (!GetComponentDestination(prepared, staged.relative_path, destination)) {
            return 0x1;
        }
        if (!filesystem->FileExists(destination) && !filesystem->DirExists(destination)) {
            continue;
        }
        if (owned_paths.contains(staged.relative_path)) {
            continue;
        }
        if (PathIsShared(id, staged.relative_path, active, disabled)) {
            return 0x1;
        }
        prepared.conflicts.push_back(staged.relative_path);
    }
    return 0;
}

Result InstallPreparedComponent(
    ui::ProgressBox* progress,
    PreparedComponentUpdate& prepared,
    const std::vector<std::string>& approved_conflicts) {
    if (!progress) {
        return 0x1;
    }
    return InstallPreparedComponentImpl(progress, prepared, approved_conflicts);
}

bool ApplyFetchedVersions(
    const std::map<std::string, std::string>& latest_versions,
    const std::map<std::string, std::string>& previous_versions,
    size_t& updated_components) {
    updated_components = 0;
    if (latest_versions.empty()) {
        return true;
    }

    manifest::Manifest custom_manifest;
    if (!manifest::load(custom_manifest, true)) {
        return false;
    }
    manifest::DisabledComponents disabled_components;
    if (!manifest::loadDisabled(disabled_components, true)) {
        return false;
    }

    const auto original_manifest = custom_manifest;
    const auto original_disabled = disabled_components;
    bool manifest_changed = false;
    bool disabled_changed = false;
    for (const auto& [id, latest_version] : latest_versions) {
        if (auto it = custom_manifest.components.find(id); it != custom_manifest.components.end()) {
            const auto updated = FormatManifestVersion(it->second.version, latest_version);
            if (updated != it->second.version) {
                it->second.version = updated;
                manifest_changed = true;
            }
            ++updated_components;
        } else if (auto it = disabled_components.components.find(id); it != disabled_components.components.end()) {
            const auto updated = FormatManifestVersion(it->second.version, latest_version);
            if (updated != it->second.version) {
                it->second.version = updated;
                disabled_changed = true;
            }
            ++updated_components;
        }
    }

    // Restore saved manifests if a later catalog write fails.
    if (manifest_changed && !manifest::save(custom_manifest, true)) {
        (void)manifest::save(original_manifest, true);
        return false;
    }
    if (disabled_changed && !manifest::saveDisabled(disabled_components, true)) {
        if (manifest_changed) {
            (void)manifest::save(original_manifest, true);
        }
        (void)manifest::saveDisabled(original_disabled, true);
        return false;
    }
    if (!catalog::SaveLatestVersions(latest_versions)) {
        if (manifest_changed) {
            (void)manifest::save(original_manifest, true);
        }
        if (disabled_changed) {
            (void)manifest::saveDisabled(original_disabled, true);
        }
        (void)catalog::SaveLatestVersions(previous_versions);
        return false;
    }
    updated_components = latest_versions.size();
    return true;
}

} // namespace sphaira::custom::updater
