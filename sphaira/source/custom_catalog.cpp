#include "custom_catalog.hpp"

#include "custom_components.hpp"
#include "log.hpp"

#include <cstring>
#include <utility>
#include <yyjson.h>

namespace sphaira::custom::catalog {

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

bool read_bool(yyjson_val* object, const char* key, bool& out) {
    auto* value = yyjson_obj_get(object, key);
    if (!value) {
        return true;
    }
    if (!yyjson_is_bool(value)) {
        return false;
    }
    out = yyjson_get_bool(value);
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

bool read_component(yyjson_val* value, CustomComponent& component) {
    if (!yyjson_is_obj(value) ||
        !read_string(value, "name", component.name) ||
        !read_string(value, "description", component.description) ||
        !read_string(value, "category", component.category) ||
        !read_string(value, "source_type", component.source_type) ||
        !read_string(value, "repo", component.repo) ||
        !read_string(value, "version", component.version) ||
        !read_string(value, "latest_version", component.latest_version) ||
        !read_string(value, "fixed_version", component.fixed_version) ||
        !read_bool(value, "fixed_version_enabled", component.fixed_version_enabled) ||
        !read_bool(value, "update_available", component.update_available)) {
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

yyjson_mut_val* component_to_json(yyjson_mut_doc* doc, const CustomComponent& component) {
    auto* value = yyjson_mut_obj(doc);
    if (!value ||
        !yyjson_mut_obj_add_str(doc, value, "name", component.name.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "description", component.description.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "category", component.category.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "source_type", component.source_type.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "repo", component.repo.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "version", component.version.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "latest_version", component.latest_version.c_str()) ||
        !yyjson_mut_obj_add_str(doc, value, "fixed_version", component.fixed_version.c_str()) ||
        !yyjson_mut_obj_add_bool(doc, value, "fixed_version_enabled", component.fixed_version_enabled) ||
        !yyjson_mut_obj_add_bool(doc, value, "update_available", component.update_available)) {
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

} // namespace

bool LoadCatalog(Catalog& out) {
    if (!::sphaira::custom::EnsureCatalogExists()) {
        return false;
    }

    auto* doc = yyjson_read_file(CATALOG_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        log_write("[CUSTOM_COMPONENTS] failed to read catalog %s\n", CATALOG_PATH);
        return false;
    }
    ON_SCOPE_EXIT(yyjson_doc_free(doc));

    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        log_write("[CUSTOM_COMPONENTS] invalid catalog root in %s\n", CATALOG_PATH);
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
            log_write("[CUSTOM_COMPONENTS] invalid component entry in %s\n", CATALOG_PATH);
            return false;
        }

        CustomComponent component;
        component.id = id;
        if (!read_component(value, component)) {
            log_write("[CUSTOM_COMPONENTS] invalid component '%s' in %s\n", id, CATALOG_PATH);
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
        log_write("[CUSTOM_COMPONENTS] failed to allocate catalog JSON document\n");
        return false;
    }
    ON_SCOPE_EXIT(yyjson_mut_doc_free(doc));

    auto* root = yyjson_mut_obj(doc);
    if (!root) {
        log_write("[CUSTOM_COMPONENTS] failed to allocate catalog JSON root\n");
        return false;
    }
    yyjson_mut_doc_set_root(doc, root);

    for (const auto& [id, component] : catalog.components) {
        auto* value = component_to_json(doc, component);
        if (!value || !yyjson_mut_obj_add_val(doc, root, id.c_str(), value)) {
            log_write("[CUSTOM_COMPONENTS] failed to serialize component '%s'\n", id.c_str());
            return false;
        }
    }

    if (!::sphaira::custom::EnsureDirectories()) {
        return false;
    }

    yyjson_write_err err{};
    const bool success = yyjson_mut_write_file(CATALOG_PATH, doc, YYJSON_WRITE_PRETTY, nullptr, &err);
    if (!success) {
        log_write("[CUSTOM_COMPONENTS] failed to save catalog %s: %s\n", CATALOG_PATH, err.msg);
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

} // namespace sphaira::custom::catalog
