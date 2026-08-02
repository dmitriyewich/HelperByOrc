#include "text_pattern_constructor_ui.h"

#include "text_pattern_engine.h"
#include "text_pattern_ui_support.h"
#include "ui_settings.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <cstdio>
#include <string_view>
#include <utility>

namespace text_pattern_constructor_ui {
namespace {

int TrackSelection(ImGuiInputTextCallbackData* data) {
    auto* state = static_cast<State*>(data->UserData);
    if (state && data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        state->selectionStartByte = data->SelectionStart;
        state->selectionEndByte = data->SelectionEnd;
    }
    return 0;
}

const char* ConfidenceLabel(text_pattern_builder::Confidence confidence) {
    UiSettings& ui = UiSettings::Instance();
    switch (confidence) {
    case text_pattern_builder::Confidence::Recommended:
        return ui.Text(UiText::TextPatternSelectionRecommended);
    case text_pattern_builder::Confidence::Exact:
        return ui.Text(UiText::TextPatternSelectionExact);
    case text_pattern_builder::Confidence::Broad:
    default:
        return ui.Text(UiText::TextPatternSelectionBroad);
    }
}

const char* TokenText(text_pattern_builder::TokenKind kind, bool help) {
    UiSettings& ui = UiSettings::Instance();
    const UiText text = help
        ? text_pattern_ui::TokenHelp(kind)
        : text_pattern_ui::TokenLabel(kind);
    return text == UiText::Count ? (help ? "" : "?") : ui.Text(text);
}

const char* PatternBuilderErrorText(std::string_view error) {
    UiSettings& ui = UiSettings::Instance();
    if (error == "empty_selection") {
        return ui.Text(UiText::TextPatternSelectionRequired);
    }
    if (error == "invalid_selection") {
        return ui.Text(UiText::TextPatternSelectionInvalid);
    }
    if (error == "invalid_utf8") {
        return ui.Text(UiText::UnwantedHelperInvalidUtf8);
    }
    return ui.Text(UiText::TextPatternBuildFailed);
}

void Rebuild(State& state) {
    std::vector<text_pattern_builder::Replacement> replacements;
    replacements.reserve(state.selectedParts.size());
    for (const SelectedPart& part : state.selectedParts) {
        replacements.push_back(part.replacement);
    }
    state.pattern = text_pattern_builder::BuildWithReplacements(
        state.preparedSample,
        replacements,
        true,
        state.error);
}

const text_pattern_builder::Suggestion* SelectedSuggestion(State& state) {
    if (state.selectionResult.suggestions.empty()) {
        state.selectedSuggestionIndex = -1;
        return nullptr;
    }
    state.selectedSuggestionIndex = std::clamp(
        state.selectedSuggestionIndex,
        0,
        static_cast<int>(state.selectionResult.suggestions.size()) - 1);
    return &state.selectionResult.suggestions[static_cast<std::size_t>(state.selectedSuggestionIndex)];
}

void SelectRecommendedSuggestion(State& state) {
    state.selectedSuggestionIndex = -1;
    for (std::size_t index = 0; index < state.selectionResult.suggestions.size(); ++index) {
        if (state.selectionResult.suggestions[index].confidence
            == text_pattern_builder::Confidence::Recommended) {
            state.selectedSuggestionIndex = static_cast<int>(index);
            return;
        }
    }
    if (!state.selectionResult.suggestions.empty()) {
        state.selectedSuggestionIndex = 0;
    }
}

void FilterSuggestionsToSelection(text_pattern_builder::SelectionResult& result) {
    result.suggestions.erase(
        std::remove_if(
            result.suggestions.begin(),
            result.suggestions.end(),
            [&](const text_pattern_builder::Suggestion& suggestion) {
                const std::string pattern = "\\A(?:" + suggestion.pattern + ")\\z";
                text_pattern::CompileResult compiled = text_pattern::Compile(pattern, false);
                return !compiled.program
                    || compiled.program->Match(result.source).status != text_pattern::MatchStatus::Match;
            }),
        result.suggestions.end());
    if (result.suggestions.empty()) {
        result.error = "invalid_replacement";
    }
}

bool DrawSuggestionPicker(State& state) {
    UiSettings& ui = UiSettings::Instance();
    const text_pattern_builder::Suggestion* selected = SelectedSuggestion(state);
    if (!selected) {
        return false;
    }

    ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternSuggestion));
    char preview[256]{};
    std::snprintf(
        preview,
        sizeof(preview),
        "%s — %s",
        ConfidenceLabel(selected->confidence),
        TokenText(selected->kind, false));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##selection_suggestion", preview)) {
        for (std::size_t index = 0; index < state.selectionResult.suggestions.size(); ++index) {
            const text_pattern_builder::Suggestion& suggestion = state.selectionResult.suggestions[index];
            char label[256]{};
            std::snprintf(
                label,
                sizeof(label),
                "%s — %s",
                ConfidenceLabel(suggestion.confidence),
                TokenText(suggestion.kind, false));
            ImGui::PushID(static_cast<int>(index));
            const bool isSelected = state.selectedSuggestionIndex == static_cast<int>(index);
            if (ImGui::Selectable(label, isSelected)) {
                state.selectedSuggestionIndex = static_cast<int>(index);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s\n%s", TokenText(suggestion.kind, true), suggestion.pattern.c_str());
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return true;
}

void RemoveReplacement(State& state, std::size_t index) {
    if (index >= state.selectedParts.size()) {
        return;
    }
    state.selectedParts.erase(state.selectedParts.begin() + static_cast<std::ptrdiff_t>(index));
    Rebuild(state);
}

void DrawReplacementList(State& state) {
    UiSettings& ui = UiSettings::Instance();
    if (state.selectedParts.empty()) {
        return;
    }

    ImGui::SeparatorText(ui.Text(UiText::TextPatternSelectedParts));
    std::size_t removeIndex = state.selectedParts.size();
    const bool compactSelectedParts = ImGui::GetContentRegionAvail().x < ui.Scale(520.0f);
    if (compactSelectedParts) {
        for (std::size_t index = 0; index < state.selectedParts.size(); ++index) {
            const SelectedPart& part = state.selectedParts[index];
            const text_pattern_builder::Replacement& replacement = part.replacement;
            const bool sourceValid = replacement.offset <= state.preparedSample.size()
                && replacement.length <= state.preparedSample.size() - replacement.offset;
            const std::string_view source = sourceValid
                ? std::string_view(state.preparedSample).substr(replacement.offset, replacement.length)
                : std::string_view{};
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::BeginChild(
                    "##selected_part_card",
                    ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
                ImGui::TextWrapped(
                    "%.*s",
                    static_cast<int>(source.size()),
                    source.empty() ? "" : source.data());
                if (!part.captureName.empty()) {
                    ImGui::TextDisabled("[chatwordsex(%s)]", part.captureName.c_str());
                }
                ImGui::TextDisabled("%s", replacement.pattern.c_str());
                if (ImGui::Button(ui.Text(UiText::TextPatternRemovePart))) {
                    removeIndex = index;
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
    } else {
        const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
            | ImGuiTableFlags_RowBg
            | ImGuiTableFlags_BordersInnerH
            | ImGuiTableFlags_NoSavedSettings;
        if (ImGui::BeginTable("##selected_parts", 3, flags)) {
            ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthStretch, 0.85f);
            ImGui::TableSetupColumn("pattern", ImGuiTableColumnFlags_WidthStretch, 1.15f);
            ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed, ui.Scale(86.0f));
            for (std::size_t index = 0; index < state.selectedParts.size(); ++index) {
                const SelectedPart& part = state.selectedParts[index];
                const text_pattern_builder::Replacement& replacement = part.replacement;
                const bool sourceValid = replacement.offset <= state.preparedSample.size()
                    && replacement.length <= state.preparedSample.size() - replacement.offset;
                const std::string_view source = sourceValid
                    ? std::string_view(state.preparedSample).substr(replacement.offset, replacement.length)
                    : std::string_view{};
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped(
                    "%.*s",
                    static_cast<int>(source.size()),
                    source.empty() ? "" : source.data());
                if (!part.captureName.empty()) {
                    ImGui::TextDisabled("[chatwordsex(%s)]", part.captureName.c_str());
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", replacement.pattern.c_str());
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Button(ui.Text(UiText::TextPatternRemovePart), ImVec2(-FLT_MIN, 0.0f))) {
                    removeIndex = index;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    if (removeIndex < state.selectedParts.size()) {
        RemoveReplacement(state, removeIndex);
    }
}

std::string NextAvailableCaptureName(const State& state) {
    for (std::size_t ordinal = 1; ordinal <= state.selectedParts.size() + 1; ++ordinal) {
        std::string candidate = ordinal == 1
            ? "value"
            : "value" + std::to_string(ordinal);
        const bool alreadyUsed = std::any_of(
            state.selectedParts.begin(),
            state.selectedParts.end(),
            [&](const SelectedPart& part) {
                return part.captureName == candidate;
            });
        if (!alreadyUsed) {
            return candidate;
        }
    }
    return "value";
}

} // namespace

void SetPreparedSample(State& state, std::string sample) {
    if (state.preparedSample == sample) {
        return;
    }
    state = {};
    state.preparedSample = std::move(sample);
}

void RefreshSelection(State& state) {
    int selectionStart = state.selectionStartByte;
    int selectionEnd = state.selectionEndByte;
    if (selectionStart > selectionEnd) {
        std::swap(selectionStart, selectionEnd);
    }
    if (selectionStart < 0 || selectionEnd <= selectionStart) {
        state.selectionResult = {};
        state.selectionResult.error = "empty_selection";
        state.selectedSuggestionIndex = -1;
        return;
    }
    state.selectionResult = text_pattern_builder::SuggestSelection(
        state.preparedSample,
        static_cast<std::size_t>(selectionStart),
        static_cast<std::size_t>(selectionEnd));
    if (state.selectionResult.error.empty()) {
        FilterSuggestionsToSelection(state.selectionResult);
    }
    SelectRecommendedSuggestion(state);
}

void AddReplacement(State& state, std::string pattern, std::string captureName) {
    int selectionStart = state.selectionStartByte;
    int selectionEnd = state.selectionEndByte;
    if (selectionStart > selectionEnd) {
        std::swap(selectionStart, selectionEnd);
    }
    if (selectionStart < 0 || selectionEnd <= selectionStart) {
        return;
    }
    const std::size_t start = static_cast<std::size_t>(selectionStart);
    const std::size_t end = static_cast<std::size_t>(selectionEnd);
    if (end > state.preparedSample.size()) {
        return;
    }
    for (std::size_t index = state.selectedParts.size(); index-- > 0;) {
        const SelectedPart& part = state.selectedParts[index];
        const text_pattern_builder::Replacement& existing = part.replacement;
        const std::size_t existingEnd = existing.offset + existing.length;
        const bool overlaps = existing.offset < end && start < existingEnd;
        if (!overlaps && (captureName.empty() || part.captureName != captureName)) {
            continue;
        }
        state.selectedParts.erase(state.selectedParts.begin() + static_cast<std::ptrdiff_t>(index));
    }
    state.selectedParts.push_back(SelectedPart{
        text_pattern_builder::Replacement{
            start,
            end - start,
            std::move(pattern),
        },
        std::move(captureName),
    });
    Rebuild(state);
}

DrawResult DrawInline(
    State& state,
    const char* id,
    DrawMode mode,
    std::string_view captureName) {
    UiSettings& ui = UiSettings::Instance();
    DrawResult result{};
    ImGui::PushID(id);
    ImGui::TextWrapped("%s", ui.Text(UiText::TextPatternSelectionHint));
    ImGui::InputTextMultiline(
        "##prepared_sample",
        state.preparedSample.data(),
        state.preparedSample.capacity() + 1,
        ui.Scale(ImVec2(0.0f, 72.0f)),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways,
        TrackSelection,
        &state);
    const bool openFromMouse = ImGui::IsItemHovered()
        && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    const bool openFromButton = ImGui::Button(ui.Text(UiText::TextPatternSelectionSuggest));
    if (openFromMouse || openFromButton) {
        RefreshSelection(state);
    }
    if (!state.selectionResult.error.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", PatternBuilderErrorText(state.selectionResult.error));
    } else if (DrawSuggestionPicker(state)) {
        const text_pattern_builder::Suggestion* selected = SelectedSuggestion(state);
        if (selected) {
            ImGui::TextWrapped("%s", TokenText(selected->kind, true));
            ImGui::TextWrapped("%s", selected->pattern.c_str());
            const UiText action = mode == DrawMode::Capture
                ? UiText::TextPatternCaptureFromSelection
                : UiText::TextPatternSelectionAdd;
            ImGui::BeginDisabled(mode == DrawMode::Capture && captureName.empty());
            if (ImGui::Button(ui.Text(action))) {
                std::string replacement = selected->pattern;
                std::string replacementCaptureName;
                if (mode == DrawMode::Capture) {
                    replacement = "(?<" + std::string(captureName) + '>' + replacement + ')';
                    replacementCaptureName = captureName;
                }
                const std::string addedCaptureName = replacementCaptureName;
                AddReplacement(
                    state,
                    std::move(replacement),
                    std::move(replacementCaptureName));
                if (!addedCaptureName.empty()) {
                    result.addedCaptureName = addedCaptureName;
                    result.nextCaptureName = NextAvailableCaptureName(state);
                }
            }
            ImGui::EndDisabled();
        }
    }

    DrawReplacementList(state);
    if (!state.pattern.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternPreview));
        ImGui::TextWrapped("%s", state.pattern.c_str());
        if (ImGui::Button(ui.Text(UiText::UnwantedUseInDraft))) {
            result.applied = true;
            result.pattern = state.pattern;
        }
        ImGui::SameLine();
        if (ImGui::Button(ui.Text(UiText::TextPatternSelectionRemoveAll))) {
            state.selectedParts.clear();
            Rebuild(state);
        }
    }
    if (!state.error.empty()) {
        ImGui::TextDisabled("%s", PatternBuilderErrorText(state.error));
    }
    ImGui::PopID();
    return result;
}

} // namespace text_pattern_constructor_ui
