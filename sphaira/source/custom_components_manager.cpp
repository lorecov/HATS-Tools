#include "custom_components_manager.hpp"
#include "log.hpp"
#include "ui/sidebar.hpp"
#include "app.hpp"
#include "ui/menus/uninstaller_menu.hpp"

namespace sphaira::ui::menu::hats {

// -----------------------------------------------------------------------------
// 1. GESTIONE MANIFEST & PARSING JSON
// -----------------------------------------------------------------------------

bool CustomComponentsManager::LoadManagerCatalog(std::unordered_map<std::string, CustomComponent>& out_components) {
    // TODO: Implementare LoadManagerCatalog (Parsing di /config/hats-tools/custom-components/manager/components.json)
    log_write("[CUSTOM_MGR] LoadManagerCatalog (stub)\n");
    return true;
}

bool CustomComponentsManager::SaveManagerCatalog(const std::unordered_map<std::string, CustomComponent>& components) {
    // TODO: Implementare SaveManagerCatalog (Scrittura JSON su file components.json)
    log_write("[CUSTOM_MGR] SaveManagerCatalog (stub)\n");
    return true;
}

bool CustomComponentsManager::SyncWithManifests(std::unordered_map<std::string, CustomComponent>& catalog) {
    // TODO: Implementare SyncWithManifests (Merge con custom_manifest.json e cartella disabled)
    log_write("[CUSTOM_MGR] SyncWithManifests (stub)\n");
    return true;
}

// -----------------------------------------------------------------------------
// 2. PROCESSING ENGINE (7 STEPS)
// -----------------------------------------------------------------------------

bool CustomComponentsManager::ExecuteProcessingSteps(const CustomComponent& comp, const std::string& downloaded_zip_path, ProgressCallback cb) {
    // TODO: Implementare ExecuteProcessingSteps (Ciclo sui passi definiti per il componente)
    log_write("[CUSTOM_MGR] ExecuteProcessingSteps per %s (stub)\n", comp.id.c_str());
    return true;
}

bool CustomComponentsManager::ExecuteStep(const ProcessingStep& step, const std::string& zip_path) {
    // TODO: Implementare ExecuteStep (Dispatcher verso gli 7 step individuali)
    log_write("[CUSTOM_MGR] ExecuteStep di tipo %d (stub)\n", static_cast<int>(step.type));
    return true;
}

bool CustomComponentsManager::StepUnzipToRoot(const std::string& zip_path) {
    // TODO: Implementare Step 1: Unzip to SD root (sdmc:/)
    log_write("[CUSTOM_MGR] Step 1: UnzipToRoot (stub)\n");
    return true;
}

bool CustomComponentsManager::StepUnzipToPath(const std::string& zip_path, const std::string& target_path) {
    // TODO: Implementare Step 2: Unzip to specific path
    log_write("[CUSTOM_MGR] Step 2: UnzipToPath (%s) (stub)\n", target_path.c_str());
    return true;
}

bool CustomComponentsManager::StepUnzipSubfolderToPath(const std::string& zip_path, const std::string& subfolder, const std::string& target_path) {
    // TODO: Implementare Step 3: Unzip subfolder to target path
    log_write("[CUSTOM_MGR] Step 3: UnzipSubfolderToPath (%s -> %s) (stub)\n", subfolder.c_str(), target_path.c_str());
    return true;
}

bool CustomComponentsManager::StepCopyFile(const std::string& src_path, const std::string& dst_path) {
    // TODO: Implementare Step 4: Copy single file
    log_write("[CUSTOM_MGR] Step 4: CopyFile (%s -> %s) (stub)\n", src_path.c_str(), dst_path.c_str());
    return true;
}

bool CustomComponentsManager::StepCopyFileToAutoFolder(const std::string& src_path, const std::string& base_target) {
    // TODO: Implementare Step 5: Copy file to auto-generated folder
    log_write("[CUSTOM_MGR] Step 5: CopyFileToAutoFolder (stub)\n");
    return true;
}

bool CustomComponentsManager::StepFindAndRename(const std::string& search_dir, const std::string& target_pattern, const std::string& new_name) {
    // TODO: Implementare Step 6: Find and rename
    log_write("[CUSTOM_MGR] Step 6: FindAndRename (%s -> %s) (stub)\n", target_pattern.c_str(), new_name.c_str());
    return true;
}

bool CustomComponentsManager::StepDeleteFile(const std::string& path) {
    // TODO: Implementare Step 7: Delete file or directory
    log_write("[CUSTOM_MGR] Step 7: DeleteFile (%s) (stub)\n", path.c_str());
    return true;
}

// -----------------------------------------------------------------------------
// 3. OPERAZIONI CICLO DI VITA COMPONENTI
// -----------------------------------------------------------------------------

bool CustomComponentsManager::InstallOrUpdateComponent(CustomComponent& comp, ProgressCallback cb) {
    // TODO: Implementare InstallOrUpdateComponent (Download ZIP in staging -> ExecuteProcessingSteps -> Aggiornamento manifest)
    log_write("[CUSTOM_MGR] InstallOrUpdateComponent per %s (stub)\n", comp.id.c_str());
    return true;
}

bool CustomComponentsManager::DisableComponent(const std::string& comp_id) {
    // TODO: Implementare DisableComponent (Spostamento file in /disabled-components/<id>/)
    log_write("[CUSTOM_MGR] DisableComponent %s (stub)\n", comp_id.c_str());
    return true;
}

bool CustomComponentsManager::EnableComponent(const std::string& comp_id) {
    // TODO: Implementare EnableComponent (Ripristino file da /disabled-components/<id>/ alla SD)
    log_write("[CUSTOM_MGR] EnableComponent %s (stub)\n", comp_id.c_str());
    return true;
}

bool CustomComponentsManager::DeleteComponent(const std::string& comp_id, bool is_disabled_storage) {
    // TODO: Implementare DeleteComponent (Cancellazione file fisici e voci nel manifest)
    log_write("[CUSTOM_MGR] DeleteComponent %s (stub)\n", comp_id.c_str());
    return true;
}

// -----------------------------------------------------------------------------
// 4. NETWORK & UPDATE MANAGEMENT (UI OVERLAY BACKEND)
// -----------------------------------------------------------------------------

void CustomComponentsManager::OpenCustomToolsSidebar(void* menu_ctx) {
    auto* menu = static_cast<UninstallerMenu*>(menu_ctx);
    if (!menu) return;

    auto options = std::make_unique<ui::Sidebar>("Custom Components Tools", "", ui::Sidebar::Side::RIGHT);
    
    options->Add(std::make_unique<ui::SidebarEntryCallback>(
        "Fetch Versions (Check Updates)", 
        [menu]() {
            // TODO: Implementare chiamata a FetchVersions
            log_write("[CUSTOM_MGR] Callback Fetch Versions (stub)\n");
        }, 
        true
    ));

    options->Add(std::make_unique<ui::SidebarEntryCallback>(
        "Update All Components", 
        [menu]() {
            // TODO: Implementare chiamata a UpdateAllComponents
            log_write("[CUSTOM_MGR] Callback Update All (stub)\n");
        }, 
        true
    ));

    options->Add(std::make_unique<ui::SidebarEntryCallback>(
        "Update Selected Components", 
        [menu]() {
            // TODO: Implementare chiamata a UpdateSelectedComponents
            log_write("[CUSTOM_MGR] Callback Update Selected (stub)\n");
        }, 
        true
    ));

    options->Add(std::make_unique<ui::SidebarEntryCallback>(
        "Add New Component", 
        [menu]() {
            // TODO: Apertura CustomComponentEditor
            log_write("[CUSTOM_MGR] Callback Add New Component (stub)\n");
        }, 
        true
    ));

    options->Add(std::make_unique<ui::SidebarEntryCallback>(
        "Modify Selected Component", 
        [menu]() {
            // TODO: Apertura CustomComponentEditor per elemento selezionato
            log_write("[CUSTOM_MGR] Callback Modify Selected Component (stub)\n");
        }, 
        true
    ));

    App::Push(std::move(options));
}

bool CustomComponentsManager::FetchVersions(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    // TODO: Implementare FetchVersions (Download info di rete / API check versioni remote)
    log_write("[CUSTOM_MGR] FetchVersions (stub)\n");
    return true;
}

bool CustomComponentsManager::UpdateAllComponents(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    // TODO: Implementare UpdateAllComponents (Iterazione su tutti i componenti con update_available == true)
    log_write("[CUSTOM_MGR] UpdateAllComponents (stub)\n");
    return true;
}

bool CustomComponentsManager::UpdateSelectedComponents(const std::vector<std::string>& selected_ids, std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    // TODO: Implementare UpdateSelectedComponents (Aggiornamento filtrato degli ID passati)
    log_write("[CUSTOM_MGR] UpdateSelectedComponents su %zu elementi (stub)\n", selected_ids.size());
    return true;
}

bool CustomComponentsManager::AddOrModifyCustomComponent(const CustomComponent& comp) {
    // TODO: Implementare AddOrModifyCustomComponent (Inserimento/Modifica e salvataggio catalogo)
    log_write("[CUSTOM_MGR] AddOrModifyCustomComponent %s (stub)\n", comp.id.c_str());
    return true;
}

// -----------------------------------------------------------------------------
// UTILITIES
// -----------------------------------------------------------------------------

bool CustomComponentsManager::EnsureDirectories() {
    // TODO: Implementare EnsureDirectories (Creazione cartelle manager, temp, disabled se non esistono)
    log_write("[CUSTOM_MGR] EnsureDirectories (stub)\n");
    return true;
}

void CustomComponentsManager::CleanStagingArea() {
    // TODO: Implementare CleanStagingArea (Svuotamento cartella temp)
    log_write("[CUSTOM_MGR] CleanStagingArea (stub)\n");
}

} // namespace sphaira::ui::menu::hats
