#include "custom_components.hpp"

#include "app.hpp"
#include "custom_catalog.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "ui/menus/custom_component_editor.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <string_view>
#include <utility>
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
