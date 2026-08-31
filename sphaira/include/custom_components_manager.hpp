#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include "fs.hpp"
#include "manifest.hpp"

namespace sphaira::ui::menu::hats {

// -----------------------------------------------------------------------------
// ENUMERAZIONI E STRUTTURE DATI
// -----------------------------------------------------------------------------

/**
 * @brief I 7 Processing Steps supportati per l'installazione/manipolazione dei pacchetti.
 */
enum class ProcessingStepType {
    UnzipToRoot,             // 1. Estrae ZIP in sdmc:/
    UnzipToPath,             // 2. Estrae ZIP in un percorso specifico
    UnzipSubfolderToPath,    // 3. Estrae sotto-cartella ZIP in percorso target
    CopyFile,                // 4. Copia un singolo file (src -> dst)
    CopyFileToAutoFolder,    // 5. Copia un file in cartella automatica (es. basata su metadati)
    FindAndRename,           // 6. Cerca file/cartella e rinomina
    DeleteFile               // 7. Elimina file o cartella
};

/**
 * @brief Rappresenta un singolo passo di elaborazione per un componente.
 */
struct ProcessingStep {
    ProcessingStepType type;
    std::string source_path;
    std::string target_path;
    std::string extra_param; // Usato per subfolder, nuovi nomi, etc.
};

/**
 * @brief Dettaglio file installato per tracciamento nel manifest.
 */
struct CustomComponentFileRecord {
    std::string rel_path;
    std::string hash;
};

/**
 * @brief Rappresentazione completa di un Custom Component.
 */
struct CustomComponent {
    std::string id;
    std::string name;
    std::string version;
    std::string latest_version;
    std::string category;
    std::string description;
    std::string download_url;
    bool is_installed{false};
    bool is_disabled{false};
    bool update_available{false};

    std::vector<ProcessingStep> install_steps;
    std::vector<CustomComponentFileRecord> tracked_files;
};

/**
 * @brief Informazioni sull'aggiornamento remoto del componente.
 */
struct RemoteVersionInfo {
    std::string id;
    std::string version;
    std::string download_url;
    std::vector<ProcessingStep> steps;
};

/**
 * @brief Callback per riportare lo stato di avanzamento delle operazioni.
 */
using ProgressCallback = std::function<void(const std::string& status_text, float progress_percentage)>;

// -----------------------------------------------------------------------------
// CLASS CUSTOM COMPONENTS MANAGER
// -----------------------------------------------------------------------------

class CustomComponentsManager {
public:

    // Percorsi standard del sistema custom
    static constexpr const char* MANAGER_JSON_PATH = "/config/hats-tools/custom-components/manager/components.json";
    static constexpr const char* STAGING_TEMP_DIR   = "/config/hats-tools/custom-components/components-manager/temp/";
    static constexpr const char* DISABLED_BASE_DIR  = "/config/hats-tools/custom-components/disabled-components/";

    // -------------------------------------------------------------------------
    // 1. GESTIONE MANIFEST & PARSING JSON
    // -------------------------------------------------------------------------

    /**
     * @brief Carica il catalogo manager locale (`components.json`).
     */
    static bool LoadManagerCatalog(std::unordered_map<std::string, CustomComponent>& out_components);

    /**
     * @brief Salva il catalogo manager locale (`components.json`).
     */
    static bool SaveManagerCatalog(const std::unordered_map<std::string, CustomComponent>& components);

    /**
     * @brief Sincronizza lo stato di `components.json` con `custom_manifest.json` e componenti disabilitati.
     */
    static bool SyncWithManifests(std::unordered_map<std::string, CustomComponent>& catalog);

    // -------------------------------------------------------------------------
    // 2. PROCESSING ENGINE (7 STEPS)
    // -------------------------------------------------------------------------

    /**
     * @brief Esegue una lista di ProcessingStep per un componente specifico.
     */
    static bool ExecuteProcessingSteps(const CustomComponent& comp, const std::string& downloaded_zip_path, ProgressCallback cb = nullptr);

    /**
     * @brief Dispatcher per la singola operazione di processing step.
     */
    static bool ExecuteStep(const ProcessingStep& step, const std::string& zip_path);

    // I 7 Step individuali:
    static bool StepUnzipToRoot(const std::string& zip_path);
    static bool StepUnzipToPath(const std::string& zip_path, const std::string& target_path);
    static bool StepUnzipSubfolderToPath(const std::string& zip_path, const std::string& subfolder, const std::string& target_path);
    static bool StepCopyFile(const std::string& src_path, const std::string& dst_path);
    static bool StepCopyFileToAutoFolder(const std::string& src_path, const std::string& base_target);
    static bool StepFindAndRename(const std::string& search_dir, const std::string& target_pattern, const std::string& new_name);
    static bool StepDeleteFile(const std::string& path);

    // -------------------------------------------------------------------------
    // 3. OPERAZIONI CICLO DI VITA COMPONENTI
    // -------------------------------------------------------------------------

    /**
     * @brief Scarica, estrae e registra un nuovo componente o aggiornamento.
     */
    static bool InstallOrUpdateComponent(CustomComponent& comp, ProgressCallback cb = nullptr);

    /**
     * @brief Sposta i file di un componente nella cartella disabled-components.
     */
    static bool DisableComponent(const std::string& comp_id);

    /**
     * @brief Ripristina i file di un componente dalla cartella disabled-components alla SD.
     */
    static bool EnableComponent(const std::string& comp_id);

    /**
     * @brief Rimuove definitivamente i file e il componente dai manifest.
     */
    static bool DeleteComponent(const std::string& comp_id, bool is_disabled_storage);

    // -------------------------------------------------------------------------
    // 4. NETWORK & UPDATE MANAGEMENT (UI OVERLAY BACKEND)
    // -------------------------------------------------------------------------

    /**
     * @brief Controlla le nuove versioni disponibili online per tutti i componenti Custom.
     */
    static bool FetchVersions(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb = nullptr);

    /**
     * @brief Aggiorna tutti i componenti per cui è disponibile un update.
     */
    static bool UpdateAllComponents(std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb = nullptr);

    /**
     * @brief Aggiorna solo i componenti selezionati dall'utente nella UI.
     */
    static bool UpdateSelectedComponents(const std::vector<std::string>& selected_ids, std::unordered_map<std::string, CustomComponent>& catalog, ProgressCallback cb = nullptr);

    /**
     * @brief Aggiunge o sovrascrive una definizione componente custom in `components.json`.
     */
    static bool AddOrModifyCustomComponent(const CustomComponent& comp);

    // -------------------------------------------------------------------------
    // UTILITIES
    // -------------------------------------------------------------------------

    /**
     * @brief Assicura che le cartelle di staging e configurazione esistano.
     */
    static bool EnsureDirectories();

    /**
     * @brief Pulizia file temporanei di download/staging.
     */
    static void CleanStagingArea();
};

} // namespace sphaira::ui::menu::hats
