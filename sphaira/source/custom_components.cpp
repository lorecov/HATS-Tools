#include "custom_components.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "fs.hpp"
#include "log.hpp"
#include "ui/menus/custom_component_editor.hpp"

#include <utility>

namespace sphaira::custom {

bool FetchVersions(const std::map<std::string, manifest::Component>&) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    App::Notify("Fetch Versions (Stub)");
    return true;
}

bool UpdateAllComponents(const std::map<std::string, manifest::Component>&) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    App::Notify("Update All Components (Stub)");
    return true;
}

bool UpdateSelectedComponents(const std::set<std::string>&) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    App::Notify("Update Selected Components (Stub)");
    return true;
}

bool AddNewComponent() {
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
                    log_write("[CUSTOM_COMPONENTS] failed to rollback custom manifest entry '%s'\n", component.id.c_str());
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

bool ModifySelectedComponent(const std::string&) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }

    App::Notify("Modify Selected Component (Stub)");
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
