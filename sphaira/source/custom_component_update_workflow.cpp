#include "custom_component_update_workflow.hpp"

#include "custom_component_github.hpp"
#include "custom_component_storage.hpp"
#include "custom_component_updater.hpp"
#include "app.hpp"
#include "custom_catalog.hpp"
#include "custom_component_update_types.hpp"
#include "fs.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sphaira::custom::workflow {
namespace {

struct FetchVersionsResult {
    size_t total{};
    size_t updated{};
    size_t failed{};
    bool cancelled{};
    bool fatal_error{};
};

struct UpdateOperation {
    // Updates run sequentially because each component reuses the same staging directory.
    std::vector<std::string> component_ids;
    size_t next_component_index{};
    size_t updated{};
    size_t failed{};
    size_t skipped{};
    bool fatal_error{};
};













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
    std::vector<update_detail::DownloadedAsset>& downloaded_assets,
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

    std::vector<github::ReleaseAsset> release_assets;
    if (!github::FetchReleaseAssets(progress, component.repo, component.latest_version, release_assets)) {
        return progress->ShouldExit() ? 0x2 : 0x1;
    }
    constexpr fs::FsPath staging{"/config/hats-tools/custom-components/manager/temp"};
    for (size_t index = 0; index < patterns.size(); ++index) {
        const auto& pattern = patterns[index];
        const auto found = std::find_if(release_assets.begin(), release_assets.end(), [&pattern](const auto& asset) {
            return github::AssetMatchesPattern(pattern.pattern, asset.name);
        });
        if (found == release_assets.end()) {
            return 0x1;
        }
        fs::FsPath destination;
        std::snprintf(destination, sizeof(destination), "%s/download-%zu", staging.s, index);
        progress->NewTransfer("Downloading " + found->name);
        if (!github::DownloadReleaseAsset(progress, found->download_url, destination)) {
            return progress->ShouldExit() ? 0x2 : 0x1;
        }
        downloaded_assets.push_back({pattern.pattern, found->name, destination});
    }
    return 0;
}

void StartNextUpdate(const std::shared_ptr<UpdateOperation>& operation);

void PromptForUpdateConflicts(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::shared_ptr<update_detail::PreparedComponentUpdate>& prepared,
    const std::shared_ptr<std::vector<std::string>>& approved_conflicts,
    size_t conflict_index);

void StartInstallPreparedComponent(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::shared_ptr<update_detail::PreparedComponentUpdate>& prepared,
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
            (void)storage::CleanStagingArea();
            StartNextUpdate(operation);
        });
}

void PromptForUpdateConflicts(
    const std::shared_ptr<UpdateOperation>& operation,
    const std::shared_ptr<update_detail::PreparedComponentUpdate>& prepared,
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
                (void)storage::CleanStagingArea();
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
    const std::shared_ptr<std::vector<update_detail::DownloadedAsset>>& downloaded_assets) {
    auto prepared = std::make_shared<update_detail::PreparedComponentUpdate>();
    App::Push<ui::ProgressBox>(
        0, "Processing", id,
        [id, downloaded_assets, prepared](auto progress) -> Result {
            return updater::ProcessDownloadedComponent(progress, id, *downloaded_assets, *prepared);
        },
        [operation, prepared](Result result) {
            if (R_FAILED(result)) {
                ++operation->failed;
                (void)storage::CleanStagingArea();
                StartNextUpdate(operation);
                return;
            }
            auto approved_conflicts = std::make_shared<std::vector<std::string>>();
            PromptForUpdateConflicts(operation, prepared, approved_conflicts, 0);
        });
}

void StartNextUpdate(const std::shared_ptr<UpdateOperation>& operation) {
    if (operation->next_component_index >= operation->component_ids.size()) {
        if (operation->fatal_error) {
            App::Notify("Custom component update failed; check the temporary backup folder");
        } else {
            App::Notify("Updated " + std::to_string(operation->updated) + " component(s); " +
                        std::to_string(operation->failed) + " failed, " +
                        std::to_string(operation->skipped) + " skipped");
        }
        return;
    }

    const auto id = operation->component_ids[operation->next_component_index++];
    auto downloaded_assets = std::make_shared<std::vector<update_detail::DownloadedAsset>>();
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
                    (void)storage::CleanStagingArea();
                    return;
                }
                ++operation->failed;
                (void)storage::CleanStagingArea();
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
        if (!github::FetchLatestReleaseTag(progress, entry.repo, tag)) {
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

bool FetchVersions() {
    if (!storage::EnsureDirectories() || !storage::EnsureCatalogExists()) {
        return false;
    }
    auto result = std::make_shared<FetchVersionsResult>();
    App::Push<ui::ProgressBox>(
        0, "Checking", "Custom component versions",
        [result](auto progress) -> Result {
            return RunFetchVersions(progress, *result);
        },
        [result](Result result_code) {
            if (result->fatal_error) {
                App::Notify("Failed to fetch custom component versions");
            } else if (!result->total) {
                App::Notify("No GitHub custom components to check");
            } else if (R_SUCCEEDED(result_code)) {
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

bool UpdateAllComponents(const std::map<std::string, manifest::Component>& components) {
    if (components.empty()) {
        App::Notify("No custom components available.");
        return false;
    }

    std::set<std::string> component_ids;
    for (const auto& [id, _] : components) {
        component_ids.insert(id);
    }
    return UpdateSelectedComponents(component_ids);
}

bool UpdateSelectedComponents(const std::set<std::string>& selected_ids) {
    if (!storage::EnsureDirectories() || !storage::EnsureCatalogExists()) {
        return false;
    }
    if (selected_ids.empty()) {
        App::Notify("Select at least one custom component to update");
        return false;
    }
    auto operation = std::make_shared<UpdateOperation>();
    operation->component_ids.assign(selected_ids.begin(), selected_ids.end());
    StartNextUpdate(operation);
    return true;
}


} // namespace sphaira::custom::workflow
