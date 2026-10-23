#include "custom_catalog.hpp"

#include "custom_component_storage.hpp"
#include "custom_catalog_json.hpp"

#include <utility>
#include <yyjson.h>

namespace sphaira::custom::catalog {



bool LoadCatalog(Catalog& out) {
    if (!::sphaira::custom::storage::EnsureCatalogExists()) {
        return false;
    }

    auto* doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        return false;
    }

    Catalog catalog;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        const auto* id = yyjson_get_str(key);
        auto* value = yyjson_obj_iter_get_val(key);
        if (!id || !value) {
            return false;
        }

        CustomComponent component;
        component.id = id;
        if (!json::ReadComponent(value, component)) {
            return false;
        }
        catalog.components.emplace(component.id, std::move(component));
    }

    out = std::move(catalog);
    return true;
}

bool SaveCatalog(const Catalog& catalog) {
    auto* doc = yyjson_mut_doc_new(nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_obj(doc);
    if (!root) {
        return false;
    }
    yyjson_mut_doc_set_root(doc, root);

    for (const auto& [id, component] : catalog.components) {
        auto* value = json::ComponentToJson(doc, component);
        if (!value || !yyjson_mut_obj_add_val(doc, root, id.c_str(), value)) {
            return false;
        }
    }

    if (!::sphaira::custom::storage::EnsureDirectories()) {
        return false;
    }

    yyjson_write_err err{};
    const bool success = yyjson_mut_write_file(CATALOG_PATH, doc, YYJSON_WRITE_PRETTY, nullptr, &err);
    if (!success) {
        return false;
    }

    return true;
}

CustomComponent* FindComponent(Catalog& catalog, const std::string& id) {
    const auto it = catalog.components.find(id);
    return it != catalog.components.end() ? &it->second : nullptr;
}

const CustomComponent* FindComponent(const Catalog& catalog, const std::string& id) {
    const auto it = catalog.components.find(id);
    return it != catalog.components.end() ? &it->second : nullptr;
}

bool UpsertComponent(Catalog& catalog, CustomComponent component) {
    if (component.id.empty()) {
        return false;
    }
    const auto id = component.id;
    catalog.components.insert_or_assign(id, std::move(component));
    return true;
}

bool RemoveComponent(Catalog& catalog, const std::string& id) {
    return catalog.components.erase(id) != 0;
}

bool AddComponent(const CustomComponent& component, const std::vector<AssetPattern>& assets) {
    if (component.id.empty() ||
        (component.source_type != "github_release" && component.source_type != "direct_url") ||
        (component.source_type == "github_release" && assets.empty())) {
        return false;
    }

    if (!::sphaira::custom::storage::EnsureCatalogExists()) {
        return false;
    }

    auto* existing_doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!existing_doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(existing_doc));

    auto* existing_root = yyjson_doc_get_root(existing_doc);
    if (!yyjson_is_obj(existing_root)) {
        return false;
    }
    if (yyjson_obj_get(existing_root, component.id.c_str())) {
        return false;
    }

    auto* doc = yyjson_doc_mut_copy(existing_doc, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_doc_get_root(doc);
    auto* value = json::NewComponentToJson(doc, component, assets);
    if (!value || !yyjson_mut_obj_add_val(doc, root, component.id.c_str(), value)) {
        return false;
    }

    if (!::sphaira::custom::storage::EnsureDirectories()) {
        return false;
    }

    yyjson_write_err err{};
    if (!yyjson_mut_write_file(CATALOG_PATH, doc, YYJSON_WRITE_PRETTY, nullptr, &err)) {
        return false;
    }
    return true;
}

bool LoadComponent(const std::string& id, CustomComponent& component, std::vector<AssetPattern>& assets) {
    auto* doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        return false;
    }

    auto* value = yyjson_obj_get(root, id.c_str());
    if (!value) {
        return false;
    }

    CustomComponent loaded_component;
    std::vector<AssetPattern> loaded_assets;
    if (!json::ReadEditableComponent(id.c_str(), value, loaded_component, loaded_assets)) {
        return false;
    }
    component = std::move(loaded_component);
    assets = std::move(loaded_assets);
    return true;
}

bool UpdateComponent(
    const std::string& id,
    const CustomComponent& component,
    const std::vector<AssetPattern>& assets) {
    if (id.empty() || component.id.empty() ||
        (component.source_type != "github_release" && component.source_type != "direct_url") ||
        (component.source_type == "github_release" && assets.empty())) {
        return false;
    }

    auto* existing_doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!existing_doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(existing_doc));

    auto* existing_root = yyjson_doc_get_root(existing_doc);
    if (!yyjson_is_obj(existing_root) || !yyjson_obj_get(existing_root, id.c_str())) {
        return false;
    }
    if (id != component.id && yyjson_obj_get(existing_root, component.id.c_str())) {
        return false;
    }

    auto* doc = yyjson_doc_mut_copy(existing_doc, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_obj_remove_str(root, id.c_str());
    auto* value = json::NewComponentToJson(doc, component, assets);
    if (!value || !yyjson_mut_obj_add_val(doc, root, component.id.c_str(), value)) {
        return false;
    }

    yyjson_write_err err{};
    if (!yyjson_mut_write_file(CATALOG_PATH, doc, YYJSON_WRITE_PRETTY, nullptr, &err)) {
        return false;
    }
    return true;
}

bool RemoveComponentsFromCatalog(const std::vector<std::string>& ids) {
    if (ids.empty()) {
        return true;
    }

    auto* existing_doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!existing_doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(existing_doc));

    auto* existing_root = yyjson_doc_get_root(existing_doc);
    if (!yyjson_is_obj(existing_root)) {
        return false;
    }

    auto* doc = yyjson_doc_mut_copy(existing_doc, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_doc_get_root(doc);
    for (const auto& id : ids) {
        yyjson_mut_obj_remove_str(root, id.c_str());
    }

    yyjson_write_err err{};
    if (!yyjson_mut_write_file(CATALOG_PATH, doc, YYJSON_WRITE_PRETTY, nullptr, &err)) {
        return false;
    }
    return true;
}

bool LoadGitHubVersionEntries(std::vector<GitHubVersionEntry>& entries) {
    if (!::sphaira::custom::storage::EnsureCatalogExists()) {
        return false;
    }

    auto* doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        return false;
    }

    std::vector<GitHubVersionEntry> loaded_entries;
    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        const auto* id = yyjson_get_str(key);
        auto* value = yyjson_obj_iter_get_val(key);
        if (!id || !yyjson_is_obj(value)) {
            return false;
        }

        std::string source_type;
        if (!json::ReadString(value, "source_type", source_type)) {
            return false;
        }
        if (source_type != "github_release") {
            continue;
        }

        GitHubVersionEntry entry;
        entry.id = id;
        auto* asset_info = yyjson_obj_get(value, "asset_info");
        if (!json::ReadString(value, "repo", entry.repo) ||
            !asset_info || !yyjson_is_obj(asset_info) ||
            !json::ReadString(asset_info, "latest_version", entry.latest_version)) {
            return false;
        }
        loaded_entries.emplace_back(std::move(entry));
    }

    entries = std::move(loaded_entries);
    return true;
}

bool SaveLatestVersions(const std::map<std::string, std::string>& versions) {
    if (versions.empty()) {
        return true;
    }

    auto* existing_doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!existing_doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(existing_doc));

    auto* existing_root = yyjson_doc_get_root(existing_doc);
    if (!yyjson_is_obj(existing_root)) {
        return false;
    }

    struct VersionMetadata {
        bool has_version{};
        std::string version;
    };
    std::map<std::string, VersionMetadata> metadata;
    for (const auto& [id, _] : versions) {
        auto* component = yyjson_obj_get(existing_root, id.c_str());
        if (!yyjson_is_obj(component)) {
            return false;
        }

        VersionMetadata entry;
        auto* asset_info = yyjson_obj_get(component, "asset_info");
        if (asset_info) {
            if (!yyjson_is_obj(asset_info)) {
                return false;
            }
            auto* current_version = yyjson_obj_get(asset_info, "version");
            if (current_version) {
                if (!yyjson_is_str(current_version)) {
                    return false;
                }
                entry.has_version = true;
                entry.version = yyjson_get_str(current_version);
            }
        } else {
            auto* current_version = yyjson_obj_get(component, "version");
            if (current_version && yyjson_is_str(current_version)) {
                entry.has_version = true;
                entry.version = yyjson_get_str(current_version);
            }
        }
        metadata.emplace(id, std::move(entry));
    }

    auto* doc = yyjson_doc_mut_copy(existing_doc, nullptr);
    if (!doc) {
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_doc_get_root(doc);
    for (const auto& [id, version] : versions) {
        auto* component = yyjson_mut_obj_get(root, id.c_str());
        auto* asset_info = yyjson_mut_obj_get(component, "asset_info");
        if (!asset_info) {
            asset_info = yyjson_mut_obj(doc);
            if (!asset_info) {
                return false;
            }
            const auto& old_metadata = metadata.at(id);
            if (old_metadata.has_version) {
                if (!yyjson_mut_obj_add_str(doc, asset_info, "version", old_metadata.version.c_str())) {
                    return false;
                }
            }
            if (!yyjson_mut_obj_add_val(doc, component, "asset_info", asset_info)) {
                return false;
            }
        }

        yyjson_mut_obj_remove_str(asset_info, "latest_version");
        if (!yyjson_mut_obj_add_str(doc, asset_info, "latest_version", version.c_str())) {
            return false;
        }
    }

    yyjson_write_err err{};
    if (!yyjson_mut_write_file(CATALOG_PATH, doc, YYJSON_WRITE_PRETTY, nullptr, &err)) {
        return false;
    }
    return true;
}

} // namespace sphaira::custom::catalog
