#include "custom_components.hpp"

#include "custom_component_updater.hpp"
#include "app.hpp"
#include "custom_catalog.hpp"
#include "download.hpp"
#include "fs.hpp"
#include "ui/menus/custom_component_editor.hpp"
#include "ui/option_box.hpp"
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

struct ReleaseAsset {
    std::string name;
    std::string url;
};

struct FetchVersionsResult {
    size_t total{};
    size_t updated{};
    size_t failed{};
    bool cancelled{};
    bool fatal_error{};
};

struct UpdateOperation {
    std::vector<std::string> ids;
    size_t next_id{};
    size_t updated{};
    size_t failed{};
    size_t skipped{};
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

bool FetchLatestTag(ui::ProgressBox* progress, const std::string& repo, std::string& tag) {
    std::string owner;
    std::string name;
    if (!ParseGitHubRepo(repo, owner, name)) {
        return false;
    }
    const auto response = curl::Api().ToMemory(
        curl::Url{"https://api.github.com/repos/" + owner + "/" + name + "/releases/latest"},
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
    auto* tag_value = yyjson_obj_get(root, "tag_name");
    if (!yyjson_is_obj(root) || !yyjson_is_str(tag_value)) {
        return false;
    }
    const auto* tag_name = yyjson_get_str(tag_value);
    if (!tag_name || !*tag_name) {
        return false;
    }
    tag = tag_name;
    return true;
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
        if (asset_name && *asset_name && asset_url && *asset_url &&
            std::string_view{asset_name}.find_first_of("/\\") == std::string_view::npos) {
            assets.push_back({asset_name, asset_url});
        }
    }
    return true;
}

char FoldCharacter(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

bool AssetPatternMatches(std::string_view pattern, std::string_view value) {
    const auto match_class = [pattern](size_t start, char character, size_t& next) {
        const auto close = pattern.find(']', start + 1);
        if (close == std::string_view::npos || close == start + 1) {
            return false;
        }
        size_t index = start + 1;
        const bool negate = pattern[index] == '!' || pattern[index] == '^';
        if (negate) {
            ++index;
        }
        bool matched = false;
        while (index < close) {
            if (index + 2 < close && pattern[index + 1] == '-') {
                const auto value_folded = FoldCharacter(character);
                matched |= value_folded >= FoldCharacter(pattern[index]) &&
                    value_folded <= FoldCharacter(pattern[index + 2]);
                index += 3;
            } else {
                matched |= FoldCharacter(pattern[index]) == FoldCharacter(character);
                ++index;
            }
        }
        next = close + 1;
        return negate ? !matched : matched;
    };
    size_t pattern_index = 0;
    size_t value_index = 0;
    size_t star_index = std::string_view::npos;
    size_t retry_value_index = 0;
    while (value_index < value.size()) {
        size_t next_pattern_index = pattern_index;
        const bool class_match = pattern_index < pattern.size() &&
            pattern[pattern_index] == '[' &&
            match_class(pattern_index, value[value_index], next_pattern_index);
        if (class_match) {
            pattern_index = next_pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size() &&
                   (pattern[pattern_index] == '?' ||
                    FoldCharacter(pattern[pattern_index]) == FoldCharacter(value[value_index]))) {
            ++pattern_index;
            ++value_index;
        } else if (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
            star_index = pattern_index++;
            retry_value_index = value_index;
        } else if (star_index != std::string_view::npos) {
            pattern_index = star_index + 1;
            value_index = ++retry_value_index;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

bool ResetDownloadArea(fs::Fs* filesystem) {
    constexpr fs::FsPath staging{"/config/hats-tools/custom-components/manager/temp"};
    if (filesystem->DirExists(staging) &&
        R_FAILED(filesystem->DeleteDirectoryRecursively(staging))) {
        return false;
    }
    return R_SUCCEEDED(filesystem->CreateDirectoryRecursively(staging));
}

Result DownloadSelectedComponent(
    ui::ProgressBox* progress,
    const std::string& id,
    std::vector<updater::DownloadedAsset>& downloaded_assets,
    bool& skipped) {
    auto* app = App::GetApp();
    if (!app || !app->m_fs) {
        return 0x1;
    }
    auto* filesystem = app->m_fs.get();
    catalog::CustomComponent component;
    std::vector<catalog::AssetPattern> patterns;
    if (!catalog::LoadComponent(id, component, patterns)) {
        return 0x1;
    }
    if (component.source_type != "github_release" || component.latest_version.empty() ||
        component.latest_version == component.version) {
        skipped = true;
        return 0;
    }
    if (patterns.empty() || !ResetDownloadArea(filesystem)) {
        return 0x1;
    }

    std::vector<ReleaseAsset> release_assets;
    if (!FetchReleaseAssets(progress, component.repo, component.latest_version, release_assets)) {
        return progress->ShouldExit() ? 0x2 : 0x1;
    }
    constexpr fs::FsPath staging{"/config/hats-tools/custom-components/manager/temp"};
    for (size_t index = 0; index < patterns.size(); ++index) {
        const auto& pattern = patterns[index];
        const auto found = std::find_if(release_assets.begin(), release_assets.end(), [&pattern](const auto& asset) {
            return AssetPatternMatches(pattern.pattern, asset.name);
        });
        if (found == release_assets.end()) {
            return 0x1;
        }
        fs::FsPath destination;
        std::snprintf(destination, sizeof(destination), "%s/download-%zu", staging.s, index);
        progress->NewTransfer("Downloading " + found->name);
        const auto result = curl::Api().ToFile(
            curl::Url{found->url},
            curl::Path{destination},
            curl::OnProgress{progress->OnDownloadProgressCallback()});
        if (!result.success) {
            return progress->ShouldExit() ? 0x2 : 0x1;
        }
        downloaded_assets.push_back({pattern.pattern, found->name, destination});
    }
    return 0;
}

void StartNextUpdate(const std::shared_ptr<UpdateOperation>& operation);

void PromptForUpdateConflicts(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::shared_ptr<updater::PreparedComponentUpdate>& prepared,
    const std::shared_ptr<std::vector<std::string>>& approved_conflicts,
    size_t conflict_index);

void StartInstallPreparedComponent(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::shared_ptr<updater::PreparedComponentUpdate>& prepared,
    const std::vector<std::string>& approved_conflicts) {
    App::Push<ui::ProgressBox>(
        0, "Installing", prepared->component.name,
        [prepared, approved_conflicts](auto progress) -> Result {
            return updater::InstallPreparedComponent(progress, *prepared, approved_conflicts);
        },
        [operation](Result result) {
            if (R_SUCCEEDED(result)) {
                ++operation->updated;
            } else {
                ++operation->failed;
                operation->fatal_error |= result == 0x2;
            }
            (void)CleanStagingArea();
            StartNextUpdate(operation);
        });
}

void PromptForUpdateConflicts(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::shared_ptr<updater::PreparedComponentUpdate>& prepared,
    const std::shared_ptr<std::vector<std::string>>& approved_conflicts,
    size_t conflict_index) {
    if (conflict_index >= prepared->conflicts.size()) {
        StartInstallPreparedComponent(operation, prepared, *approved_conflicts);
        return;
    }
    const auto path = prepared->conflicts[conflict_index];
    App::Push<ui::OptionBox>(
        "An existing SD file or folder conflicts with this component:\n/" + path +
            "\n\nCreate a temporary backup and replace it?",
        "Cancel update", "Replace", 0,
        [operation, prepared, approved_conflicts, conflict_index](auto selected) {
            if (!selected || *selected != 1) {
                ++operation->skipped;
                (void)CleanStagingArea();
                StartNextUpdate(operation);
                return;
            }
            approved_conflicts->push_back(prepared->conflicts[conflict_index]);
            PromptForUpdateConflicts(operation, prepared, approved_conflicts, conflict_index + 1);
        });
}

void StartProcessDownloadedComponent(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::string& id,
    const std::shared_ptr<std::vector<updater::DownloadedAsset>>& downloaded_assets) {
    auto prepared = std::make_shared<updater::PreparedComponentUpdate>();
    App::Push<ui::ProgressBox>(
        0, "Processing", id,
        [id, downloaded_assets, prepared](auto progress) -> Result {
            return updater::ProcessDownloadedComponent(progress, id, *downloaded_assets, *prepared);
        },
        [operation, prepared](Result result) {
            if (R_FAILED(result)) {
                ++operation->failed;
                (void)CleanStagingArea();
                StartNextUpdate(operation);
                return;
            }
            auto approved_conflicts = std::make_shared<std::vector<std::string>>();
            PromptForUpdateConflicts(operation, prepared, approved_conflicts, 0);
        });
}

void StartNextUpdate(const std::shared_ptr<UpdateOperation>& operation) {
    if (operation->next_id >= operation->ids.size()) {
        if (operation->fatal_error) {
            App::Notify("Custom component update failed; check the temporary backup folder");
        } else {
            App::Notify("Updated " + std::to_string(operation->updated) + " component(s); " +
                        std::to_string(operation->failed) + " failed, " +
                        std::to_string(operation->skipped) + " skipped");
        }
        return;
    }

    const auto id = operation->ids[operation->next_id++];
    auto downloaded_assets = std::make_shared<std::vector<updater::DownloadedAsset>>();
    auto skipped = std::make_shared<bool>(false);
    App::Push<ui::ProgressBox>(
        0, "Downloading", id,
        [id, downloaded_assets, skipped](auto progress) -> Result {
            return DownloadSelectedComponent(progress, id, *downloaded_assets, *skipped);
        },
        [operation, id, downloaded_assets, skipped](Result result) {
            if (*skipped) {
                ++operation->skipped;
                StartNextUpdate(operation);
                return;
            }
            if (R_FAILED(result)) {
                if (result == 0x2) {
                    App::Notify("Custom component update cancelled");
                    (void)CleanStagingArea();
                    return;
                }
                ++operation->failed;
                (void)CleanStagingArea();
                StartNextUpdate(operation);
                return;
            }
            StartProcessDownloadedComponent(operation, id, downloaded_assets);
        });
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
    for (size_t index = 0; index < entries.size(); ++index) {
        if (progress->ShouldExit()) {
            result.cancelled = true;
            break;
        }
        const auto& entry = entries[index];
        progress->NewTransfer("Checking " + entry.repo + " (" +
                              std::to_string(index + 1) + "/" + std::to_string(entries.size()) + ")");
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
    if (!updater::ApplyFetchedVersions(latest_versions, previous_versions, result.updated)) {
        ++result.failed;
        result.fatal_error = true;
        return 0x1;
    }
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
            } else if (!result->total) {
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
    auto operation = std::make_shared<UpdateOperation>();
    operation->ids.assign(selected_ids.begin(), selected_ids.end());
    StartNextUpdate(operation);
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
