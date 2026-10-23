#include "custom_components.hpp"

#include "custom_component_update_workflow.hpp"
#include "custom_component_storage.hpp"
#include "app.hpp"
#include "custom_catalog.hpp"
#include "ui/menus/custom_component_editor.hpp"

#include <utility>
#include <vector>

namespace sphaira::custom {

bool FetchVersions(const std::map<std::string, manifest::Component>&) {
    return workflow::FetchVersions();
}

bool UpdateAllComponents(const std::map<std::string, manifest::Component>& components) {
    return workflow::UpdateAllComponents(components);
}

bool UpdateSelectedComponents(const std::set<std::string>& component_ids) {
    return workflow::UpdateSelectedComponents(component_ids);
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
    return storage::EnsureDirectories();
}

bool CleanStagingArea() {
    return storage::CleanStagingArea();
}

bool EnsureCatalogExists() {
    return storage::EnsureCatalogExists();
}

} // namespace sphaira::custom
