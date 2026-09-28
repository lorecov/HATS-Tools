#include "custom_components_manager.hpp"
#include "download.hpp"
#include "log.hpp"
#include "minizip_helper.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <minizip/unzip.h>
#include <yyjson.h>

namespace sphaira::ui::menu::hats {

namespace {

constexpr const char* CUSTOM_MANIFEST_PATH = "/config/hats-tools/custom-components/custom_manifest.json";
constexpr const char* DISABLED_MANIFEST_PATH = "/config/hats-tools/custom-components/disabled-custom-components.json";
constexpr char EXTRA_SEPARATOR = '\x1e';
constexpr char FIELD_SEPARATOR = '\x1f';
thread_local std::vector<CustomComponentFileRecord>* g_tracked_files{};

std::string json_string(yyjson_val* object, const char* key) {
    auto value = object ? yyjson_obj_get(object, key) : nullptr;
    const auto text = yyjson_get_str(value);
    return text ? text : "";
}

std::string normalize_relative(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    if (path.find(':') != std::string::npos) return {};
    std::string result;
    size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part == "..") return {};
        if (!part.empty() && part != ".") {
            if (!result.empty()) result += '/';
            result += part;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

fs::FsPath sd_path(const std::string& path) {
    if (path.size() >= PATH_MAX) return {};
    fs::FsPath result{"/"};
    const auto relative = normalize_relative(path);
    if (!path.empty() && relative.empty() && path != "/") return {};
    if (!relative.empty()) result += relative;
    return result;
}

fs::FsPath join_path(const std::string& root, const std::string& child) {
    const auto normalized_root = normalize_relative(root);
    const auto relative = normalize_relative(child);
    if (root.size() + relative.size() + 2 >= PATH_MAX || (!root.empty() && root != "/" && normalized_root.empty())
        || (!child.empty() && relative.empty())) return {};
    fs::FsPath result{root.c_str()};
    if (!result.ends_with("/")) result += '/';
    result += relative;
    return result;
}

std::string basename(const std::string& path) {
    const auto relative = normalize_relative(path);
    const auto slash = relative.find_last_of('/');
    return slash == std::string::npos ? relative : relative.substr(slash + 1);
}

std::string parent_path(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "/" : path.substr(0, slash + 1);
}

bool wildcard_match(const char* pattern, const char* value) {
    if (!pattern || !value) return false;
    if (*pattern == '\0') return *value == '\0';
    if (*pattern == '*') {
        while (*pattern == '*') ++pattern;
        if (*pattern == '\0') return true;
        for (; *value; ++value) if (wildcard_match(pattern, value)) return true;
        return false;
    }
    if (*pattern == '?' || *pattern == *value) return *value && wildcard_match(pattern + 1, value + 1);
    return false;
}

void track_path(const fs::FsPath& path) {
    if (!g_tracked_files) return;
    const auto relative = normalize_relative(path.s);
    if (relative.empty()) return;
    if (std::none_of(g_tracked_files->begin(), g_tracked_files->end(), [&relative](const auto& item) {
        return item.rel_path == relative;
    })) g_tracked_files->push_back({relative, {}});
}

std::string asset_pattern(const ProcessingStep& step) {
    const auto separator = step.extra_param.find(EXTRA_SEPARATOR);
    return separator == std::string::npos ? "" : step.extra_param.substr(0, separator);
}

std::vector<std::string> extra_fields(const ProcessingStep& step) {
    std::vector<std::string> result;
    const auto separator = step.extra_param.find(EXTRA_SEPARATOR);
    if (separator == std::string::npos || separator + 1 == step.extra_param.size()) return result;
    size_t start = separator + 1;
    while (start <= step.extra_param.size()) {
        const auto end = step.extra_param.find(FIELD_SEPARATOR, start);
        result.push_back(step.extra_param.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}

std::string encode_extra(const std::string& pattern, const std::vector<std::string>& fields) {
    std::string result = pattern + EXTRA_SEPARATOR;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) result += FIELD_SEPARATOR;
        result += fields[i];
    }
    return result;
}

bool set_string(yyjson_mut_doc* doc, yyjson_mut_val* object, const char* key, const std::string& value) {
    auto current = yyjson_mut_obj_get(object, key);
    return current ? yyjson_mut_set_str(current, value.c_str()) : yyjson_mut_obj_add_str(doc, object, key, value.c_str());
}

yyjson_mut_doc* load_mutable_json(const char* path, bool create) {
    auto read = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (read) {
        auto doc = yyjson_doc_mut_copy(read, nullptr);
        yyjson_doc_free(read);
        return doc;
    }
    if (!create) return nullptr;
    auto doc = yyjson_mut_doc_new(nullptr);
    if (doc) yyjson_mut_doc_set_root(doc, yyjson_mut_obj(doc));
    return doc;
}

bool save_mutable_json(const char* path, yyjson_mut_doc* doc) {
    yyjson_write_err error{};
    if (yyjson_mut_write_file(path, doc, YYJSON_WRITE_PRETTY, nullptr, &error)) return true;
    log_write("[CUSTOM_MGR] write failed: %s (%s)\n", path, error.msg ? error.msg : "unknown error");
    return false;
}

struct ManifestEntry {
    std::string name;
    std::string version;
    std::string category;
    std::string repo;
    std::vector<std::string> files;
};

bool read_manifest_entry(const char* path, const std::string& id, ManifestEntry& entry) {
    auto doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) return false;
    auto root = yyjson_doc_get_root(doc);
    auto components = root ? yyjson_obj_get(root, "components") : nullptr;
    auto component = components ? yyjson_obj_get(components, id.c_str()) : nullptr;
    if (!component) {
        yyjson_doc_free(doc);
        return false;
    }
    entry.name = json_string(component, "name");
    entry.version = json_string(component, "version");
    entry.category = json_string(component, "category");
    entry.repo = json_string(component, "repo");
    auto files = yyjson_obj_get(component, "files");
    if (files && yyjson_is_arr(files)) {
        size_t index, max;
        yyjson_val* item;
        yyjson_arr_foreach(files, index, max, item) {
            const auto file = yyjson_get_str(item);
            if (file) entry.files.emplace_back(file);
        }
    }
    yyjson_doc_free(doc);
    return true;
}

bool update_manifest_entry(const char* path, const std::string& id, const ManifestEntry* entry) {
    auto doc = load_mutable_json(path, entry != nullptr);
    if (!doc) return entry == nullptr;
    auto root = yyjson_mut_doc_get_root(doc);
    auto components = yyjson_mut_obj_get(root, "components");
    if (!components && entry) components = yyjson_mut_obj_add_obj(doc, root, "components");
    bool success = components != nullptr;
    if (success && !entry) {
        yyjson_mut_obj_remove_str(components, id.c_str());
    } else if (success) {
        auto component = yyjson_mut_obj_get(components, id.c_str());
        if (!component) component = yyjson_mut_obj_add_obj(doc, components, id.c_str());
        success = component && set_string(doc, component, "name", entry->name)
            && set_string(doc, component, "version", entry->version)
            && set_string(doc, component, "category", entry->category)
            && set_string(doc, component, "repo", entry->repo);
        if (success) {
            yyjson_mut_obj_remove_str(component, "files");
            auto files = yyjson_mut_arr(doc);
            success = files && yyjson_mut_obj_add_val(doc, component, "files", files);
            for (const auto& file : entry->files) success = success && yyjson_mut_arr_add_str(doc, files, file.c_str());
        }
    }
    if (success) success = save_mutable_json(path, doc);
    yyjson_mut_doc_free(doc);
    return success;
}

std::string step_action(ProcessingStepType type) {
    switch (type) {
        case ProcessingStepType::UnzipToRoot: return "unzip_to_root";
        case ProcessingStepType::UnzipToPath: return "unzip_to_path";
        case ProcessingStepType::UnzipSubfolderToPath: return "unzip_subfolder_to_path";
        case ProcessingStepType::CopyFile: return "copy_file";
        case ProcessingStepType::CopyFileToAutoFolder: return "copy_file_to_auto_folder";
        case ProcessingStepType::FindAndRename: return "find_and_rename";
        case ProcessingStepType::DeleteFile: return "delete_file";
    }
    return {};
}

bool extract_zip(const std::string& zip_path, const std::string& target_root, const std::string& subfolder = {}) {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    const auto safe_root = sd_path(target_root);
    if (safe_root.empty()) return false;
    zlib_filefunc64_def functions;
    sphaira::mz::FileFuncStdio(&functions);
    auto zip = unzOpen2_64(zip_path.c_str(), &functions);
    if (!zip) return false;
    unz_global_info64 info{};
    bool success = unzGetGlobalInfo64(zip, &info) == UNZ_OK && unzGoToFirstFile(zip) == UNZ_OK;
    const auto prefix = normalize_relative(subfolder);
    std::array<char, 32768> buffer{};
    for (u64 index = 0; success && index < info.number_entry; ++index) {
        unz_file_info64 file_info{};
        std::array<char, PATH_MAX> name{};
        if (unzGetCurrentFileInfo64(zip, &file_info, name.data(), name.size(), nullptr, 0, nullptr, 0) != UNZ_OK) {
            success = false;
            break;
        }
        const std::string original{name.data()};
        const bool directory = !original.empty() && (original.back() == '/' || original.back() == '\\');
        auto relative = normalize_relative(original);
        if (!original.empty() && relative.empty()) {
            success = false;
            break;
        }
        if (!prefix.empty()) {
            if (relative == prefix) relative.clear();
            else if (relative.starts_with(prefix + "/")) relative.erase(0, prefix.size() + 1);
            else relative.clear();
        }
        if (!relative.empty()) {
            auto destination = join_path(safe_root.s, relative);
            if (destination.empty()) { success = false; break; }
            if (directory) {
                success = R_SUCCEEDED(sd.CreateDirectoryRecursively(destination)) || sd.DirExists(destination);
            } else if (unzOpenCurrentFile(zip) != UNZ_OK) {
                success = false;
            } else {
                success = R_SUCCEEDED(sd.CreateDirectoryRecursively(parent_path(destination.s).c_str()));
                if (success && sd.FileExists(destination)) success = R_SUCCEEDED(sd.DeleteFile(destination));
                if (success) success = R_SUCCEEDED(sd.CreateFile(destination));
                fs::File output;
                if (success) success = R_SUCCEEDED(sd.OpenFile(destination, fs::OpenMode_WriteBuffered, &output));
                s64 offset = 0;
                while (success) {
                    const int bytes = unzReadCurrentFile(zip, buffer.data(), buffer.size());
                    if (bytes < 0) { success = false; break; }
                    if (!bytes) break;
                    success = R_SUCCEEDED(output.Write(offset, buffer.data(), bytes, 0));
                    offset += bytes;
                }
                output.Close();
                unzCloseCurrentFile(zip);
                if (success) track_path(destination);
            }
        }
        if (index + 1 < info.number_entry && unzGoToNextFile(zip) != UNZ_OK) success = false;
    }
    unzClose(zip);
    return success && R_SUCCEEDED(sd.Commit());
}

bool copy_file(const std::string& source, const std::string& destination) {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    if (normalize_relative(source).empty() || normalize_relative(destination).empty()) return false;
    auto src = sd_path(source);
    auto dst = sd_path(destination);
    if (!sd.FileExists(src) || R_FAILED(sd.CreateDirectoryRecursively(parent_path(dst.s).c_str()))) return false;
    if (sd.FileExists(dst) && R_FAILED(sd.DeleteFile(dst))) return false;
    const bool success = R_SUCCEEDED(sd.copy_entire_file(dst, src));
    if (success) track_path(dst);
    return success && R_SUCCEEDED(sd.Commit());
}

bool resolve_release(const CustomComponent& component, std::vector<std::pair<std::string, std::string>>& assets, std::string& version) {
    if (component.download_url.find("api.github.com/repos/") == std::string::npos) {
        if (!component.download_url.empty()) assets.emplace_back("", component.download_url);
        version = component.latest_version;
        return !assets.empty();
    }
    auto response = sphaira::curl::Api().ToMemory(sphaira::curl::Url{component.download_url});
    if (!response.success || response.data.empty()) return false;
    auto doc = yyjson_read(reinterpret_cast<const char*>(response.data.data()), response.data.size(), YYJSON_READ_NOFLAG);
    if (!doc) return false;
    auto root = yyjson_doc_get_root(doc);
    version = json_string(root, "tag_name");
    std::vector<std::pair<std::string, std::string>> remote;
    auto array = root ? yyjson_obj_get(root, "assets") : nullptr;
    if (array && yyjson_is_arr(array)) {
        size_t index, max;
        yyjson_val* asset;
        yyjson_arr_foreach(array, index, max, asset) remote.emplace_back(json_string(asset, "name"), json_string(asset, "browser_download_url"));
    }
    std::vector<std::string> patterns;
    for (const auto& step : component.install_steps) {
        const auto pattern = asset_pattern(step);
        if (!pattern.empty() && std::find(patterns.begin(), patterns.end(), pattern) == patterns.end()) patterns.push_back(pattern);
    }
    if (patterns.empty()) patterns.emplace_back("*");
    for (const auto& pattern : patterns) {
        auto found = std::find_if(remote.begin(), remote.end(), [&pattern](const auto& item) {
            return wildcard_match(pattern.c_str(), item.first.c_str());
        });
        if (found != remote.end()) assets.push_back(*found);
    }
    yyjson_doc_free(doc);
    return !assets.empty();
}

bool download_to(const std::string& url, const std::string& path, const ProgressCallback& callback) {
    if (url.empty()) return false;
    const auto result = sphaira::curl::Api().ToFile(
        sphaira::curl::Url{url}, sphaira::curl::Path{path},
        sphaira::curl::OnProgress{[callback](s64 total, s64 now, s64, s64) {
            if (callback) callback("Downloading", total > 0 ? 100.0f * now / total : 0.0f);
            return true;
        }});
    return result.success;
}

bool rename_match(fs::FsNativeSd& sd, const fs::FsPath& dir_path, const std::string& pattern, const std::string& new_name, unsigned depth = 0) {
    if (depth > 32 || pattern.empty() || new_name.empty()) return false;
    fs::Dir dir;
    if (R_FAILED(sd.OpenDirectory(dir_path, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir))) return false;
    std::vector<FsDirectoryEntry> entries;
    if (R_FAILED(dir.ReadAll(entries))) return false;
    for (const auto& entry : entries) {
        const std::string name{entry.name};
        if (name.empty() || name == "." || name == "..") continue;
        auto child = join_path(dir_path.s, name);
        if (child.empty()) continue;
        if (wildcard_match(pattern.c_str(), name.c_str())) {
            auto destination = join_path(parent_path(child.s), new_name);
            if (destination.empty()) return false;
            if (sd.FileExists(destination) || sd.DirExists(destination)) return false;
            if (R_FAILED(sd.CreateDirectoryRecursively(parent_path(destination.s).c_str()))) return false;
            const bool directory = sd.DirExists(child);
            const bool success = directory ? R_SUCCEEDED(sd.RenameDirectory(child, destination)) : R_SUCCEEDED(sd.RenameFile(child, destination));
            if (success) track_path(destination);
            return success;
        }
        if (sd.DirExists(child) && rename_match(sd, child, pattern, new_name, depth + 1)) return true;
    }
    return false;
}

std::string repository_value(const CustomComponent& component) {
    constexpr const char* marker = "/repos/";
    const auto start = component.download_url.find(marker);
    if (start == std::string::npos) return component.download_url;
    const auto repo_start = start + std::strlen(marker);
    const auto end = component.download_url.find("/releases", repo_start);
    return component.download_url.substr(repo_start, end == std::string::npos ? end : end - repo_start);
}

} // namespace

// -----------------------------------------------------------------------------
// 1. GESTIONE MANIFEST & PARSING JSON
// -----------------------------------------------------------------------------

bool CustomComponentsManager::LoadManagerCatalog(std::unordered_map<std::string, CustomComponent>& out_components) {
    out_components.clear();
    auto doc = yyjson_read_file(MANAGER_JSON_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) return false;
    auto root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    yyjson_obj_iter iter;
    yyjson_obj_iter_init(root, &iter);
    yyjson_val* key;
    while ((key = yyjson_obj_iter_next(&iter))) {
        const auto id = yyjson_get_str(key);
        auto value = yyjson_obj_iter_get_val(key);
        if (!id || !yyjson_is_obj(value)) continue;
        CustomComponent component;
        component.id = id;
        component.name = json_string(value, "name");
        component.description = json_string(value, "description");
        component.category = json_string(value, "category");
        auto asset_info = yyjson_obj_get(value, "asset_info");
        component.version = json_string(asset_info, "version");
        const auto source_type = json_string(value, "source_type");
        const auto repo = json_string(value, "repo");
        if (source_type == "direct_url") {
            component.download_url = repo;
        } else if (!repo.empty()) {
            component.download_url = "https://api.github.com/repos/" + repo + "/releases/latest";
        }

        const auto parse_step = [&component](yyjson_val* step_value, const std::string& pattern) {
            const auto action = json_string(step_value, "action");
            ProcessingStep step{};
            if (action == "unzip_to_root") step.type = ProcessingStepType::UnzipToRoot;
            else if (action == "unzip_to_path") step.type = ProcessingStepType::UnzipToPath;
            else if (action == "unzip_subfolder_to_path") step.type = ProcessingStepType::UnzipSubfolderToPath;
            else if (action == "copy_file") step.type = ProcessingStepType::CopyFile;
            else if (action == "copy_file_to_auto_folder") step.type = ProcessingStepType::CopyFileToAutoFolder;
            else if (action == "find_and_rename") step.type = ProcessingStepType::FindAndRename;
            else if (action == "delete_file") step.type = ProcessingStepType::DeleteFile;
            else return;
            step.target_path = json_string(step_value, "target_path");
            switch (step.type) {
                case ProcessingStepType::UnzipSubfolderToPath: step.source_path = json_string(step_value, "subfolder_name"); break;
                case ProcessingStepType::CopyFile: step.source_path = json_string(step_value, "source_file_pattern"); break;
                case ProcessingStepType::FindAndRename: step.source_path = json_string(step_value, "search_dir"); break;
                case ProcessingStepType::DeleteFile: step.source_path = json_string(step_value, "path"); break;
                default: step.source_path = json_string(step_value, "source_file_pattern"); break;
            }
            step.extra_param = pattern;
            step.extra_param += EXTRA_SEPARATOR;
            const std::array<const char*, 6> keys{{"subfolder_name", "target_pattern", "target_filename", "new_name", "search_dir", "path"}};
            for (size_t i = 0; i < keys.size(); ++i) {
                if (i) step.extra_param += FIELD_SEPARATOR;
                step.extra_param += json_string(step_value, keys[i]);
            }
            component.install_steps.emplace_back(std::move(step));
        };

        auto patterns = yyjson_obj_get(value, "asset_patterns");
        if (patterns && yyjson_is_arr(patterns)) {
            size_t index, max;
            yyjson_val* pattern;
            yyjson_arr_foreach(patterns, index, max, pattern) {
                auto processing = yyjson_obj_get(pattern, "processing_steps");
                const auto asset_name = json_string(pattern, "pattern");
                if (!processing || !yyjson_is_arr(processing)) continue;
                size_t step_index, step_max;
                yyjson_val* step;
                yyjson_arr_foreach(processing, step_index, step_max, step) parse_step(step, asset_name);
            }
        } else {
            auto processing = yyjson_obj_get(value, "processing_steps");
            if (processing && yyjson_is_arr(processing)) {
                size_t index, max;
                yyjson_val* step;
                yyjson_arr_foreach(processing, index, max, step) parse_step(step, "");
            }
        }
        auto extras = yyjson_obj_get(value, "component_extras");
        if (extras && yyjson_is_arr(extras)) {
            size_t index, max;
            yyjson_val* extra;
            yyjson_arr_foreach(extras, index, max, extra) {
                auto enabled = yyjson_obj_get(extra, "enabled");
                if (enabled && !yyjson_get_bool(enabled)) continue;
                ProcessingStep step{};
                step.type = ProcessingStepType::CopyFile;
                step.source_path = json_string(extra, "source");
                step.target_path = json_string(extra, "target");
                const auto overwrite = yyjson_obj_get(extra, "overwrite");
                step.extra_param = encode_extra("", {"", "", "", "", "", "component_extra", overwrite && yyjson_get_bool(overwrite) ? "1" : "0"});
                if (!step.source_path.empty() && !step.target_path.empty()) component.install_steps.emplace_back(std::move(step));
            }
        }
        out_components.emplace(component.id, std::move(component));
    }
    yyjson_doc_free(doc);
    return true;
}

bool CustomComponentsManager::SaveManagerCatalog(const std::unordered_map<std::string, CustomComponent>& components) {
    if (!EnsureDirectories()) return false;
    auto doc = load_mutable_json(MANAGER_JSON_PATH, true);
    if (!doc) return false;
    auto root = yyjson_mut_doc_get_root(doc);
    if (!yyjson_mut_is_obj(root)) {
        yyjson_mut_doc_free(doc);
        return false;
    }
    bool success = true;
    for (const auto& [id, component] : components) {
        auto object = yyjson_mut_obj_get(root, id.c_str());
        if (object) {
            success = set_string(doc, object, "name", component.name)
                && set_string(doc, object, "description", component.description)
                && set_string(doc, object, "category", component.category)
                && set_string(doc, object, "source_type", component.download_url.find("api.github.com/repos/") != std::string::npos ? "github_release" : "direct_url")
                && set_string(doc, object, "repo", repository_value(component));
            auto asset_info = yyjson_mut_obj_get(object, "asset_info");
            if (!asset_info) asset_info = yyjson_mut_obj_add_obj(doc, object, "asset_info");
            success = success && asset_info && set_string(doc, asset_info, "version", component.version);
        } else {
            object = yyjson_mut_obj_add_obj(doc, root, id.c_str());
            if (!object) { success = false; break; }
            success = set_string(doc, object, "name", component.name)
                && set_string(doc, object, "description", component.description)
                && set_string(doc, object, "category", component.category)
                && set_string(doc, object, "source_type", component.download_url.find("api.github.com/repos/") != std::string::npos ? "github_release" : "direct_url")
                && set_string(doc, object, "repo", repository_value(component));
            auto asset_info = yyjson_mut_obj_add_obj(doc, object, "asset_info");
            success = success && asset_info && set_string(doc, asset_info, "version", component.version);
            if (success) {
                const bool github_release = component.download_url.find("api.github.com/repos/") != std::string::npos;
                auto patterns = github_release ? yyjson_mut_arr(doc) : nullptr;
                auto direct_steps = github_release ? nullptr : yyjson_mut_arr(doc);
                if (github_release) success = patterns && yyjson_mut_obj_add_val(doc, object, "asset_patterns", patterns);
                else success = set_string(doc, object, "asset_pattern", "not_applicable")
                    && direct_steps && yyjson_mut_obj_add_val(doc, object, "processing_steps", direct_steps);
                std::vector<std::pair<std::string, yyjson_mut_val*>> pattern_steps;
                for (const auto& step : component.install_steps) {
                    yyjson_mut_val* steps = direct_steps;
                    if (github_release) {
                        const auto name = asset_pattern(step);
                        auto found = std::find_if(pattern_steps.begin(), pattern_steps.end(), [&name](const auto& item) { return item.first == name; });
                        if (found == pattern_steps.end()) {
                            auto pattern = yyjson_mut_obj(doc);
                            steps = yyjson_mut_arr(doc);
                            success = success && pattern && steps && yyjson_mut_arr_add_val(patterns, pattern)
                                && set_string(doc, pattern, "pattern", name)
                                && yyjson_mut_obj_add_val(doc, pattern, "processing_steps", steps);
                            pattern_steps.emplace_back(name, steps);
                        } else {
                            steps = found->second;
                        }
                    }
                    auto item = yyjson_mut_obj(doc);
                    success = success && item && yyjson_mut_arr_add_val(steps, item)
                        && set_string(doc, item, "action", step_action(step.type));
                    if (success && !step.target_path.empty()) success = set_string(doc, item, "target_path", step.target_path);
                    const auto fields = extra_fields(step);
                    const auto add_field = [&](const char* key, size_t index) {
                        return index >= fields.size() || fields[index].empty() || set_string(doc, item, key, fields[index]);
                    };
                    success = success && add_field("subfolder_name", 0)
                        && add_field("target_pattern", 1) && add_field("target_filename", 2)
                        && add_field("new_name", 3) && add_field("search_dir", 4)
                        && add_field("path", 5);
                    if (success && !step.source_path.empty() && step.type == ProcessingStepType::CopyFile)
                        success = set_string(doc, item, "source_file_pattern", step.source_path);
                }
            }
        }
        if (!success) break;
    }
    if (success) success = save_mutable_json(MANAGER_JSON_PATH, doc);
    yyjson_mut_doc_free(doc);
    return success;
}

bool CustomComponentsManager::SyncWithManifests(std::unordered_map<std::string, CustomComponent>& catalog) {
    auto active = yyjson_read_file(CUSTOM_MANIFEST_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    auto disabled = yyjson_read_file(DISABLED_MANIFEST_PATH, YYJSON_READ_NOFLAG, nullptr, nullptr);
    auto active_root = active ? yyjson_doc_get_root(active) : nullptr;
    auto disabled_root = disabled ? yyjson_doc_get_root(disabled) : nullptr;
    auto active_components = active_root ? yyjson_obj_get(active_root, "components") : nullptr;
    auto disabled_components = disabled_root ? yyjson_obj_get(disabled_root, "components") : nullptr;
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) {
        if (active) yyjson_doc_free(active);
        if (disabled) yyjson_doc_free(disabled);
        return false;
    }
    for (auto& [id, component] : catalog) {
        auto installed = active_components ? yyjson_obj_get(active_components, id.c_str()) : nullptr;
        auto turned_off = disabled_components ? yyjson_obj_get(disabled_components, id.c_str()) : nullptr;
        component.is_installed = installed != nullptr;
        component.is_disabled = turned_off != nullptr || sd.DirExists(join_path(DISABLED_BASE_DIR, id));
        auto record = installed ? installed : turned_off;
        if (record) {
            const auto installed_version = json_string(record, "version");
            if (!installed_version.empty()) component.version = installed_version;
            component.tracked_files.clear();
            auto files = yyjson_obj_get(record, "files");
            if (files && yyjson_is_arr(files)) {
                size_t index, max;
                yyjson_val* file;
                yyjson_arr_foreach(files, index, max, file) {
                    const auto text = yyjson_get_str(file);
                    if (text) component.tracked_files.push_back({text, {}});
                }
            }
        }
        component.update_available = !component.latest_version.empty() && component.latest_version != component.version;
    }
    if (active) yyjson_doc_free(active);
    if (disabled) yyjson_doc_free(disabled);
    return true;
}

// -----------------------------------------------------------------------------
// 2. PROCESSING ENGINE (7 STEPS)
// -----------------------------------------------------------------------------

bool CustomComponentsManager::ExecuteProcessingSteps(const CustomComponent& comp, const std::string& downloaded_zip_path, ProgressCallback cb) {
    const auto downloaded_name = basename(downloaded_zip_path);
    for (size_t index = 0; index < comp.install_steps.size(); ++index) {
        const auto& step = comp.install_steps[index];
        const auto pattern = asset_pattern(step);
        if (!pattern.empty() && !wildcard_match(pattern.c_str(), downloaded_name.c_str())) continue;
        if (cb) cb("Installing " + comp.name, comp.install_steps.empty() ? 100.0f : 100.0f * index / comp.install_steps.size());
        if (!ExecuteStep(step, downloaded_zip_path)) return false;
    }
    return true;
}

bool CustomComponentsManager::ExecuteStep(const ProcessingStep& step, const std::string& zip_path) {
    const auto fields = extra_fields(step);
    switch (step.type) {
        case ProcessingStepType::UnzipToRoot: return StepUnzipToRoot(zip_path);
        case ProcessingStepType::UnzipToPath: return StepUnzipToPath(zip_path, step.target_path);
        case ProcessingStepType::UnzipSubfolderToPath: return StepUnzipSubfolderToPath(zip_path, step.source_path, step.target_path);
        case ProcessingStepType::CopyFile: {
            fs::FsNativeSd sd;
            if (R_FAILED(sd.GetFsOpenResult())) return false;
            const auto source_candidate = step.source_path.empty() ? zip_path : step.source_path;
            const auto fields = extra_fields(step);
            if (fields.size() > 5 && fields[5] == "component_extra") {
                auto source = join_path("/config/hats-tools/custom-components/custom-manager/", source_candidate);
                auto destination = sd_path(step.target_path);
                if (source.empty() || destination.empty()) return false;
                if (fields.size() > 6 && fields[6] == "0" && (sd.FileExists(destination) || sd.DirExists(destination))) return true;
                return copy_file(source.s, destination.s);
            }
            auto source = sd_path(source_candidate);
            if (!sd.FileExists(source)) source = sd_path(zip_path);
            if (source.empty()) return false;
            return copy_file(source.s, step.target_path);
        }
        case ProcessingStepType::CopyFileToAutoFolder: return StepCopyFileToAutoFolder(zip_path, step.target_path);
        case ProcessingStepType::FindAndRename: {
            const auto pattern = fields.size() > 1 && !fields[1].empty() ? fields[1] : step.source_path;
            const auto new_name = fields.size() > 2 && !fields[2].empty() ? fields[2] : fields.size() > 3 ? fields[3] : "";
            const auto directory = fields.size() > 4 && !fields[4].empty() ? fields[4] : step.source_path;
            return StepFindAndRename(directory, pattern, new_name);
        }
        case ProcessingStepType::DeleteFile: return StepDeleteFile(step.source_path);
    }
    return false;
}

bool CustomComponentsManager::StepUnzipToRoot(const std::string& zip_path) {
    return extract_zip(zip_path, "/");
}

bool CustomComponentsManager::StepUnzipToPath(const std::string& zip_path, const std::string& target_path) {
    return extract_zip(zip_path, target_path.empty() ? "/" : target_path);
}

bool CustomComponentsManager::StepUnzipSubfolderToPath(const std::string& zip_path, const std::string& subfolder, const std::string& target_path) {
    return extract_zip(zip_path, target_path.empty() ? "/" : target_path, subfolder);
}

bool CustomComponentsManager::StepCopyFile(const std::string& src_path, const std::string& dst_path) {
    return copy_file(src_path, dst_path);
}

bool CustomComponentsManager::StepCopyFileToAutoFolder(const std::string& src_path, const std::string& base_target) {
    const auto source_name = basename(src_path);
    if (source_name.empty()) return false;
    auto target = base_target;
    if (target.empty()) target = "/";
    if (target.back() != '/') target += '/';
    target += source_name;
    return copy_file(src_path, target);
}

bool CustomComponentsManager::StepFindAndRename(const std::string& search_dir, const std::string& target_pattern, const std::string& new_name) {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    return rename_match(sd, sd_path(search_dir), target_pattern, new_name) && R_SUCCEEDED(sd.Commit());
}

bool CustomComponentsManager::StepDeleteFile(const std::string& path) {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    const auto relative = normalize_relative(path);
    if (relative.empty()) return false;
    auto target = sd_path(path);
    bool success = false;
    if (sd.FileExists(target)) success = R_SUCCEEDED(sd.DeleteFile(target));
    else if (sd.DirExists(target)) success = R_SUCCEEDED(sd.DeleteDirectoryRecursively(target));
    if (success && g_tracked_files) {
        std::erase_if(*g_tracked_files, [&relative](const auto& file) {
            return file.rel_path == relative || file.rel_path.starts_with(relative + "/");
        });
    }
    return success && R_SUCCEEDED(sd.Commit());
}

// -----------------------------------------------------------------------------
// 3. OPERAZIONI CICLO DI VITA COMPONENTI
// -----------------------------------------------------------------------------

bool CustomComponentsManager::InstallOrUpdateComponent(CustomComponent& comp, ProgressCallback cb) {
    if (!EnsureDirectories()) return false;
    CleanStagingArea();
    std::vector<std::pair<std::string, std::string>> assets;
    std::string version;
    if (!resolve_release(comp, assets, version)) return false;
    std::vector<CustomComponentFileRecord> tracked;
    auto previous_tracker = g_tracked_files;
    g_tracked_files = &tracked;
    bool success = true;
    for (const auto& [asset_name, url] : assets) {
        const auto name = asset_name.empty() ? basename(url) : basename(asset_name);
        if (name.empty()) { success = false; break; }
        auto download_path = join_path(STAGING_TEMP_DIR, name);
        if (cb) cb("Downloading " + name, 0.0f);
        if (!download_to(url, download_path.s, cb)) { success = false; break; }
        if (!comp.install_steps.empty()) success = ExecuteProcessingSteps(comp, download_path.s, cb);
        else if (download_path.ends_with(".zip")) success = StepUnzipToRoot(download_path.s);
        else success = false;
        if (!success) break;
    }
    g_tracked_files = previous_tracker;
    if (!success) {
        CleanStagingArea();
        return false;
    }
    comp.latest_version = version.empty() ? comp.latest_version : version;
    if (!comp.latest_version.empty()) comp.version = comp.latest_version;
    comp.tracked_files = std::move(tracked);
    comp.is_installed = true;
    comp.is_disabled = false;
    comp.update_available = false;
    const auto record = ManifestEntry{comp.name, comp.version, comp.category, repository_value(comp), [&comp]() {
        std::vector<std::string> files;
        for (const auto& file : comp.tracked_files) files.push_back(file.rel_path);
        return files;
    }()};
    success = update_manifest_entry(CUSTOM_MANIFEST_PATH, comp.id, &record);
    CleanStagingArea();
    return success;
}

bool CustomComponentsManager::DisableComponent(const std::string& comp_id) {
    if (comp_id.empty() || normalize_relative(comp_id) != comp_id || comp_id.find('/') != std::string::npos) return false;
    if (!EnsureDirectories()) return false;
    ManifestEntry entry;
    if (!read_manifest_entry(CUSTOM_MANIFEST_PATH, comp_id, entry)) return false;
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    auto disabled_root = join_path(DISABLED_BASE_DIR, comp_id);
    if (sd.DirExists(disabled_root)) return false;
    std::vector<std::pair<fs::FsPath, fs::FsPath>> moved;
    for (const auto& file : entry.files) {
        auto relative = normalize_relative(file);
        if (relative.empty()) return false;
        auto source = sd_path(relative);
        auto destination = join_path(disabled_root.s, relative);
        if (!sd.FileExists(source) && !sd.DirExists(source)) continue;
        if (sd.FileExists(destination) || sd.DirExists(destination)) return false;
        if (R_FAILED(sd.CreateDirectoryRecursively(parent_path(destination.s).c_str()))) return false;
        const bool directory = sd.DirExists(source);
        const auto rc = directory ? sd.RenameDirectory(source, destination) : sd.RenameFile(source, destination);
        if (R_FAILED(rc)) {
            for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                sd.CreateDirectoryRecursively(parent_path(it->first.s).c_str());
                if (sd.FileExists(it->second)) sd.RenameFile(it->second, it->first);
                else if (sd.DirExists(it->second)) sd.RenameDirectory(it->second, it->first);
            }
            return false;
        }
        moved.emplace_back(source, destination);
    }
    if (R_FAILED(sd.Commit())) {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            sd.CreateDirectoryRecursively(parent_path(it->first.s).c_str());
            if (sd.FileExists(it->second)) sd.RenameFile(it->second, it->first);
            else if (sd.DirExists(it->second)) sd.RenameDirectory(it->second, it->first);
        }
        sd.Commit();
        return false;
    }
    const auto rollback = [&]() {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            sd.CreateDirectoryRecursively(parent_path(it->first.s).c_str());
            if (sd.FileExists(it->second)) sd.RenameFile(it->second, it->first);
            else if (sd.DirExists(it->second)) sd.RenameDirectory(it->second, it->first);
        }
    };
    if (!update_manifest_entry(DISABLED_MANIFEST_PATH, comp_id, &entry)) {
        rollback();
        return false;
    }
    if (!update_manifest_entry(CUSTOM_MANIFEST_PATH, comp_id, nullptr)) {
        update_manifest_entry(DISABLED_MANIFEST_PATH, comp_id, nullptr);
        rollback();
        return false;
    }
    return true;
}

bool CustomComponentsManager::EnableComponent(const std::string& comp_id) {
    if (comp_id.empty() || normalize_relative(comp_id) != comp_id || comp_id.find('/') != std::string::npos) return false;
    if (!EnsureDirectories()) return false;
    ManifestEntry entry;
    if (!read_manifest_entry(DISABLED_MANIFEST_PATH, comp_id, entry)) return false;
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    auto disabled_root = join_path(DISABLED_BASE_DIR, comp_id);
    std::vector<std::pair<fs::FsPath, fs::FsPath>> moved;
    for (const auto& file : entry.files) {
        auto relative = normalize_relative(file);
        if (relative.empty()) return false;
        auto source = join_path(disabled_root.s, relative);
        auto destination = sd_path(relative);
        if (!sd.FileExists(source) && !sd.DirExists(source)) continue;
        if (sd.FileExists(destination) || sd.DirExists(destination)) return false;
        if (R_FAILED(sd.CreateDirectoryRecursively(parent_path(destination.s).c_str()))) return false;
        const bool directory = sd.DirExists(source);
        const auto rc = directory ? sd.RenameDirectory(source, destination) : sd.RenameFile(source, destination);
        if (R_FAILED(rc)) {
            for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                sd.CreateDirectoryRecursively(parent_path(it->first.s).c_str());
                if (sd.FileExists(it->second)) sd.RenameFile(it->second, it->first);
                else if (sd.DirExists(it->second)) sd.RenameDirectory(it->second, it->first);
            }
            return false;
        }
        moved.emplace_back(source, destination);
    }
    if (R_FAILED(sd.Commit())) {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            sd.CreateDirectoryRecursively(parent_path(it->first.s).c_str());
            if (sd.FileExists(it->second)) sd.RenameFile(it->second, it->first);
            else if (sd.DirExists(it->second)) sd.RenameDirectory(it->second, it->first);
        }
        sd.Commit();
        return false;
    }
    const auto rollback = [&]() {
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            sd.CreateDirectoryRecursively(parent_path(it->first.s).c_str());
            if (sd.FileExists(it->second)) sd.RenameFile(it->second, it->first);
            else if (sd.DirExists(it->second)) sd.RenameDirectory(it->second, it->first);
        }
    };
    if (!update_manifest_entry(CUSTOM_MANIFEST_PATH, comp_id, &entry)) {
        rollback();
        return false;
    }
    if (!update_manifest_entry(DISABLED_MANIFEST_PATH, comp_id, nullptr)) {
        update_manifest_entry(CUSTOM_MANIFEST_PATH, comp_id, nullptr);
        rollback();
        return false;
    }
    sd.DeleteDirectoryRecursively(disabled_root);
    return true;
}

bool CustomComponentsManager::DeleteComponent(const std::string& comp_id, bool is_disabled_storage) {
    if (comp_id.empty() || normalize_relative(comp_id) != comp_id || comp_id.find('/') != std::string::npos) return false;
    const auto manifest_path = is_disabled_storage ? DISABLED_MANIFEST_PATH : CUSTOM_MANIFEST_PATH;
    ManifestEntry entry;
    if (!read_manifest_entry(manifest_path, comp_id, entry)) return false;
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    for (const auto& file : entry.files) {
        auto relative = normalize_relative(file);
        if (relative.empty()) continue;
        auto path = is_disabled_storage ? join_path(join_path(DISABLED_BASE_DIR, comp_id).s, relative) : sd_path(relative);
        if (sd.FileExists(path)) {
            if (R_FAILED(sd.DeleteFile(path))) return false;
        } else if (sd.DirExists(path) && R_FAILED(sd.DeleteDirectoryRecursively(path))) return false;
    }
    if (is_disabled_storage) sd.DeleteDirectoryRecursively(join_path(DISABLED_BASE_DIR, comp_id));
    if (R_FAILED(sd.Commit())) return false;
    return update_manifest_entry(manifest_path, comp_id, nullptr);
}

// -----------------------------------------------------------------------------
// 4. NETWORK & UPDATE MANAGEMENT (UI OVERLAY BACKEND)
// -----------------------------------------------------------------------------

bool CustomComponentsManager::FetchVersions(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    size_t index = 0;
    for (auto& [id, component] : catalog) {
        if (cb) cb("Checking " + component.name, catalog.empty() ? 100.0f : 100.0f * index / catalog.size());
        ++index;
        if (component.download_url.find("api.github.com/repos/") == std::string::npos) continue;
        std::vector<std::pair<std::string, std::string>> assets;
        std::string version;
        if (!resolve_release(component, assets, version)) return false;
        component.latest_version = version;
        component.update_available = !version.empty() && version != component.version;
    }
    return true;
}

bool CustomComponentsManager::UpdateAllComponents(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    std::vector<std::string> selected;
    for (const auto& [id, component] : catalog) if (component.update_available && !component.is_disabled) selected.push_back(id);
    return UpdateSelectedComponents(selected, catalog, std::move(cb));
}

bool CustomComponentsManager::UpdateSelectedComponents(const std::vector<std::string>& selected_ids, std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    for (size_t index = 0; index < selected_ids.size(); ++index) {
        auto found = catalog.find(selected_ids[index]);
        if (found == catalog.end() || found->second.is_disabled) continue;
        if (cb) cb("Updating " + found->second.name, selected_ids.empty() ? 100.0f : 100.0f * index / selected_ids.size());
        if (!InstallOrUpdateComponent(found->second, cb)) return false;
    }
    return true;
}

bool CustomComponentsManager::AddOrModifyCustomComponent(const CustomComponent& comp) {
    if (comp.id.empty() || normalize_relative(comp.id) != comp.id || comp.id.find('/') != std::string::npos) return false;
    std::unordered_map<std::string, CustomComponent> catalog;
    if (!LoadManagerCatalog(catalog)) catalog.clear();
    catalog.insert_or_assign(comp.id, comp);
    return SaveManagerCatalog(catalog);
}

// -----------------------------------------------------------------------------
// UTILITIES
// -----------------------------------------------------------------------------

bool CustomComponentsManager::EnsureDirectories() {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return false;
    const std::array<const char*, 4> paths{{
        "/config/hats-tools/custom-components/custom-manager/manager/",
        STAGING_TEMP_DIR,
        DISABLED_BASE_DIR,
        "/config/hats-tools/custom-components/",
    }};
    for (const auto path : paths) if (R_FAILED(sd.CreateDirectoryRecursively(path)) && !sd.DirExists(path)) return false;
    return true;
}

void CustomComponentsManager::CleanStagingArea() {
    fs::FsNativeSd sd;
    if (R_FAILED(sd.GetFsOpenResult())) return;
    fs::FsPath staging{STAGING_TEMP_DIR};
    if (sd.DirExists(staging)) sd.DeleteDirectoryRecursively(staging);
    sd.CreateDirectoryRecursively(staging);
}

} // namespace sphaira::ui::menu::hats
