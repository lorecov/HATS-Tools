#include "custom_component_storage.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "fs.hpp"

namespace sphaira::custom::storage {

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

} // namespace sphaira::custom::storage
