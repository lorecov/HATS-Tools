#include "custom_component_updater.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "custom_component_update_utils.hpp"
#include "fs.hpp"
#include "ui/progress_box.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::custom::updater {

using update_detail::PreparedComponentUpdate;
using namespace update_detail;

namespace {

struct FileMove {
    fs::FsPath original;
    fs::FsPath backup;
    bool is_directory{};
};

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

Result InstallPreparedComponent(
    ui::ProgressBox* progress,
    PreparedComponentUpdate& prepared,
    const std::vector<std::string>& approved_conflicts) {
    if (!progress) {
        return 0x1;
    }
    return InstallPreparedComponentImpl(progress, prepared, approved_conflicts);
}

} // namespace sphaira::custom::updater
