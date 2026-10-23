#include "custom_component_updater.hpp"

#include "custom_catalog.hpp"

#include <map>
#include <string>
#include <string_view>

namespace sphaira::custom::updater {
namespace {

std::string FormatManifestVersion(const std::string& current_version, const std::string& latest_version) {
    constexpr std::string_view separator{" ---> "};
    const auto separator_pos = current_version.find(separator);
    auto installed_version = current_version.substr(0, separator_pos);
    if (installed_version.empty()) {
        installed_version = "not installed";
    }
    if (installed_version != "not installed" && installed_version == latest_version) {
        return installed_version;
    }
    return installed_version + std::string{separator} + latest_version;
}

} // namespace

bool ApplyFetchedVersions(
    const std::map<std::string, std::string>& latest_versions,
    const std::map<std::string, std::string>& previous_versions,
    size_t& updated_components) {
    updated_components = 0;
    if (latest_versions.empty()) {
        return true;
    }

    manifest::Manifest custom_manifest;
    if (!manifest::load(custom_manifest, true)) {
        return false;
    }
    manifest::DisabledComponents disabled_components;
    if (!manifest::loadDisabled(disabled_components, true)) {
        return false;
    }

    const auto original_manifest = custom_manifest;
    const auto original_disabled = disabled_components;
    bool manifest_changed = false;
    bool disabled_changed = false;
    for (const auto& [id, latest_version] : latest_versions) {
        if (auto it = custom_manifest.components.find(id); it != custom_manifest.components.end()) {
            const auto updated = FormatManifestVersion(it->second.version, latest_version);
            if (updated != it->second.version) {
                it->second.version = updated;
                manifest_changed = true;
            }
            ++updated_components;
        } else if (auto it = disabled_components.components.find(id); it != disabled_components.components.end()) {
            const auto updated = FormatManifestVersion(it->second.version, latest_version);
            if (updated != it->second.version) {
                it->second.version = updated;
                disabled_changed = true;
            }
            ++updated_components;
        }
    }

    // Restore saved manifests if a later catalog write fails.
    if (manifest_changed && !manifest::save(custom_manifest, true)) {
        (void)manifest::save(original_manifest, true);
        return false;
    }
    if (disabled_changed && !manifest::saveDisabled(disabled_components, true)) {
        if (manifest_changed) {
            (void)manifest::save(original_manifest, true);
        }
        (void)manifest::saveDisabled(original_disabled, true);
        return false;
    }
    if (!catalog::SaveLatestVersions(latest_versions)) {
        if (manifest_changed) {
            (void)manifest::save(original_manifest, true);
        }
        if (disabled_changed) {
            (void)manifest::saveDisabled(original_disabled, true);
        }
        (void)catalog::SaveLatestVersions(previous_versions);
        return false;
    }
    updated_components = latest_versions.size();
    return true;
}

} // namespace sphaira::custom::updater
