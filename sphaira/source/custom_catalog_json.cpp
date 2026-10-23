#include "custom_catalog_json.hpp"

#include <cstring>
#include <utility>

namespace sphaira::custom::catalog::json {
namespace {

const char* step_type_to_string(StepType type) {
    switch (type) {
        case StepType::UnzipToRoot: return "unzip_to_root";
        case StepType::UnzipToPath: return "unzip_to_path";
        case StepType::UnzipSubfolderToPath: return "unzip_subfolder_to_path";
        case StepType::CopyFile: return "copy_file";
        case StepType::CopyFileToAutoFolder: return "copy_file_to_auto_folder";
        case StepType::FindAndRename: return "find_and_rename";
        case StepType::DeleteFile: return "delete_file";
        case StepType::FindAndCopy: return "find_and_copy";
    }
    return nullptr;
}

bool step_type_from_string(const char* name, StepType& type) {
    if (!std::strcmp(name, "unzip_to_root")) {
        type = StepType::UnzipToRoot;
    } else if (!std::strcmp(name, "unzip_to_path")) {
        type = StepType::UnzipToPath;
    } else if (!std::strcmp(name, "unzip_subfolder_to_path")) {
        type = StepType::UnzipSubfolderToPath;
    } else if (!std::strcmp(name, "copy_file")) {
        type = StepType::CopyFile;
    } else if (!std::strcmp(name, "copy_file_to_auto_folder")) {
        type = StepType::CopyFileToAutoFolder;
    } else if (!std::strcmp(name, "find_and_rename")) {
        type = StepType::FindAndRename;
    } else if (!std::strcmp(name, "delete_file")) {
        type = StepType::DeleteFile;
    } else if (!std::strcmp(name, "find_and_copy")) {
        type = StepType::FindAndCopy;
    } else {
        return false;
    }
    return true;
}

bool read_string(yyjson_val* object, const char* key, std::string& out) {
    auto* value = yyjson_obj_get(object, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_str(value)) {
        return false;
    }

    const auto* string = yyjson_get_str(value);
    if (!string) {
        return false;
    }
    out = string;
    return true;
}

bool read_string_array(yyjson_val* value, std::vector<std::string>& out) {
    if (!yyjson_is_arr(value)) {
        return false;
    }

    size_t index, max;
    yyjson_val* item;
    yyjson_arr_foreach(value, index, max, item) {
        if (!yyjson_is_str(item)) {
            return false;
        }
        const auto* string = yyjson_get_str(item);
        if (!string) {
            return false;
        }
        out.emplace_back(string);
    }
    return true;
}

bool read_action_steps(yyjson_val* value, std::vector<ProcessingStep>& steps) {
    if (!yyjson_is_arr(value)) {
        return false;
    }

    size_t index, max;
    yyjson_val* item;
    yyjson_arr_foreach(value, index, max, item) {
        if (!yyjson_is_obj(item)) {
            return false;
        }

        ProcessingStep step;
        std::string action;
        if (!read_string(item, "action", action) ||
            !step_type_from_string(action.c_str(), step.type)) {
            return false;
        }

        if (step.type == StepType::DeleteFile) {
            if (!read_string(item, "path", step.target_path)) {
                return false;
            }
        } else if (!read_string(item, "target_path", step.target_path)) {
            return false;
        }

        if (!read_string(item, "subfolder_name", step.subfolder_name) ||
            !read_string(item, "source_file_pattern", step.source_file_pattern) ||
            !read_string(item, "target_filename", step.target_filename)) {
            return false;
        }
        steps.emplace_back(std::move(step));
    }
    return true;
}

yyjson_mut_val* processing_step_to_json(yyjson_mut_doc* doc, const ProcessingStep& step) {
    const auto* action = step_type_to_string(step.type);
    auto* value = yyjson_mut_obj(doc);
    if (!action || !value || !yyjson_mut_obj_add_str(doc, value, "action", action)) {
        return nullptr;
    }

    switch (step.type) {
        case StepType::UnzipToRoot:
            break;
        case StepType::UnzipToPath:
        case StepType::CopyFile:
        case StepType::CopyFileToAutoFolder:
            if (!yyjson_mut_obj_add_str(doc, value, "target_path", step.target_path.c_str())) {
                return nullptr;
            }
            break;
        case StepType::FindAndRename:
        case StepType::FindAndCopy:
            if (!yyjson_mut_obj_add_str(doc, value, "source_file_pattern", step.source_file_pattern.c_str()) ||
                !yyjson_mut_obj_add_str(doc, value, "target_path", step.target_path.c_str())) {
                return nullptr;
            }
            if (step.type == StepType::FindAndRename &&
                !yyjson_mut_obj_add_str(doc, value, "target_filename", step.target_filename.c_str())) {
                return nullptr;
            }
            break;
        case StepType::DeleteFile:
            if (!yyjson_mut_obj_add_str(doc, value, "path", step.target_path.c_str())) {
                return nullptr;
            }
            break;
        case StepType::UnzipSubfolderToPath:
            if (!yyjson_mut_obj_add_str(doc, value, "subfolder_name", step.subfolder_name.c_str())) {
                return nullptr;
            }
            if (!step.target_path.empty() &&
                !yyjson_mut_obj_add_str(doc, value, "target_path", step.target_path.c_str())) {
                return nullptr;
            }
            break;
    }
    return value;
}

bool add_processing_steps(
    yyjson_mut_doc* doc,
    yyjson_mut_val* parent,
    const std::vector<ProcessingStep>& steps) {
    auto* array = yyjson_mut_arr(doc);
    if (!array || !yyjson_mut_obj_add_val(doc, parent, "processing_steps", array)) {
        return false;
    }
    for (const auto& step : steps) {
        auto* value = processing_step_to_json(doc, step);
        if (!value || !yyjson_mut_arr_add_val(array, value)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool ReadString(yyjson_val* object, const char* key, std::string& out) {
    return read_string(object, key, out);
}

bool ReadComponent(yyjson_val* value, CustomComponent& component) {
    if (!yyjson_is_obj(value) ||
        !read_string(value, "name", component.name) ||
        !read_string(value, "description", component.description) ||
        !read_string(value, "category", component.category) ||
        !read_string(value, "source_type", component.source_type) ||
        !read_string(value, "repo", component.repo) ||
        !read_string(value, "version", component.version)) {
        return false;
    }

    if (auto* patterns = yyjson_obj_get(value, "asset_patterns")) {
        if (!read_string_array(patterns, component.asset_patterns)) {
            return false;
        }
    }

    if (auto* steps = yyjson_obj_get(value, "processing_steps")) {
        if (!yyjson_is_arr(steps)) {
            return false;
        }

        size_t index, max;
        yyjson_val* item;
        yyjson_arr_foreach(steps, index, max, item) {
            if (!yyjson_is_obj(item)) {
                return false;
            }

            ProcessingStep step;
            std::string type_name;
            if (!read_string(item, "type", type_name) ||
                !step_type_from_string(type_name.c_str(), step.type) ||
                !read_string(item, "target_path", step.target_path) ||
                !read_string(item, "subfolder_name", step.subfolder_name) ||
                !read_string(item, "source_file_pattern", step.source_file_pattern) ||
                !read_string(item, "target_filename", step.target_filename)) {
                return false;
            }
            component.processing_steps.emplace_back(std::move(step));
        }
    }

    return true;
}

bool ReadEditableComponent(
    const char* id,
    yyjson_val* value,
    CustomComponent& component,
    std::vector<AssetPattern>& assets) {
    if (!yyjson_is_obj(value)) {
        return false;
    }

    component.id = id;
    if (!read_string(value, "name", component.name) ||
        !read_string(value, "description", component.description) ||
        !read_string(value, "category", component.category) ||
        !read_string(value, "source_type", component.source_type) ||
        !read_string(value, "repo", component.repo)) {
        return false;
    }

    if (auto* asset_info = yyjson_obj_get(value, "asset_info")) {
        if (!read_string(asset_info, "version", component.version) ||
            !read_string(asset_info, "latest_version", component.latest_version)) {
            return false;
        }
    } else if (!read_string(value, "version", component.version)) {
        return false;
    }

    if (component.source_type == "github_release") {
        auto* patterns = yyjson_obj_get(value, "asset_patterns");
        if (!yyjson_is_arr(patterns)) {
            return false;
        }
        size_t index, max;
        yyjson_val* pattern_value;
        yyjson_arr_foreach(patterns, index, max, pattern_value) {
            if (!yyjson_is_obj(pattern_value)) {
                return false;
            }
            AssetPattern asset;
            if (!read_string(pattern_value, "pattern", asset.pattern)) {
                return false;
            }
            auto* steps = yyjson_obj_get(pattern_value, "processing_steps");
            if (!read_action_steps(steps, asset.processing_steps)) {
                return false;
            }
            component.asset_patterns.push_back(asset.pattern);
            assets.emplace_back(std::move(asset));
        }
    } else if (component.source_type == "direct_url") {
        auto* steps = yyjson_obj_get(value, "processing_steps");
        if (!read_action_steps(steps, component.processing_steps)) {
            return false;
        }
    } else {
        return false;
    }
    return true;
}

yyjson_mut_val* ComponentToJson(yyjson_mut_doc* doc, const CustomComponent& component) {
    auto* value = yyjson_mut_obj(doc);
    if (!value ||
        !yyjson_mut_obj_add_str(doc, value, "name", component.name.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "description", component.description.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "category", component.category.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "source_type", component.source_type.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "repo", component.repo.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "version", component.version.c_str())) {
        return nullptr;
    }

    auto* patterns = yyjson_mut_arr(doc);
    if (!patterns || !yyjson_mut_obj_add_val(doc, value, "asset_patterns", patterns)) {
        return nullptr;
    }
    for (const auto& pattern : component.asset_patterns) {
        if (!yyjson_mut_arr_add_str(doc, patterns, pattern.c_str())) {
            return nullptr;
        }
    }

    auto* steps = yyjson_mut_arr(doc);
    if (!steps || !yyjson_mut_obj_add_val(doc, value, "processing_steps", steps)) {
        return nullptr;
    }
    for (const auto& step : component.processing_steps) {
        const auto* type = step_type_to_string(step.type);
        auto* step_value = yyjson_mut_obj(doc);
        if (!type || !step_value ||
            !yyjson_mut_obj_add_str(doc, step_value, "type", type) ||
            !yyjson_mut_obj_add_str(doc, step_value, "target_path", step.target_path.c_str()) ||
            !yyjson_mut_obj_add_str(doc, step_value, "subfolder_name", step.subfolder_name.c_str()) ||
            !yyjson_mut_obj_add_str(doc, step_value, "source_file_pattern", step.source_file_pattern.c_str()) ||
            !yyjson_mut_obj_add_str(doc, step_value, "target_filename", step.target_filename.c_str()) ||
            !yyjson_mut_arr_add_val(steps, step_value)) {
            return nullptr;
        }
    }

    return value;
}

yyjson_mut_val* NewComponentToJson(
    yyjson_mut_doc* doc,
    const CustomComponent& component,
    const std::vector<AssetPattern>& assets) {
    auto* value = yyjson_mut_obj(doc);
    auto* asset_info = yyjson_mut_obj(doc);
    if (!value || !asset_info ||
        !yyjson_mut_obj_add_str(doc, value, "name", component.name.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "description", component.description.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "category", component.category.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "source_type", component.source_type.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "repo", component.repo.c_str()) ||
        !yyjson_mut_obj_add_str(doc, asset_info, "version", component.version.c_str()) ||
        !yyjson_mut_obj_add_str(doc, asset_info, "latest_version", component.latest_version.c_str()) ||
        !yyjson_mut_obj_add_val(doc, value, "asset_info", asset_info)) {
        return nullptr;
    }

    if (component.source_type == "github_release") {
        auto* patterns = yyjson_mut_arr(doc);
        if (!patterns || !yyjson_mut_obj_add_val(doc, value, "asset_patterns", patterns)) {
            return nullptr;
        }
        for (const auto& asset : assets) {
            auto* pattern = yyjson_mut_obj(doc);
            if (!pattern ||
                !yyjson_mut_obj_add_str(doc, pattern, "pattern", asset.pattern.c_str()) ||
                !add_processing_steps(doc, pattern, asset.processing_steps) ||
                !yyjson_mut_arr_add_val(patterns, pattern)) {
                return nullptr;
            }
        }
    } else if (component.source_type == "direct_url") {
        if (!yyjson_mut_obj_add_str(doc, value, "asset_pattern", "not_applicable") ||
            !add_processing_steps(doc, value, component.processing_steps)) {
            return nullptr;
        }
    } else {
        return nullptr;
    }

    return value;
}

} // namespace sphaira::custom::catalog::json
