#pragma once

#include "manifest.hpp"

#include <map>
#include <set>

namespace sphaira::custom::workflow {

bool FetchVersions();
bool UpdateAllComponents(const std::map<std::string, manifest::Component>& components);
bool UpdateSelectedComponents(const std::set<std::string>& component_ids);

} // namespace sphaira::custom::workflow
