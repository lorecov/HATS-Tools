#include "custom_component_updater.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "custom_component_update_utils.hpp"
#include "fs.hpp"
#include "threaded_file_transfer.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace sphaira::custom::updater {

using update_detail::DownloadedAsset;
using update_detail::PreparedComponentUpdate;
using update_detail::StagedFile;
using namespace update_detail;

namespace {

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

} // namespace sphaira::custom::updater
