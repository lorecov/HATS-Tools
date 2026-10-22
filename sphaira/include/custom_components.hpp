#pragma once

#include "manifest.hpp"
#include <string>
#include <vector>
#include <map>
#include <set>

namespace sphaira::custom {

// 1. Fetch Versions (Check Updates)
// Prende l'elenco dei componenti custom e verifica aggiornamenti remoti
bool FetchVersions(const std::map<std::string, manifest::Component>& components);

// 2. Update All Components
// Aggiorna tutti i componenti presenti nel manifest custom
bool UpdateAllComponents(const std::map<std::string, manifest::Component>& components);

// 3. Update Selected Components
// Riceve la lista degli ID selezionati dall'utente (m_selected_ids) e li aggiorna
bool UpdateSelectedComponents(const std::set<std::string>& selected_ids);

// 4. Add New Component
// Apre o avvia il flusso per aggiungere un nuovo componente custom
bool AddNewComponent();

// 5. Modify Selected Component
// Riceve l'ID del singolo componente selezionato e permette di modificarne le impostazioni
bool ModifySelectedComponent(const std::string& component_id);

// Helpers for managing the catalog file

bool EnsureDirectories();
bool CleanStagingArea();
bool EnsureCatalogExists();
bool LoadCatalog(std::string& json);
bool SaveCatalog(const std::string& json);

} // namespace sphaira::custom
