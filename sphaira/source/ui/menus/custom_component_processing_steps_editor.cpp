#include "ui/menus/custom_component_processing_steps_editor.hpp"

#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"

#include "app.hpp"
#include "i18n.hpp"
#include "swkbd.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace sphaira::ui::menu::hats {
namespace {

constexpr const char* STEP_NAMES[]{
    "Unzip to Root",
    "Unzip to Path",
    "Copy File",
    "Copy File to Auto Folder",
    "Find and Rename",
    "Delete File",
    "Find and Copy",
    "Unzip Subfolder to Path",
};

constexpr catalog::StepType STEP_TYPES[]{
    catalog::StepType::UnzipToRoot,
    catalog::StepType::UnzipToPath,
    catalog::StepType::CopyFile,
    catalog::StepType::CopyFileToAutoFolder,
    catalog::StepType::FindAndRename,
    catalog::StepType::DeleteFile,
    catalog::StepType::FindAndCopy,
    catalog::StepType::UnzipSubfolderToPath,
};

const char* GetStepName(catalog::StepType type) {
    for (size_t i = 0; i < std::size(STEP_TYPES); ++i) {
        if (STEP_TYPES[i] == type) {
            return STEP_NAMES[i];
        }
    }
    return "Unknown";
}

class ProcessingStepValuesEditor final : public ui::Widget {
public:
    using Field = std::pair<std::string, std::string*>;

    ProcessingStepValuesEditor(std::vector<Field> fields, std::string title)
        : m_fields{std::move(fields)}
        , m_title{std::move(title)} {
        SetActions(
            std::make_pair(Button::A, Action{"Edit"_i18n, [this](){
                EditSelected();
            }}),
            std::make_pair(Button::B, Action{"Back"_i18n, [this](){
                SetPop();
            }})
        );

        m_pos = {0.f, SCREEN_HEIGHT - 80.f - 140.f - 300.f, SCREEN_WIDTH, 80.f + 140.f + 300.f};
        const float line_top = m_pos.y + 70.f;
        const float line_bottom = SCREEN_HEIGHT - 73.f;
        const Vec4 list_bounds{0.f, line_top, SCREEN_WIDTH, line_bottom - line_top};
        const Vec4 list_pos{280.f, line_top + 43.f, SCREEN_HEIGHT, 60.f};
        m_list = std::make_unique<List>(1, 6, list_bounds, list_pos);
    }

    void Update(Controller* controller, TouchInfo* touch) override {
        Widget::Update(controller, touch);
        if (m_fields.empty()) {
            return;
        }

        m_list->OnUpdate(controller, touch, m_index, m_fields.size(), [this](bool touched, auto index) {
            if (touched && m_index == index) {
                FireAction(Button::A);
            } else {
                App::PlaySoundEffect(SoundEffect::Focus);
                m_index = index;
            }
        });
    }

    void Draw(NVGcontext* vg, Theme* theme) override {
        gfx::dimBackground(vg);
        gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP));
        gfx::drawText(vg, m_pos.x + 70.f, m_pos.y + 28.f, 24.f,
            theme->GetColour(ThemeEntryID_TEXT), m_title.c_str());
        const float line_top = m_pos.y + 70.f;
        const float line_bottom = SCREEN_HEIGHT - 73.f;
        gfx::drawRect(vg, 30.f, line_top, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
        gfx::drawRect(vg, 30.f, line_bottom, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

        m_list->Draw(vg, theme, m_fields.size(), [this](auto* vg, auto* theme, const auto& rect, auto index) {
            const auto& [x, y, w, h] = rect;
            const bool selected = m_index == static_cast<s64>(index);
            const auto colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
            if (selected) {
                gfx::drawRectOutline(vg, theme, 4.f, rect);
            } else if (index + 1 < m_fields.size()) {
                gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
            }

            gfx::drawText(vg, x + 15.f, y + h / 2.f, 20.f, m_fields[index].first.c_str(),
                nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, colour);
            gfx::drawTextBox(vg, x + w * 0.42f, y + h / 2.f, 18.f, w * 0.56f,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO),
                m_fields[index].second->c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        });
        Widget::Draw(vg, theme);
    }

private:
    void EditSelected() {
        if (m_index < 0 || static_cast<size_t>(m_index) >= m_fields.size()) {
            return;
        }

        auto& [label, value] = m_fields[m_index];
        std::string updated;
        if (R_SUCCEEDED(swkbd::ShowText(updated, label.c_str(), label.c_str(), value->c_str(), 0, 512))) {
            *value = std::move(updated);
        }
    }

    std::vector<Field> m_fields;
    std::string m_title;
    std::unique_ptr<List> m_list;
    s64 m_index{};
}

} // namespace

CustomComponentProcessingStepsEditor::CustomComponentProcessingStepsEditor(
    std::vector<catalog::ProcessingStep>& steps, std::string title)
    : MenuBase{std::move(title), MenuFlag_None}
    , m_target_steps{steps}
    , m_steps{steps} {
    SetActions(
        std::make_pair(Button::A, Action{"Change Type"_i18n, [this](){
            if (m_index >= 0 && static_cast<size_t>(m_index) < m_steps.size()) {
                ChooseStepType(m_index, false);
            }
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            RequestBack();
        }}),
        std::make_pair(Button::SELECT, Action{"Configure"_i18n, [this](){
            if (m_index >= 0 && static_cast<size_t>(m_index) < m_steps.size()) {
                ConfigureStep(m_index);
            }
        }}),
        std::make_pair(Button::X, Action{"Add Step"_i18n, [this](){
            AddStep();
        }}),
        std::make_pair(Button::Y, Action{"Remove Step"_i18n, [this](){
            RemoveStep();
        }}),
        std::make_pair(Button::START, Action{"Save Processing Steps"_i18n, [this](){
            SaveSteps();
        }})
    );

    const Vec4 list_pos{75.f, GetY() + 72.f, 1220.f - 150.f, 64.f};
    m_list = std::make_unique<List>(1, 7, m_pos, list_pos);
    m_list->SetLayout(List::Layout::GRID);
}

void CustomComponentProcessingStepsEditor::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (m_steps.empty()) {
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, m_steps.size(), [this](bool touched, auto index) {
        if (touched && m_index == index) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect::Focus);
            SetIndex(index);
        }
    });
}

void CustomComponentProcessingStepsEditor::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    if (m_steps.empty()) {
        gfx::drawTextArgs(vg, SCREEN_WIDTH / 2.f, SCREEN_HEIGHT / 2.f, 21.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "No processing steps. Press %s to add one.", gfx::getButton(Button::X));
        return;
    }

    m_list->Draw(vg, theme, m_steps.size(), [this](auto* vg, auto* theme, const auto& rect, auto index) {
        DrawStep(vg, theme, rect, index);
    });
}

void CustomComponentProcessingStepsEditor::SetIndex(s64 index) {
    if (m_steps.empty()) {
        m_index = 0;
        return;
    }
    m_index = std::clamp<s64>(index, 0, static_cast<s64>(m_steps.size()) - 1);
    if (m_index == 0) {
        m_list->SetYoff(0);
    }
}

bool CustomComponentProcessingStepsEditor::HasUnsavedChanges() const {
    if (m_steps.size() != m_target_steps.size()) {
        return true;
    }

    for (size_t i = 0; i < m_steps.size(); ++i) {
        const auto& current = m_steps[i];
        const auto& saved = m_target_steps[i];
        if (current.type != saved.type ||
            current.target_path != saved.target_path ||
            current.subfolder_name != saved.subfolder_name ||
            current.source_file_pattern != saved.source_file_pattern ||
            current.target_filename != saved.target_filename) {
            return true;
        }
    }
    return false;
}

void CustomComponentProcessingStepsEditor::RequestBack() {
    if (!HasUnsavedChanges()) {
        SetPop();
        return;
    }

    App::Push<ui::OptionBox>("Exit without saving processing steps?",
        "Continue editing", "Exit without saving", 0, [this](auto selected) {
            if (selected && *selected == 1) {
                SetPop();
            }
        });
}

void CustomComponentProcessingStepsEditor::ChooseStepType(size_t index, bool add_step) {
    if (!add_step && index >= m_steps.size()) {
        return;
    }

    const ui::PopupList::Items names(std::begin(STEP_NAMES), std::end(STEP_NAMES));
    const auto current = add_step
        ? std::begin(STEP_TYPES)
        : std::find(std::begin(STEP_TYPES), std::end(STEP_TYPES), m_steps[index].type);
    const auto current_index = current == std::end(STEP_TYPES)
        ? 0
        : std::distance(std::begin(STEP_TYPES), current);

    App::Push<ui::PopupList>("Step Type", names, [this, index, add_step](auto selected) {
        if (!selected || *selected < 0 || static_cast<size_t>(*selected) >= std::size(STEP_TYPES)) {
            return;
        }

        auto step_index = index;
        if (add_step) {
            m_steps.emplace_back();
            step_index = m_steps.size() - 1;
            SetIndex(static_cast<s64>(step_index));
        } else if (step_index >= m_steps.size()) {
            return;
        }

        m_steps[step_index] = catalog::ProcessingStep{};
        m_steps[step_index].type = STEP_TYPES[*selected];
    }, current_index);
}

void CustomComponentProcessingStepsEditor::ConfigureStep(size_t index) {
    if (index >= m_steps.size()) {
        return;
    }

    std::vector<ProcessingStepValuesEditor::Field> fields;
    auto& step = m_steps[index];
    switch (m_steps[index].type) {
        case catalog::StepType::UnzipToRoot:
            App::Notify("This step has no configurable values");
            return;
        case catalog::StepType::UnzipToPath:
        case catalog::StepType::CopyFile:
        case catalog::StepType::CopyFileToAutoFolder:
            fields.emplace_back("Target Path", &step.target_path);
            break;
        case catalog::StepType::FindAndRename:
            fields.emplace_back("Source File Pattern", &step.source_file_pattern);
            fields.emplace_back("Target Path", &step.target_path);
            fields.emplace_back("Target Filename", &step.target_filename);
            break;
        case catalog::StepType::DeleteFile:
            fields.emplace_back("Path to Delete", &step.target_path);
            break;
        case catalog::StepType::FindAndCopy:
            fields.emplace_back("Source File Pattern", &step.source_file_pattern);
            fields.emplace_back("Target Path", &step.target_path);
            break;
        case catalog::StepType::UnzipSubfolderToPath:
            fields.emplace_back("Source Subfolder", &step.subfolder_name);
            fields.emplace_back("Target Path (optional)", &step.target_path);
            break;
    }

    App::Push<ProcessingStepValuesEditor>(std::move(fields), "Processing Step Values");
}

void CustomComponentProcessingStepsEditor::AddStep() {
    ChooseStepType(m_steps.size(), true);
}

void CustomComponentProcessingStepsEditor::RemoveStep() {
    if (m_index < 0 || static_cast<size_t>(m_index) >= m_steps.size()) {
        return;
    }
    m_steps.erase(m_steps.begin() + m_index);
    SetIndex(std::min<s64>(m_index, static_cast<s64>(m_steps.size()) - 1));
}

void CustomComponentProcessingStepsEditor::SaveSteps() {
    for (size_t index = 0; index < m_steps.size(); ++index) {
        const auto& step = m_steps[index];
        const char* missing_value = nullptr;
        switch (step.type) {
            case catalog::StepType::UnzipToRoot:
                break;
            case catalog::StepType::UnzipToPath:
            case catalog::StepType::CopyFile:
            case catalog::StepType::CopyFileToAutoFolder:
            case catalog::StepType::DeleteFile:
                if (step.target_path.empty()) {
                    missing_value = step.type == catalog::StepType::DeleteFile ? "Path to Delete" : "Target Path";
                }
                break;
            case catalog::StepType::FindAndRename:
                if (step.source_file_pattern.empty()) {
                    missing_value = "Source File Pattern";
                } else if (step.target_path.empty()) {
                    missing_value = "Target Path";
                } else if (step.target_filename.empty()) {
                    missing_value = "Target Filename";
                }
                break;
            case catalog::StepType::FindAndCopy:
                if (step.source_file_pattern.empty()) {
                    missing_value = "Source File Pattern";
                } else if (step.target_path.empty()) {
                    missing_value = "Target Path";
                }
                break;
            case catalog::StepType::UnzipSubfolderToPath:
                if (step.subfolder_name.empty()) {
                    missing_value = "Source Subfolder";
                }
                break;
        }

        if (missing_value) {
            App::Notify("Step " + std::to_string(index + 1) + ": " + missing_value + " is required");
            return;
        }
    }

    m_target_steps = m_steps;
    App::Notify("Processing steps saved");
    SetPop();
}

void CustomComponentProcessingStepsEditor::DrawStep(NVGcontext* vg, Theme* theme, const Vec4& rect, size_t index) const {
    const auto& [x, y, w, h] = rect;
    const bool selected = m_index == static_cast<s64>(index);
    if (selected) {
        gfx::drawRectOutline(vg, theme, 3.f, rect);
    } else if (index + 1 < m_steps.size()) {
        gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
    }

    const auto& step = m_steps[index];
    gfx::drawTextArgs(vg, x + 20.f, y + 15.f, 20.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
        theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
        "%zu. %s", index + 1, GetStepName(step.type));

    std::string parameters;
    const auto append_parameter = [&parameters](const char* label, const std::string& value) {
        if (!value.empty()) {
            if (!parameters.empty()) {
                parameters += "  |  ";
            }
            parameters += label;
            parameters += ": ";
            parameters += value;
        }
    };

    switch (step.type) {
        case catalog::StepType::UnzipToRoot:
            break;
        case catalog::StepType::UnzipToPath:
        case catalog::StepType::CopyFile:
        case catalog::StepType::CopyFileToAutoFolder:
            append_parameter("Target Path", step.target_path);
            break;
        case catalog::StepType::FindAndRename:
            append_parameter("Source File Pattern", step.source_file_pattern);
            append_parameter("Target Path", step.target_path);
            append_parameter("Target Filename", step.target_filename);
            break;
        case catalog::StepType::DeleteFile:
            append_parameter("Path to delete", step.target_path);
            break;
        case catalog::StepType::FindAndCopy:
            append_parameter("Source File Pattern", step.source_file_pattern);
            append_parameter("Target Path", step.target_path);
            break;
        case catalog::StepType::UnzipSubfolderToPath:
            append_parameter("Source Subfolder", step.subfolder_name);
            append_parameter("Target Path", step.target_path);
            break;
    }
    if (parameters.empty()) parameters = "No additional values";
    parameters += "  |  Press ";
    parameters += gfx::getButton(Button::A);
    parameters += " to change type, ";
    parameters += gfx::getButton(Button::SELECT);
    parameters += " to configure";

    gfx::drawTextBox(vg, x + 20.f, y + 40.f, 15.f, w - 40.f,
        theme->GetColour(ThemeEntryID_TEXT_INFO), parameters.c_str(),
        NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
}

void CustomComponentProcessingStepsEditor::EditText(std::string& value, const std::string& title) {
    std::string updated;
    if (R_SUCCEEDED(swkbd::ShowText(updated, title.c_str(), title.c_str(), value.c_str(), 0, 512))) {
        value = std::move(updated);
    }
}



} // namespace sphaira::ui::menu::hats
