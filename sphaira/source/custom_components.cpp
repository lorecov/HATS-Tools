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
                    log_write("[CUSTOM_COMPONENTS] failed to rollback custom manifest update for '%s'\n", component_id.c_str());
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
