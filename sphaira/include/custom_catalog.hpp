#pragma once

#include <map>
#include <string>
#include <vector>

namespace sphaira::custom::catalog {

enum class StepType {
    UnzipToRoot,
    UnzipToPath,
    UnzipSubfolderToPath,
    CopyFile,
    CopyFileToAutoFolder,
    FindAndRename,
    DeleteFile,
    FindAndCopy,
};

struct ProcessingStep {
    StepType type{StepType::UnzipToRoot};
    std::string target_path;
    std::string subfolder_name;
    std::string source_file_pattern;
    std::string target_filename;
};

struct AssetPattern {
    std::string pattern;
    std::vector<ProcessingStep> processing_steps;
};

struct CustomComponent {
    std::string id;
    std::string name;
    std::string description;
    std::string category;
    std::string source_type;
    std::string repo;
    std::string version;
    std::string latest_version;
    std::vector<std::string> asset_patterns;
    std::vector<ProcessingStep> processing_steps;
};

struct GitHubVersionEntry {
    std::string id;
    std::string repo;
    std::string latest_version;
};

struct Catalog {
    std::map<std::string, CustomComponent> components;
};

inline constexpr char CATALOG_PATH[] = "/config/hats-tools/custom-components/manager/components.json";

bool LoadCatalog(Catalog& out);
bool SaveCatalog(const Catalog& catalog);
bool AddComponent(const CustomComponent& component, const std::vector<AssetPattern>& assets);
bool LoadComponent(const std::string& id, CustomComponent& component, std::vector<AssetPattern>& assets);
bool UpdateComponent(const std::string& id, const CustomComponent& component, const std::vector<AssetPattern>& assets);
bool RemoveComponentsFromCatalog(const std::vector<std::string>& ids);
bool LoadGitHubVersionEntries(std::vector<GitHubVersionEntry>& entries);
bool SaveLatestVersions(const std::map<std::string, std::string>& versions);

CustomComponent* FindComponent(Catalog& catalog, const std::string& id);
const CustomComponent* FindComponent(const Catalog& catalog, const std::string& id);
bool UpsertComponent(Catalog& catalog, CustomComponent component);
bool RemoveComponent(Catalog& catalog, const std::string& id);

} // namespace sphaira::custom::catalog
