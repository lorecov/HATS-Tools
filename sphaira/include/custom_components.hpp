#pragma once

#include "custom_catalog.hpp"
#include "manifest.hpp"

#include <map>
#include <set>
#include <string>

namespace sphaira::custom {

// Fetches the latest GitHub release versions for catalog components.
bool FetchVersions(const std::map<std::string, manifest::Component>& components);

// Updates every custom component listed in the manifest.
bool UpdateAllComponents(const std::map<std::string, manifest::Component>& components);

// Updates the custom components identified by their catalog IDs.
bool UpdateSelectedComponents(const std::set<std::string>& selected_ids);

// Opens the custom component editor for a new component.
bool AddNewComponent();

// Opens the custom component editor for an existing component.
bool ModifySelectedComponent(const std::string& component_id);

bool EnsureDirectories();
bool CleanStagingArea();
bool EnsureCatalogExists();

} // namespace sphaira::custom
