
#pragma once

#include "custom_catalog.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

namespace catalog = ::sphaira::custom::catalog;

struct CustomComponentEditorDraft {
    struct Asset {
        std::string pattern;
        std::vector<catalog::ProcessingStep> processing_steps;
    };

    catalog::CustomComponent component;
    std::vector<Asset> assets;
    std::vector<catalog::ProcessingStep> direct_url_steps;
};

struct CustomComponentProcessingStepsEditor final : MenuBase {
    explicit CustomComponentProcessingStepsEditor(std::vector<catalog::ProcessingStep>& steps, std::string title);

    auto GetShortTitle() const -> const char* override { return "Processing Steps"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    void EditStep(size_t index);
    void AddStep();
    void RemoveStep();
    void MoveStep(s64 direction);
    void EditText(std::string& value, const std::string& title);
    void DrawStep(NVGcontext* vg, Theme* theme, const Vec4& rect, size_t index) const;

    std::vector<catalog::ProcessingStep>& m_steps;
    std::unique_ptr<List> m_list;
    s64 m_index{};
};

struct CustomComponentEditor final : MenuBase {
    using SaveCallback = std::function<void(const CustomComponentEditorDraft&)>;

    explicit CustomComponentEditor(catalog::CustomComponent component = {}, bool is_new = true, SaveCallback on_save = {});

    auto GetShortTitle() const -> const char* override { return "Component Editor"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    enum class Tab {
        Details,
        Source,
    };

    enum class RowType {
        ComponentId,
        Name,
        Category,
        Description,
        SourceType,
        SourceLocation,
        AssetPattern,
        NoAssetsInfo,
    };

    struct Row {
        RowType type;
        size_t index{};
    };

    void BuildRows();
    void SetIndex(s64 index);
    void SetTab(Tab tab);
    void OnSelect();
    void OnAdd();
    void OnRemove();
    void OnSave();
    void EditText(std::string& value, const std::string& title);
    void OpenSelectedSteps();
    void UpdateSourceActions();
    void DrawTabs(NVGcontext* vg, Theme* theme) const;
    std::string GetRowValue(const Row& row) const;

    catalog::CustomComponent m_component;
    std::vector<CustomComponentEditorDraft::Asset> m_assets;
    std::vector<catalog::ProcessingStep> m_direct_url_steps;
    std::vector<Row> m_rows;
    std::unique_ptr<List> m_list;
    SaveCallback m_on_save;
    Tab m_tab{Tab::Details};
    s64 m_index{};
    size_t m_selected_asset{};
};

} // namespace sphaira::ui::menu::hats
