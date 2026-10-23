#pragma once

#include "custom_catalog.hpp"

#include <yyjson.h>

namespace sphaira::custom::catalog::json {

bool ReadString(yyjson_val* object, const char* key, std::string& out);
bool ReadComponent(yyjson_val* value, CustomComponent& component);
bool ReadEditableComponent(
    const char* id,
    yyjson_val* value,
    CustomComponent& component,
    std::vector<AssetPattern>& assets);
yyjson_mut_val* ComponentToJson(yyjson_mut_doc* doc, const CustomComponent& component);
yyjson_mut_val* NewComponentToJson(
    yyjson_mut_doc* doc,
    const CustomComponent& component,
    const std::vector<AssetPattern>& assets);

} // namespace sphaira::custom::catalog::json
