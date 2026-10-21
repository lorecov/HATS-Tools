#include "custom_components.hpp"

#include "fs.hpp"
#include "app.hpp"
#include "log.hpp"

#include <cstdlib>
#include <yyjson.h>

namespace sphaira::custom {

    // 1. Fetch Versions (Check Updates)
bool FetchVersions(const std::map<std::string, manifest::Component>& components) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }
    // TODO: Caricare components.json, verificare i tag su GitHub e aggiornare custom_manifest.json
    App::Notify("Fetch Versions (Stub)");
    return true;
}

// 2. Update All Components
bool UpdateAllComponents(const std::map<std::string, manifest::Component>& components) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }
    // TODO: Scaricare e applicare i processing steps per tutti i componenti aggiornabili
    App::Notify("Update All Components (Stub)");
    return true;
}

// 3. Update Selected Components
bool UpdateSelectedComponents(const std::set<std::string>& selected_ids) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }
    // TODO: Scaricare e applicare i processing steps per i soli ID selezionati
    App::Notify("Update Selected Components (Stub)");
    return true;
}

// 4. Add New Component
bool AddNewComponent() {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }
    // TODO: Aprire l'interfaccia UI dell'editor per creare una nuova entry in components.json
    App::Notify("Add New Component (Stub)");
    return true;
}

// 5. Modify Selected Component
bool ModifySelectedComponent(const std::string& component_id) {
    if (!EnsureDirectories() || !EnsureCatalogExists()) {
        return false;
    }
    // TODO: Aprire l'interfaccia UI dell'editor con i dati del componente per modificarli
    App::Notify("Modify Selected Component (Stub)");
    return true;
}


// Helpers for managing the catalog file

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
    constexpr fs::FsPath catalog_path{"/config/hats-tools/custom-components/manager/components.json"};
    constexpr u8 empty_catalog[]{'{', '}', '\n'};

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

bool LoadCatalog(std::string& json) {
    constexpr const char* catalog_path = "/config/hats-tools/custom-components/manager/components.json";
    if (!EnsureCatalogExists()) {
        return false;
    }

    auto* doc = yyjson_read_file(catalog_path, YYJSON_READ_NOFLAG, nullptr, nullptr);
    if (!doc) {
        return false;
    }

    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    size_t json_size{};
    auto* serialized = yyjson_write(doc, YYJSON_WRITE_PRETTY, &json_size);
    yyjson_doc_free(doc);
    if (!serialized) {
        return false;
    }

    json.assign(serialized, json_size);
    std::free(serialized);
    return true;
}

bool SaveCatalog(const std::string& json) {
    constexpr const char* catalog_path = "/config/hats-tools/custom-components/manager/components.json";
    auto* doc = yyjson_read(json.data(), json.size(), YYJSON_READ_NOFLAG);
    if (!doc) {
        return false;
    }

    auto* root = yyjson_doc_get_root(doc);
    if (!root || !yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        return false;
    }

    auto* mutable_doc = yyjson_doc_mut_copy(doc, nullptr);
    yyjson_doc_free(doc);
    if (!mutable_doc) {
        return false;
    }

    if (!EnsureDirectories()) {
        yyjson_mut_doc_free(mutable_doc);
        return false;
    }

    yyjson_write_err err{};
    const bool success = yyjson_mut_write_file(catalog_path, mutable_doc, YYJSON_WRITE_PRETTY, nullptr, &err);
    yyjson_mut_doc_free(mutable_doc);
    if (!success) {
        return false;
    }

    return true;
}

} // namespace sphaira::custom
