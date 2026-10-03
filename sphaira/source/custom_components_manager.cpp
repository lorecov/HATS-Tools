#include "custom_components_manager.hpp"

namespace sphaira::ui::menu::hats {

// -----------------------------------------------------------------------------
// 1. GESTIONE MANIFEST & PARSING JSON
// -----------------------------------------------------------------------------

bool CustomComponentsManager::LoadManagerCatalog(std::unordered_map<std::string, CustomComponent>& out_components) {
    // TODO: Implementare LoadManagerCatalog (Parsing di /config/hats-tools/custom-components/manager/components.json)
    return true;
}

bool CustomComponentsManager::SaveManagerCatalog(const std::unordered_map<std::string, CustomComponent>& components) {
    // TODO: Implementare SaveManagerCatalog (Scrittura JSON su file components.json)
    return true;
}

bool CustomComponentsManager::SyncWithManifests(std::unordered_map<std::string, CustomComponent>& catalog) {
    // TODO: Implementare SyncWithManifests (Merge con custom_manifest.json e cartella disabled)
    return true;
}

// -----------------------------------------------------------------------------
// 2. PROCESSING ENGINE (7 STEPS)
// -----------------------------------------------------------------------------

bool CustomComponentsManager::ExecuteProcessingSteps(const CustomComponent& comp, const std::string& downloaded_zip_path, ProgressCallback cb) {
    // TODO: Implementare ExecuteProcessingSteps (Ciclo sui passi definiti per il componente)
    return true;
}

bool CustomComponentsManager::ExecuteStep(const ProcessingStep& step, const std::string& zip_path) {
    // TODO: Implementare ExecuteStep (Dispatcher verso gli 7 step individuali)
    return true;
}

bool CustomComponentsManager::StepUnzipToRoot(const std::string& zip_path) {
    // TODO: Implementare Step 1: Unzip to SD root (sdmc:/)
    return true;
}

bool CustomComponentsManager::StepUnzipToPath(const std::string& zip_path, const std::string& target_path) {
    // TODO: Implementare Step 2: Unzip to specific path
    return true;
}

bool CustomComponentsManager::StepUnzipSubfolderToPath(const std::string& zip_path, const std::string& subfolder, const std::string& target_path) {
    // TODO: Implementare Step 3: Unzip subfolder to target path
    return true;
}

bool CustomComponentsManager::StepCopyFile(const std::string& src_path, const std::string& dst_path) {
    // TODO: Implementare Step 4: Copy single file
    return true;
}

bool CustomComponentsManager::StepCopyFileToAutoFolder(const std::string& src_path, const std::string& base_target) {
    // TODO: Implementare Step 5: Copy file to auto-generated folder
    return true;
}

bool CustomComponentsManager::StepFindAndRename(const std::string& search_dir, const std::string& target_pattern, const std::string& new_name) {
    // TODO: Implementare Step 6: Find and rename
    return true;
}

bool CustomComponentsManager::StepDeleteFile(const std::string& path) {
    // TODO: Implementare Step 7: Delete file or directory
    return true;
}

// -----------------------------------------------------------------------------
// 3. OPERAZIONI CICLO DI VITA COMPONENTI
// -----------------------------------------------------------------------------

bool CustomComponentsManager::InstallOrUpdateComponent(CustomComponent& comp, ProgressCallback cb) {
    // TODO: Implementare InstallOrUpdateComponent (Download ZIP in staging -> ExecuteProcessingSteps -> Aggiornamento manifest)
    return true;
}

bool CustomComponentsManager::DisableComponent(const std::string& comp_id) {
    // TODO: Implementare DisableComponent (lo spostamento lo lasciamo fare da uninstaller_menu.cpp, qui modifichiamo lo stato del pacchetto (in compoents.json), la funzione dovrà essere richiamata esclusivamente da uninstaller_menu.cpp)
    return true;
}

bool CustomComponentsManager::EnableComponent(const std::string& comp_id) {
    // TODO: Implementare EnableComponent (funzione opposta di DisableComponent)
    return true;
}

bool CustomComponentsManager::DeleteComponent(const std::string& comp_id, bool is_disabled_storage) {
    // TODO: Implementare DeleteComponent (per la cancellazione ci pensa uninstaller_menu.cpp qui rimuoviamo solamente il componente da components.json)
    return true;
}

// -----------------------------------------------------------------------------
// 4. NETWORK & UPDATE MANAGEMENT (UI OVERLAY BACKEND)
// -----------------------------------------------------------------------------

bool CustomComponentsManager::FetchVersions(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    // TODO: Implementare FetchVersions (Download info di rete / API check versioni remote)
    return true;
}

bool CustomComponentsManager::UpdateAllComponents(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    // TODO: Implementare UpdateAllComponents (Iterazione su tutti i componenti con update_available == true)
    return true;
}

bool CustomComponentsManager::UpdateSelectedComponents(const std::vector<std::string>& selected_ids, std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb) {
    // TODO: Implementare UpdateSelectedComponents (Aggiornamento filtrato degli ID passati)
    return true;
}

bool CustomComponentsManager::AddOrModifyCustomComponent(const CustomComponent& comp) {
    // TODO: Implementare AddOrModifyCustomComponent (Inserimento/Modifica e salvataggio catalogo UI da implementare + logica del menu)
    return true;
}

// -----------------------------------------------------------------------------
// UTILITIES
// -----------------------------------------------------------------------------

bool CustomComponentsManager::EnsureDirectories() {
    // TODO: Implementare EnsureDirectories (Creazione cartelle manager, temp, disabled se non esistono)
    return true;
}

void CustomComponentsManager::CleanStagingArea() {
    // TODO: Implementare CleanStagingArea (Svuotamento cartella temp)
}

} // namespace sphaira::ui::menu::hats
