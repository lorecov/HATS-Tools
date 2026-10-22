
#include "ui/menus/custom_component_editor.hpp"

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

constexpr const char* CATEGORIES[]{
    "Essential",
    "Homebrew Apps",
    "Patches",
    "Tesla Overlays",
    "Payloads",
};

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

bool ProcessingStepsEqual(
    const std::vector<catalog::ProcessingStep>& lhs,
    const std::vector<catalog::ProcessingStep>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        const auto& left = lhs[i];
        const auto& right = rhs[i];
        if (left.type != right.type ||
            left.target_path != right.target_path ||
            left.subfolder_name != right.subfolder_name ||
            left.source_file_pattern != right.source_file_pattern ||
            left.target_filename != right.target_filename) {
            return false;
        }
    }
    return true;
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
};

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
        gfx::drawTextArgs(vg, 95.f, GetY() + 105.f, 21.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT_INFO),
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
        "Exit without saving", "Continue editing", 1, [this](auto selected) {
            if (selected && *selected == 0) {
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

CustomComponentEditor::CustomComponentEditor(catalog::CustomComponent component, bool is_new, SaveCallback on_save)
    : MenuBase{is_new ? "Add Custom Component" : "Edit Custom Component", MenuFlag_None}
    , m_component{std::move(component)}
    , m_on_save{std::move(on_save)} {
    if (m_component.source_type.empty()) {
        m_component.source_type = "github_release";
    }

    for (const auto& pattern : m_component.asset_patterns) {
        m_assets.push_back({pattern, {}});
    }
    if (!m_component.processing_steps.empty()) {
        if (m_component.source_type == "direct_url") {
            m_direct_url_steps = std::move(m_component.processing_steps);
        } else if (!m_assets.empty()) {
            m_assets.front().processing_steps = std::move(m_component.processing_steps);
        }
    }

    this->SetActions(
        std::make_pair(Button::A, Action{"Edit"_i18n, [this](){
            OnSelect();
        }}),
        std::make_pair(Button::B, Action{"Back"_i18n, [this](){
            RequestBack();
        }}),
        std::make_pair(Button::X, Action{"Add Asset"_i18n, [this](){
            OnAdd();
        }}),
        std::make_pair(Button::Y, Action{"Remove"_i18n, [this](){
            OnRemove();
        }}),
        std::make_pair(Button::SELECT, Action{"Processing Steps"_i18n, [this](){
            OpenSelectedSteps();
        }}),
        std::make_pair(Button::L, Action{"Details"_i18n, [this](){
            SetTab(Tab::Details);
        }}),
        std::make_pair(Button::R, Action{"Source"_i18n, [this](){
            SetTab(Tab::Source);
        }}),
        std::make_pair(Button::START, Action{"Save"_i18n, [this](){
            OnSave();
        }})
    );

    const Vec4 list_pos{75.f, GetY() + 1.f + 102.f, 1220.f - 150.f, 58.f};
    m_list = std::make_unique<List>(1, 7, m_pos, list_pos);
    m_list->SetLayout(List::Layout::GRID);
    BuildRows();
    UpdateSourceActions();
    m_initial_draft = BuildDraft();
}

void CustomComponentEditor::Update(Controller* controller, TouchInfo* touch) {
    MenuBase::Update(controller, touch);
    if (touch && touch->is_tap) {
        const float y = GetY() + 38.f;
        const float tab_w = (1220.f - 150.f - 20.f) / 2.f;
        if (touch->in_range(75, static_cast<s32>(y), static_cast<s32>(tab_w), 42)) {
            SetTab(Tab::Details);
            return;
        }
        if (touch->in_range(static_cast<s32>(75.f + tab_w + 20.f), static_cast<s32>(y), static_cast<s32>(tab_w), 42)) {
            SetTab(Tab::Source);
            return;
        }
    }

    if (m_rows.empty()) {
        return;
    }

    m_list->OnUpdate(controller, touch, m_index, m_rows.size(), [this](bool touched, auto index) {
        if (touched && m_index == index) {
            FireAction(Button::A);
        } else {
            App::PlaySoundEffect(SoundEffect::Focus);
            SetIndex(index);
        }
    });
}

void CustomComponentEditor::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);
    DrawTabs(vg, theme);

    const auto count = static_cast<s64>(m_rows.size());
    m_list->Draw(vg, theme, count, [this](auto* vg, auto* theme, const auto& rect, auto index) {
        const auto& [x, y, w, h] = rect;
        const auto& row = m_rows[index];
        const bool selected = m_index == index;
        const auto label_colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT);
        const auto value_colour = theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO);

        if (selected) {
            gfx::drawRectOutline(vg, theme, 3.f, rect);
        } else if (index + 1 < m_rows.size()) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        std::string label;
        switch (row.type) {
            case RowType::ComponentId: label = "Component ID"; break;
            case RowType::Name: label = "Name"; break;
            case RowType::Category: label = "Category"; break;
            case RowType::Description: label = "Description"; break;
            case RowType::SourceType: label = "Source Type"; break;
            case RowType::SourceLocation: label = m_component.source_type == "direct_url" ? "Direct URL" : "GitHub Repo"; break;
            case RowType::AssetPattern: label = "Asset Pattern " + std::to_string(row.index + 1); break;
        }

        const auto value = GetRowValue(row);
        gfx::drawText(vg, x + 20.f, y + 18.f, 20.f, label.c_str(),
            nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_colour);

        if (row.type == RowType::AssetPattern ||
            (row.type == RowType::SourceLocation && m_component.source_type == "direct_url")) {
            std::string title;
            std::string prompt;
            if (row.type == RowType::AssetPattern && row.index < m_assets.size()) {
                title = m_assets[row.index].pattern;
                if (title.empty()) {
                    prompt = "Press " + std::string{gfx::getButton(Button::A)} + " to enter asset pattern";
                } else {
                    prompt = std::to_string(m_assets[row.index].processing_steps.size()) +
                        " steps (press " + gfx::getButton(Button::SELECT) + " to configure)";
                }
            } else if (row.type == RowType::SourceLocation) {
                title = m_component.repo;
                if (title.empty()) {
                    prompt = "Press " + std::string{gfx::getButton(Button::A)} + " to enter Direct URL";
                } else {
                    prompt = std::to_string(m_direct_url_steps.size()) +
                        " steps (press " + gfx::getButton(Button::SELECT) + " to configure)";
                }
            }
            if (!title.empty()) {
                gfx::drawTextBox(vg, x + 20.f, y + 8.f, 18.f, w - 40.f,
                    value_colour, title.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
                gfx::drawTextBox(vg, x + 20.f, y + 34.f, 15.f, w - 40.f,
                    value_colour, prompt.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            } else {
                gfx::drawTextBox(vg, x + 20.f, y + 18.f, 15.f, w - 40.f,
                    value_colour, prompt.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
            }
        } else if (!value.empty()) {
            gfx::drawTextBox(vg, x + w * 0.42f, y + 18.f, 18.f, w * 0.56f,
                value_colour, value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        } else {
            const auto prompt = row.type == RowType::Category
                ? "Press " + std::string{gfx::getButton(Button::A)} + " to select category"
                : "Press " + std::string{gfx::getButton(Button::A)} + " to enter value";
            gfx::drawTextBox(vg, x + w * 0.42f, y + 18.f, 15.f, w * 0.56f,
                value_colour, prompt.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        }
    });

    if (m_tab == Tab::Source && m_component.source_type == "github_release" && m_assets.empty()) {
        const auto message = "No asset patterns. Press " + std::string{gfx::getButton(Button::X)} + " to add one.";
        const float empty_area_top = GetY() + 103.f + 2.f * 58.f;
        const float empty_area_bottom = m_button_pos.y - 35.f;
        const float empty_area_center_y = (empty_area_top + empty_area_bottom) / 2.f;
        const float list_center_x = 75.f + (1220.f - 150.f) / 2.f;
        gfx::drawTextArgs(vg, list_center_x, empty_area_center_y, 21.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT_INFO),
            "%s", message.c_str());
    }
}

void CustomComponentEditor::DrawTabs(NVGcontext* vg, Theme* theme) const {
    const float y = GetY() + 38.f;
    const float height = 42.f;
    const float start_x = 75.f;
    const float tab_w = (1220.f - 150.f - 20.f) / 2.f;

    const auto draw_tab = [&](float x, bool active, const char* title, Button button) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, tab_w, height, 6.f);
        nvgFillColor(vg, theme->GetColour(ThemeEntryID_BACKGROUND));
        nvgFill(vg);
        if (active) {
            nvgFillColor(vg, nvgRGBA(255, 255, 255, 25));
            nvgFill(vg);
        }

        nvgStrokeWidth(vg, active ? 2.f : 1.f);
        nvgStrokeColor(vg, theme->GetColour(active ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_LINE_SEPARATOR));
        nvgStroke(vg);

        gfx::drawTextArgs(vg, x + tab_w / 2.f, y + height / 2.f, 20.f,
            NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE,
            theme->GetColour(active ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT),
            "%s", title);
        gfx::drawTextArgs(vg, x + 20.f, y + height / 2.f, 22.f,
            NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", gfx::getButton(button));
    };

    draw_tab(start_x, m_tab == Tab::Details, "Component Details", Button::L);
    draw_tab(start_x + tab_w + 20.f, m_tab == Tab::Source, "Source Information", Button::R);
}

void CustomComponentEditor::BuildRows() {
    m_rows.clear();
    if (m_tab == Tab::Details) {
        m_rows = {
            {RowType::ComponentId},
            {RowType::Name},
            {RowType::Category},
            {RowType::Description},
        };
    } else {
        m_rows.push_back({RowType::SourceType});
        m_rows.push_back({RowType::SourceLocation});
        if (m_component.source_type == "github_release") {
            for (size_t i = 0; i < m_assets.size(); ++i) {
                m_rows.push_back({RowType::AssetPattern, i});
            }
        }
    }

    if (m_rows.empty()) {
        m_index = 0;
    } else {
        m_index = std::clamp<s64>(m_index, 0, static_cast<s64>(m_rows.size()) - 1);
    }
    m_list->SetYoff(0);
}

void CustomComponentEditor::SetIndex(s64 index) {
    if (m_rows.empty()) {
        m_index = 0;
        return;
    }

    index = std::clamp<s64>(index, 0, static_cast<s64>(m_rows.size()) - 1);
    if (m_tab == Tab::Source && m_component.source_type == "github_release" &&
        m_rows[index].type == RowType::AssetPattern && m_selected_asset != m_rows[index].index) {
        m_selected_asset = m_rows[index].index;
        m_index = index;
        BuildRows();
        UpdateSourceActions();
        return;
    }

    m_index = index;
    UpdateSourceActions();
    if (m_index == 0) {
        m_list->SetYoff(0);
    }
}

void CustomComponentEditor::SetTab(Tab tab) {
    if (m_tab == tab) {
        return;
    }
    m_tab = tab;
    m_index = 0;
    BuildRows();
    UpdateSourceActions();
}

void CustomComponentEditor::OnSelect() {
    if (m_rows.empty() || m_index < 0 || static_cast<size_t>(m_index) >= m_rows.size()) {
        return;
    }

    const auto row = m_rows[m_index];
    switch (row.type) {
        case RowType::ComponentId:
            EditText(m_component.id, "Component ID");
            break;
        case RowType::Name:
            EditText(m_component.name, "Component Name");
            break;
        case RowType::Category: {
            ui::PopupList::Items items(std::begin(CATEGORIES), std::end(CATEGORIES));
            items.insert(items.begin(), "None");
            const auto category = std::find(std::begin(CATEGORIES), std::end(CATEGORIES), m_component.category);
            const auto category_index = category == std::end(CATEGORIES)
                ? 0
                : static_cast<s64>(std::distance(std::begin(CATEGORIES), category)) + 1;
            App::Push<ui::PopupList>("Category", items, [this](auto index) {
                if (index && *index >= 0 && static_cast<size_t>(*index) <= std::size(CATEGORIES)) {
                    m_component.category = *index == 0 ? "" : CATEGORIES[*index - 1];
                }
            }, category_index);
            break;
        }
        case RowType::Description:
            EditText(m_component.description, "Component Description");
            break;
        case RowType::SourceType: {
            const ui::PopupList::Items items{"GitHub Release", "Direct URL"};
            App::Push<ui::PopupList>("Source Type", items, [this](auto index) {
                if (index) {
                    m_component.source_type = *index == 0 ? "github_release" : "direct_url";
                    BuildRows();
                    UpdateSourceActions();
                }
            }, m_component.source_type == "direct_url" ? 1 : 0);
            break;
        }
        case RowType::SourceLocation:
            EditText(m_component.repo, m_component.source_type == "direct_url" ? "Direct URL" : "GitHub Repo");
            break;
        case RowType::AssetPattern:
            EditText(m_assets[row.index].pattern, "Asset Pattern");
            break;
    }
}

void CustomComponentEditor::OnAdd() {
    if (m_tab != Tab::Source || m_component.source_type != "github_release") {
        return;
    }

    m_assets.push_back({});
    m_selected_asset = m_assets.size() - 1;
    BuildRows();
    const auto pattern_row = std::find_if(m_rows.begin(), m_rows.end(), [this](const auto& row) {
        return row.type == RowType::AssetPattern && row.index == m_selected_asset;
    });
    if (pattern_row != m_rows.end()) {
        m_index = std::distance(m_rows.begin(), pattern_row);
        UpdateSourceActions();
        EditText(m_assets[m_selected_asset].pattern, "Asset Pattern");
    }
}

void CustomComponentEditor::OnRemove() {
    if (m_tab != Tab::Source || m_rows.empty() || m_index < 0 ||
        static_cast<size_t>(m_index) >= m_rows.size()) {
        return;
    }

    const auto row = m_rows[m_index];
    if (row.type == RowType::AssetPattern) {
        m_assets.erase(m_assets.begin() + row.index);
        if (m_assets.empty()) {
            m_selected_asset = 0;
        } else {
            m_selected_asset = std::min(m_selected_asset, m_assets.size() - 1);
        }
        BuildRows();
        UpdateSourceActions();
    }
}

void CustomComponentEditor::OpenSelectedSteps() {
    if (m_tab != Tab::Source || m_rows.empty() || m_index < 0 ||
        static_cast<size_t>(m_index) >= m_rows.size()) {
        return;
    }

    const auto& row = m_rows[m_index];
    if (m_component.source_type == "direct_url" && row.type == RowType::SourceLocation) {
        App::Push<CustomComponentProcessingStepsEditor>(m_direct_url_steps, "Direct URL Processing Steps");
    } else if (m_component.source_type == "github_release" &&
               row.type == RowType::AssetPattern && row.index < m_assets.size()) {
        m_selected_asset = row.index;
        App::Push<CustomComponentProcessingStepsEditor>(
            m_assets[row.index].processing_steps,
            "Asset Processing Steps " + std::to_string(row.index + 1));
    }
}

void CustomComponentEditor::UpdateSourceActions() {
    if (m_tab != Tab::Source || m_rows.empty() || m_index < 0 ||
        static_cast<size_t>(m_index) >= m_rows.size()) {
        RemoveAction(Button::SELECT);
        RemoveAction(Button::X);
        RemoveAction(Button::Y);
        return;
    }

    const auto& row = m_rows[m_index];
    if ((m_component.source_type == "direct_url" && row.type == RowType::SourceLocation) ||
        (m_component.source_type == "github_release" && row.type == RowType::AssetPattern)) {
        SetAction(Button::SELECT, Action{"Processing Steps"_i18n, [this](){
            OpenSelectedSteps();
        }});
    } else {
        RemoveAction(Button::SELECT);
    }

    if (m_component.source_type == "github_release") {
        SetAction(Button::X, Action{"Add Asset"_i18n, [this](){
            OnAdd();
        }});
    } else {
        RemoveAction(Button::X);
    }

    if (row.type == RowType::AssetPattern) {
        SetAction(Button::Y, Action{"Remove Asset"_i18n, [this](){
            OnRemove();
        }});
    } else {
        RemoveAction(Button::Y);
    }
}

void CustomComponentEditor::OnSave() {
    if (m_component.id.empty()) {
        App::Notify("Component ID is required");
        return;
    }
    if (m_component.name.empty()) {
        App::Notify("Name is required");
        return;
    }
    if (m_component.category.empty()) {
        App::Notify("Category is required");
        return;
    }

    if (m_component.source_type == "github_release") {
        if (m_component.repo.empty()) {
            App::Notify("GitHub Repo is required");
            return;
        }
        if (m_assets.empty()) {
            App::Notify("At least one asset is required");
            return;
        }
        for (size_t i = 0; i < m_assets.size(); ++i) {
            if (m_assets[i].processing_steps.empty()) {
                App::Notify("Asset " + std::to_string(i + 1) + " needs at least one saved processing step");
                return;
            }
        }
    } else if (m_component.source_type == "direct_url") {
        if (m_component.repo.empty()) {
            App::Notify("Direct URL is required");
            return;
        }
        if (m_direct_url_steps.empty()) {
            App::Notify("Direct URL needs at least one saved processing step");
            return;
        }
    }

    if (!m_on_save) {
        App::Notify("Save action is not connected");
        return;
    }

    const auto draft = BuildDraft();
    m_on_save(draft);
    SetPop();
}

void CustomComponentEditor::RequestBack() {
    if (!HasUnsavedChanges()) {
        SetPop();
        return;
    }

    App::Push<ui::OptionBox>("Exit without saving component changes?",
        "Exit without saving", "Continue editing", 1, [this](auto selected) {
            if (selected && *selected == 0) {
                SetPop();
            }
        });
}

bool CustomComponentEditor::HasUnsavedChanges() const {
    const auto current = BuildDraft();
    const auto& lhs = current.component;
    const auto& rhs = m_initial_draft.component;
    if (lhs.id != rhs.id ||
        lhs.name != rhs.name ||
        lhs.description != rhs.description ||
        lhs.category != rhs.category ||
        lhs.source_type != rhs.source_type ||
        lhs.repo != rhs.repo ||
        lhs.version != rhs.version ||
        lhs.latest_version != rhs.latest_version ||
        lhs.fixed_version != rhs.fixed_version ||
        lhs.fixed_version_enabled != rhs.fixed_version_enabled ||
        lhs.update_available != rhs.update_available ||
        lhs.asset_patterns != rhs.asset_patterns ||
        current.direct_url_steps.size() != m_initial_draft.direct_url_steps.size() ||
        !ProcessingStepsEqual(current.direct_url_steps, m_initial_draft.direct_url_steps) ||
        current.assets.size() != m_initial_draft.assets.size()) {
        return true;
    }

    for (size_t i = 0; i < current.assets.size(); ++i) {
        if (current.assets[i].pattern != m_initial_draft.assets[i].pattern ||
            !ProcessingStepsEqual(current.assets[i].processing_steps, m_initial_draft.assets[i].processing_steps)) {
            return true;
        }
    }
    return false;
}

CustomComponentEditorDraft CustomComponentEditor::BuildDraft() const {
    CustomComponentEditorDraft draft;
    draft.component = m_component;
    draft.component.asset_patterns.clear();
    for (const auto& asset : m_assets) {
        draft.component.asset_patterns.push_back(asset.pattern);
    }
    if (m_component.source_type == "direct_url") {
        draft.component.processing_steps = m_direct_url_steps;
    } else if (m_selected_asset < m_assets.size()) {
        draft.component.processing_steps = m_assets[m_selected_asset].processing_steps;
    } else {
        draft.component.processing_steps.clear();
    }
    draft.assets = m_assets;
    draft.direct_url_steps = m_direct_url_steps;
    return draft;
}

void CustomComponentEditor::EditText(std::string& value, const std::string& title) {
    std::string updated;
    if (R_SUCCEEDED(swkbd::ShowText(updated, title.c_str(), title.c_str(), value.c_str(), 0, 512))) {
        value = std::move(updated);
        BuildRows();
    }
}

std::string CustomComponentEditor::GetRowValue(const Row& row) const {
    switch (row.type) {
        case RowType::ComponentId: return m_component.id;
        case RowType::Name: return m_component.name;
        case RowType::Category: return m_component.category;
        case RowType::Description: return m_component.description;
        case RowType::SourceType: return m_component.source_type == "direct_url" ? "Direct URL" : "GitHub Release";
        case RowType::SourceLocation: return m_component.repo;
        case RowType::AssetPattern:
            if (row.index < m_assets.size()) {
                return m_assets[row.index].pattern;
            }
            return {};
    }
    return {};
}

} // namespace sphaira::ui::menu::hats
