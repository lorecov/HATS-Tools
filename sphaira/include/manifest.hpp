#pragma once

#include "fs.hpp"
#include <string>
#include <vector>
#include <map>

namespace sphaira::manifest {

// Path to manifest on SD card root
constexpr const char* MANIFEST_PATH = "/manifest.json";
constexpr const char* DISABLED_COMPONENTS_PATH = "/config/hats-tools/disabled-components.json";
constexpr const char* DISABLED_COMPONENTS_DIR = "/config/hats-tools/disabled-components";

constexpr const char* MANIFEST_PATH_CUSTOM = "/config/hats-tools/custom-components/custom_manifest.json";
constexpr const char* DISABLED_COMPONENTS_PATH_CUSTOM = "/config/hats-tools/custom-components/disabled-custom-components.json";
constexpr const char* DISABLED_COMPONENTS_DIR_CUSTOM = "/config/hats-tools/custom-components/disabled-custom-components";

// Protected component IDs that cannot be uninstalled
constexpr const char* PROTECTED_COMPONENTS[] = {
    "atmosphere",
    "hekate"
};

struct Component {
    std::string id;
    std::string name;
    std::string version;
    std::string category;
    std::string repo;
    std::vector<std::string> files;

    bool isProtected() const;
};

struct Manifest {
    std::string pack_name;
    std::string build_date;
    std::string builder_version;
    std::string supported_firmware;
    std::string content_hash;
    std::map<std::string, Component> components;
};

struct DisabledComponents {
    std::map<std::string, Component> components;
};

// Load manifest from /manifest.json
// Returns true on success, false if file doesn't exist or parse error
bool load(Manifest& out, bool is_custom = false);

// Save manifest to /manifest.json
// Returns true on success
bool save(const Manifest& m, bool is_custom = false);

// Check if manifest exists
bool exists(bool is_custom = false);

// Load disabled component metadata.
// Missing disabled metadata is treated as an empty list.
bool loadDisabled(DisabledComponents& out, bool is_custom = false);

// Save disabled component metadata.
bool saveDisabled(const DisabledComponents& disabled, bool is_custom = false);

// Get list of all components (for uninstaller menu)
std::vector<Component> getComponents(const Manifest& m);

// Get list of uninstallable components (excludes atmosphere/hekate)
std::vector<Component> getUninstallableComponents(const Manifest& m);

// Remove a component from manifest and delete its files
// Returns true on success
bool removeComponent(Manifest& m, const std::string& id, fs::Fs* fs, bool is_custom = false);

// Move component files into disabled storage and remove it from the active manifest.
bool disableComponent(Manifest& m, DisabledComponents& disabled, const std::string& id, fs::Fs* fs, bool is_custom = false);

// Move disabled component files back and restore it to the active manifest.
bool enableComponent(Manifest& m, DisabledComponents& disabled, const std::string& id, fs::Fs* fs, bool is_custom = false);

// Permanently remove disabled component files and metadata.
bool deleteDisabledComponent(DisabledComponents& disabled, const std::string& id, fs::Fs* fs, bool is_custom = false);

// Remove multiple components
// Returns number of successfully removed components
int removeComponents(Manifest& m, const std::vector<std::string>& ids, fs::Fs* fs);

// Check if a component ID is protected
bool isProtectedComponent(const std::string& id, bool is_custom = false);

} // namespace sphaira::manifest
