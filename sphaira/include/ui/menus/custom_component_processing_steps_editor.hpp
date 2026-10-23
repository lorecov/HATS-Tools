#pragma once

#include "custom_catalog.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"

#include <string>
#include <vector>

namespace sphaira::ui::menu::hats {

struct CustomComponentProcessingStepsEditor final : MenuBase {
    explicit CustomComponentProcessingStepsEditor(std::vector<catalog::ProcessingStep>& steps, std::string title);

    auto GetShortTitle() const -> const char* override { return "Processing Steps"; }
    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;

private:
    void SetIndex(s64 index);
    bool HasUnsavedChanges() const;
    void RequestBack();
    void ChooseStepType(size_t index, bool add_step);
    void ConfigureStep(size_t index);
    void AddStep();
    void RemoveStep();
    void SaveSteps();
    void EditText(std::string& value, const std::string& title);
    void DrawStep(NVGcontext* vg, Theme* theme, const Vec4& rect, size_t index) const;

    std::vector<catalog::ProcessingStep>& m_target_steps;
    std::vector<catalog::ProcessingStep> m_steps;
    std::unique_ptr<List> m_list;
    s64 m_index{};
};

} // namespace sphaira::ui::menu::hats
