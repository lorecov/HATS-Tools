
#include "ui/menus/custom_component_editor.hpp"
#include "ui/menus/custom_component_editor_utils.hpp"
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

constexpr const char* CATEGORIES[]{
    "Essential",
    "Homebrew Apps",
    "Patches",
    "Tesla Overlays",
    "Payloads",
};

} // namespace

CustomComponentEditor::CustomComponentEditor(
    catalog::CustomComponent component,
    bool is_new,
    SaveCallback on_save,
    std::vector<CustomComponentEditorDraft::Asset> assets)
    : MenuBase{is_new ? "Add Custom Component" : "Edit Custom Component", MenuFlag_None}
    , m_component{std::move(component)}
    , m_assets{std::move(assets)}
    , m_on_save{std::move(on_save)} {
    if (m_component.source_type.empty()) {
        m_component.source_type = "github_release";
    }

    if (m_assets.empty()) {
        for (const auto& pattern : m_component.asset_patterns) {
            m_assets.push_back({pattern, {}});
        }
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
        std::make_pair(Button::L2, Action{"", [this](){
            SetTab(Tab::Details);
        }}),
        std::make_pair(Button::R2, Action{"", [this](){
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
        const float empty_area_center_y = (empty_area_top + empty_area_bottom) / 2.f - 8.f;
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
        const bool left_tab = button == Button::L2;
        gfx::drawTextArgs(vg, left_tab ? x + 20.f : x + tab_w - 20.f, y + height / 2.f, 22.f,
            left_tab ? NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE : NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE,
            theme->GetColour(ThemeEntryID_TEXT_INFO), "%s", gfx::getButton(button));
    };

    draw_tab(start_x, m_tab == Tab::Details, "Component Details", Button::L2);
    draw_tab(start_x + tab_w + 20.f, m_tab == Tab::Source, "Source Information", Button::R2);
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
    if (m_on_save(draft)) {
        SetPop();
    }
}

void CustomComponentEditor::RequestBack() {
    if (!HasUnsavedChanges()) {
        SetPop();
        return;
    }

    App::Push<ui::OptionBox>("Exit without saving component changes?",
        "Continue editing", "Exit without saving", 0, [this](auto selected) {
            if (selected && *selected == 1) {
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
        lhs.asset_patterns != rhs.asset_patterns ||
        current.direct_url_steps.size() != m_initial_draft.direct_url_steps.size() ||
        !detail::ProcessingStepsEqual(current.direct_url_steps, m_initial_draft.direct_url_steps) ||
        current.assets.size() != m_initial_draft.assets.size()) {
        return true;
    }

    for (size_t i = 0; i < current.assets.size(); ++i) {
        if (current.assets[i].pattern != m_initial_draft.assets[i].pattern ||
            !detail::ProcessingStepsEqual(current.assets[i].processing_steps, m_initial_draft.assets[i].processing_steps)) {
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
