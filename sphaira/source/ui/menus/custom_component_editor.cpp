
#include "ui/menus/custom_component_editor.hpp"

#include "ui/nvg_util.hpp"
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
    "Unzip Subfolder to Path",
    "Copy File",
    "Copy File to Auto Folder",
    "Find and Rename",
    "Delete File",
};

constexpr catalog::StepType STEP_TYPES[]{
    catalog::StepType::UnzipToRoot,
    catalog::StepType::UnzipToPath,
    catalog::StepType::UnzipSubfolderToPath,
    catalog::StepType::CopyFile,
    catalog::StepType::CopyFileToAutoFolder,
    catalog::StepType::FindAndRename,
    catalog::StepType::DeleteFile,
};

const char* GetStepName(catalog::StepType type) {
    for (size_t i = 0; i < std::size(STEP_TYPES); ++i) {
        if (STEP_TYPES[i] == type) {
            return STEP_NAMES[i];
        }
    }
    return "Unknown";
}

} // namespace

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
            SetPop();
        }}),
        std::make_pair(Button::X, Action{"Add"_i18n, [this](){
            OnAdd();
        }}),
        std::make_pair(Button::Y, Action{"Remove"_i18n, [this](){
            OnRemove();
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
            case RowType::AddAssetPattern: label = "Add Asset Pattern"; break;
            case RowType::AssetStepsInfo: label = "Add an asset pattern to configure its processing steps"; break;
            case RowType::DirectStepsHeader: label = "Processing Steps"; break;
            case RowType::ProcessingStep: label = "Processing Step " + std::to_string(row.index + 1); break;
            case RowType::AddProcessingStep: label = "Add Processing Step"; break;
        }

        const auto value = GetRowValue(row);
        gfx::drawText(vg, x + 20.f, y + 18.f, 20.f, label.c_str(),
            nullptr, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, label_colour);
        if (!value.empty()) {
            gfx::drawTextBox(vg, x + w * 0.42f, y + 18.f, 18.f, w * 0.56f,
                theme->GetColour(selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO),
                value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
        }
    });
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
            m_rows.push_back({RowType::AddAssetPattern});
            if (m_assets.empty()) {
                m_rows.push_back({RowType::AssetStepsInfo});
            } else {
                for (size_t i = 0; i < m_assets[m_selected_asset].processing_steps.size(); ++i) {
                    m_rows.push_back({RowType::ProcessingStep, i});
                }
                m_rows.push_back({RowType::AddProcessingStep});
            }
        } else {
            m_rows.push_back({RowType::DirectStepsHeader});
            for (size_t i = 0; i < m_direct_url_steps.size(); ++i) {
                m_rows.push_back({RowType::ProcessingStep, i});
            }
            m_rows.push_back({RowType::AddProcessingStep});
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
        return;
    }

    m_index = index;
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
            App::Push<ui::PopupList>("Category", items, [this](auto index) {
                if (index && *index >= 0 && static_cast<size_t>(*index) < std::size(CATEGORIES)) {
                    m_component.category = CATEGORIES[*index];
                }
            }, m_component.category);
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
        case RowType::AddAssetPattern:
            OnAdd();
            break;
        case RowType::ProcessingStep: {
            auto* steps = GetSelectedSteps();
            if (steps) {
                EditStep(row.index, *steps);
            }
            break;
        }
        case RowType::AddProcessingStep:
            OnAdd();
            break;
        case RowType::AssetStepsInfo:
        case RowType::DirectStepsHeader:
            break;
    }
}

void CustomComponentEditor::OnAdd() {
    if (m_tab != Tab::Source) {
        return;
    }
    if (m_component.source_type == "github_release") {
        if (!m_assets.empty() && m_rows[m_index].type == RowType::AddProcessingStep) {
            auto& steps = m_assets[m_selected_asset].processing_steps;
            steps.emplace_back();
            EditStep(steps.size() - 1, steps);
            BuildRows();
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
            EditText(m_assets[m_selected_asset].pattern, "Asset Pattern");
        }
        BuildRows();
        return;
    }

    m_direct_url_steps.emplace_back();
    EditStep(m_direct_url_steps.size() - 1, m_direct_url_steps);
    BuildRows();
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
    } else if (row.type == RowType::ProcessingStep) {
        if (auto* steps = GetSelectedSteps(); steps && row.index < steps->size()) {
            steps->erase(steps->begin() + row.index);
            BuildRows();
        }
    }
}

void CustomComponentEditor::OnSave() {
    if (!m_on_save) {
        App::Notify("Save action is not connected");
        return;
    }

    CustomComponentEditorDraft draft;
    draft.component = m_component;
    draft.component.asset_patterns.clear();
    for (const auto& asset : m_assets) {
        draft.component.asset_patterns.push_back(asset.pattern);
    }
    draft.component.processing_steps = m_component.source_type == "direct_url"
        ? m_direct_url_steps
        : (m_assets.empty() ? std::vector<catalog::ProcessingStep>{} : m_assets[m_selected_asset].processing_steps);
    draft.assets = m_assets;
    draft.direct_url_steps = m_direct_url_steps;
    m_on_save(draft);
    SetPop();
}

void CustomComponentEditor::EditText(std::string& value, const std::string& title) {
    std::string updated;
    if (R_SUCCEEDED(swkbd::ShowText(updated, title.c_str(), title.c_str(), value.c_str(), 0, 512))) {
        value = std::move(updated);
        BuildRows();
    }
}

void CustomComponentEditor::EditStep(size_t index, std::vector<catalog::ProcessingStep>& steps) {
    if (index >= steps.size()) {
        return;
    }

    ui::PopupList::Items items{
        "Change Step Type",
        "Target Path",
        "Subfolder Name",
        "Source File Pattern",
        "Target Filename",
    };
    App::Push<ui::PopupList>("Processing Step", items, [this, index, &steps](auto selected) {
        if (!selected || *selected < 0) {
            return;
        }

        auto& step = steps[index];
        switch (*selected) {
            case 0: {
                ui::PopupList::Items names(std::begin(STEP_NAMES), std::end(STEP_NAMES));
                const auto current = std::find(std::begin(STEP_TYPES), std::end(STEP_TYPES), step.type);
                const auto current_index = current == std::end(STEP_TYPES) ? 0 : std::distance(std::begin(STEP_TYPES), current);
                App::Push<ui::PopupList>("Step Type", names, [this, index, &steps](auto type_index) {
                    if (type_index && *type_index >= 0 && static_cast<size_t>(*type_index) < std::size(STEP_TYPES) &&
                        index < steps.size()) {
                        steps[index].type = STEP_TYPES[*type_index];
                        BuildRows();
                    }
                }, current_index);
                break;
            }
            case 1: EditText(step.target_path, "Target Path"); break;
            case 2: EditText(step.subfolder_name, "Subfolder Name"); break;
            case 3: EditText(step.source_file_pattern, "Source File Pattern"); break;
            case 4: EditText(step.target_filename, "Target Filename"); break;
            default: break;
        }
        BuildRows();
    });
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
            return row.index < m_assets.size() ? m_assets[row.index].pattern : std::string{};
        case RowType::ProcessingStep: {
            const auto* steps = GetSelectedSteps();
            return steps && row.index < steps->size() ? GetStepName((*steps)[row.index].type) : std::string{};
        }
        case RowType::AddAssetPattern:
        case RowType::AssetStepsInfo:
        case RowType::DirectStepsHeader:
        case RowType::AddProcessingStep:
            return {};
    }
    return {};
}

std::vector<catalog::ProcessingStep>* CustomComponentEditor::GetSelectedSteps() {
    if (m_component.source_type == "direct_url") {
        return &m_direct_url_steps;
    }
    if (m_assets.empty() || m_selected_asset >= m_assets.size()) {
        return nullptr;
    }
    return &m_assets[m_selected_asset].processing_steps;
}

const std::vector<catalog::ProcessingStep>* CustomComponentEditor::GetSelectedSteps() const {
    if (m_component.source_type == "direct_url") {
        return &m_direct_url_steps;
    }
    if (m_assets.empty() || m_selected_asset >= m_assets.size()) {
        return nullptr;
    }
    return &m_assets[m_selected_asset].processing_steps;
}

} // namespace sphaira::ui::menu::hats
