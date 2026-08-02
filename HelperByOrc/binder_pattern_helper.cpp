#include "binder_editor.h"

#include "binder_module_impl.h"
#include "binder_tag_selector.h"
#include "text_pattern_input.h"
#include "text_pattern_ui_support.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <string>
#include <string_view>
#include <utility>

namespace binder_editor {
namespace {

constexpr char kPatternHelperPopupId[] = "binder_text_pattern_helper";

enum class PatternWorkspaceMode {
    Automatic = 0,
    Manual,
    Capture,
    Reference,
};

ImVec2 FitModalMinimum(const ImVec2& preferredMinimum, const ImVec2& maximum) {
    return {
        std::min(preferredMinimum.x, maximum.x),
        std::min(preferredMinimum.y, maximum.y),
    };
}

PatternWorkspaceMode CurrentWorkspaceMode(const TextPatternHelperState& helper) {
    return static_cast<PatternWorkspaceMode>(std::clamp(
        helper.workspaceMode,
        static_cast<int>(PatternWorkspaceMode::Automatic),
        static_cast<int>(PatternWorkspaceMode::Reference)));
}

void DrawPatternModeBar(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const float rowRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    float previousRight = 0.0f;
    bool first = true;
    const auto drawMode = [&](PatternWorkspaceMode mode, UiText label) {
        const char* text = ui.Text(label);
        const float width = ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (!first && previousRight + ImGui::GetStyle().ItemSpacing.x + width <= rowRight) {
            ImGui::SameLine();
        }
        const bool selected = CurrentWorkspaceMode(helper) == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(text)) {
            helper.workspaceMode = static_cast<int>(mode);
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        previousRight = ImGui::GetItemRectMax().x;
        first = false;
    };

    drawMode(PatternWorkspaceMode::Automatic, UiText::PatternModeAutomatic);
    drawMode(PatternWorkspaceMode::Manual, UiText::PatternModeManual);
    drawMode(PatternWorkspaceMode::Capture, UiText::PatternModeCapture);
    drawMode(PatternWorkspaceMode::Reference, UiText::PatternModeReference);
}

struct PreparedSample {
    std::string normalized;
    bool timestampRemoved = false;
};

struct CapturePreset {
    UiText label = UiText::Count;
    std::string_view pattern{};
    bool custom = false;
};

constexpr std::array<CapturePreset, 6> kCapturePresets{{
    {UiText::EditorPatternCaptureLatinWord, "[A-Za-z]+", false},
    {UiText::EditorPatternCapturePlayerId, "[0-9]{1,4}", false},
    {UiText::EditorPatternCaptureInteger, "-?[0-9]+", false},
    {UiText::EditorPatternCaptureNonSpace, "\\S+", false},
    {UiText::EditorPatternCaptureLineText, "[^\\r\\n]+", false},
    {UiText::EditorPatternCaptureCustom, {}, true},
}};

PreparedSample PrepareSample(std::string_view sample) {
    const text_pattern_input::ChatlogSample chatlog = text_pattern_input::ExtractChatlogPayload(sample);
    return {
        binder_internal::NormalizeTriggerText(chatlog.payload),
        chatlog.timestampRemoved,
    };
}

struct InputTextUserData {
    std::string* value = nullptr;
    State* editor = nullptr;
    bool helperField = false;
};

int InputTextCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<InputTextUserData*>(data->UserData);
    if (!userData || !userData->value || !userData->editor) {
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        std::string& value = *userData->value;
        value.resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value.data();
        return 0;
    }

    if (data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
        return 0;
    }

    TextPatternHelperState& helper = userData->editor->textPatternHelper;
    if (userData->helperField && helper.restoreHelperFocus) {
        const int textLength = data->BufTextLen;
        const int cursor = std::clamp(helper.cursorByte, 0, textLength);
        const int selectionStart = std::clamp(helper.selectionStartByte, 0, textLength);
        const int selectionEnd = std::clamp(helper.selectionEndByte, 0, textLength);
        data->CursorPos = cursor;
        data->SelectionStart = selectionStart;
        data->SelectionEnd = selectionEnd;
        helper.restoreHelperFocus = false;
    }

    helper.cursorByte = data->CursorPos;
    helper.selectionStartByte = data->SelectionStart;
    helper.selectionEndByte = data->SelectionEnd;
    return 0;
}

bool DrawBoundTriggerInput(State& editor, const char* label, const char* hint, bool helperField) {
    std::string& value = editor.draft.textTrigger.text;
    if (value.capacity() < 256) {
        value.reserve(256);
    }

    TextPatternHelperState& helper = editor.textPatternHelper;
    if (helperField && helper.restoreHelperFocus) {
        ImGui::SetKeyboardFocusHere();
    }

    InputTextUserData userData{&value, &editor, helperField};
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll
        | ImGuiInputTextFlags_CallbackResize
        | ImGuiInputTextFlags_CallbackAlways;
    const bool changed = ImGui::InputTextWithHint(
        label,
        hint,
        value.data(),
        value.capacity() + 1,
        flags,
        InputTextCallback,
        &userData);
    if (changed) {
        helper.validationReady = false;
    }
    return changed;
}

void SetTriggerText(State& editor, std::string value) {
    editor.draft.textTrigger.text = std::move(value);
    editor.draft.textTrigger.pattern = true;
    TextPatternHelperState& helper = editor.textPatternHelper;
    helper.cursorByte = static_cast<int>(editor.draft.textTrigger.text.size());
    helper.selectionStartByte = helper.cursorByte;
    helper.selectionEndByte = helper.cursorByte;
    helper.restoreHelperFocus = true;
    helper.validationReady = false;
}

void InsertTriggerText(State& editor, std::string_view value) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    std::string& target = editor.draft.textTrigger.text;
    const int textLength = static_cast<int>(target.size());
    int selectionStart = helper.selectionStartByte;
    int selectionEnd = helper.selectionEndByte;
    if (selectionStart < 0 || selectionEnd < 0) {
        selectionStart = helper.cursorByte;
        selectionEnd = helper.cursorByte;
    }
    if (selectionStart < 0 || selectionEnd < 0) {
        selectionStart = textLength;
        selectionEnd = textLength;
    }
    selectionStart = std::clamp(selectionStart, 0, textLength);
    selectionEnd = std::clamp(selectionEnd, 0, textLength);
    if (selectionStart > selectionEnd) {
        std::swap(selectionStart, selectionEnd);
    }
    target.replace(
        static_cast<std::size_t>(selectionStart),
        static_cast<std::size_t>(selectionEnd - selectionStart),
        value);
    editor.draft.textTrigger.pattern = true;
    helper.cursorByte = selectionStart + static_cast<int>(value.size());
    helper.selectionStartByte = helper.cursorByte;
    helper.selectionEndByte = helper.cursorByte;
    helper.restoreHelperFocus = true;
    helper.validationReady = false;
}

bool IsValidCaptureName(std::string_view name) {
    if (name.empty() || name.size() > 64) {
        return false;
    }
    const auto isAsciiLetter = [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    };
    if (!isAsciiLetter(name.front())) {
        return false;
    }
    return std::all_of(name.begin() + 1, name.end(), [&](char ch) {
        return isAsciiLetter(ch) || (ch >= '0' && ch <= '9') || ch == '_';
    });
}

std::string CapturePattern(const TextPatternHelperState& helper) {
    const int presetIndex = std::clamp(
        helper.capturePreset,
        0,
        static_cast<int>(kCapturePresets.size()) - 1);
    const CapturePreset& preset = kCapturePresets[static_cast<std::size_t>(presetIndex)];
    return preset.custom ? helper.captureCustomPattern : std::string(preset.pattern);
}

std::string SelectedTriggerText(const State& editor) {
    const TextPatternHelperState& helper = editor.textPatternHelper;
    const std::string& target = editor.draft.textTrigger.text;
    int selectionStart = helper.selectionStartByte;
    int selectionEnd = helper.selectionEndByte;
    if (selectionStart < 0 || selectionEnd < 0 || selectionStart == selectionEnd) {
        return {};
    }
    if (selectionStart > selectionEnd) {
        std::swap(selectionStart, selectionEnd);
    }
    const int textLength = static_cast<int>(target.size());
    selectionStart = std::clamp(selectionStart, 0, textLength);
    selectionEnd = std::clamp(selectionEnd, 0, textLength);
    if (selectionStart == selectionEnd) {
        return {};
    }
    return target.substr(
        static_cast<std::size_t>(selectionStart),
        static_cast<std::size_t>(selectionEnd - selectionStart));
}

std::string BuildCaptureGroup(const State& editor) {
    const TextPatternHelperState& helper = editor.textPatternHelper;
    const std::string selected = SelectedTriggerText(editor);
    const std::string content = selected.empty() ? CapturePattern(helper) : selected;
    if (!IsValidCaptureName(helper.captureGroupName) || content.empty()) {
        return {};
    }
    return "(?<" + helper.captureGroupName + '>' + content + ')';
}

const char* TokenLabel(text_pattern_builder::TokenKind kind) {
    UiSettings& ui = UiSettings::Instance();
    const UiText text = text_pattern_ui::TokenLabel(kind);
    return text == UiText::Count ? "?" : ui.Text(text);
}

const char* TokenHelp(text_pattern_builder::TokenKind kind) {
    UiSettings& ui = UiSettings::Instance();
    const UiText text = text_pattern_ui::TokenHelp(kind);
    return text == UiText::Count ? "" : ui.Text(text);
}

void Regenerate(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    helper.options.colors = false;
    const std::string normalized = PrepareSample(helper.sample).normalized;
    text_pattern_constructor_ui::SetPreparedSample(helper.constructor, normalized);
    const text_pattern_builder::Result built = text_pattern_builder::Build(normalized, helper.options);
    helper.exact = built.exact;
    helper.recommended = built.recommended;
    helper.contains = built.contains;
    helper.tokens = built.tokens;
    helper.builderWarning.clear();
    helper.exactValid = false;
    helper.recommendedValid = false;
    helper.containsValid = false;
    helper.outputLanguage = static_cast<int>(UiSettings::Instance().Language());
    helper.validationReady = false;

    UiSettings& ui = UiSettings::Instance();
    if (!built.error.empty()) {
        helper.builderWarning = ui.Text(UiText::UnwantedHelperInvalidUtf8);
        helper.exact.clear();
        helper.recommended.clear();
        helper.contains.clear();
        helper.tokens.clear();
        return;
    }

    const auto matchesSource = [&](const std::string& pattern) {
        if (pattern.empty()) {
            return false;
        }
        text_pattern::CompileResult compiled = text_pattern::Compile(pattern, false);
        return compiled.program
            && compiled.program->Match(normalized).status == text_pattern::MatchStatus::Match;
    };
    helper.exactValid = matchesSource(helper.exact);
    helper.recommendedValid = matchesSource(helper.recommended);
    helper.containsValid = matchesSource(helper.contains);
    if (!helper.recommendedValid && helper.exactValid) {
        helper.recommended = helper.exact;
        helper.recommendedValid = true;
        helper.contains = helper.exact.size() >= 4
            ? helper.exact.substr(2, helper.exact.size() - 4)
            : std::string{};
        helper.containsValid = matchesSource(helper.contains);
        helper.builderWarning = ui.Text(UiText::UnwantedHelperExactFallback);
    }
}

void RefreshValidation(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const int language = static_cast<int>(ui.Language());
    if (helper.validationReady
        && helper.validationPattern == editor.draft.textTrigger.text
        && helper.validationSample == helper.sample
        && helper.validationPatternEnabled == editor.draft.textTrigger.pattern
        && helper.validationLanguage == language) {
        return;
    }

    helper.validationReady = true;
    helper.validationPattern = editor.draft.textTrigger.text;
    helper.validationSample = helper.sample;
    helper.validationPatternEnabled = editor.draft.textTrigger.pattern;
    helper.validationLanguage = language;
    helper.validationError.clear();
    helper.validationWarning.clear();
    helper.validationMatched = false;
    helper.validationRegexMatched = false;
    helper.validationTested = helper.testRequested || !helper.sample.empty();
    helper.validationTimestampRemoved = false;
    helper.validationProgram.reset();
    helper.validationCaptures = {};

    const PreparedSample preparedSample = PrepareSample(helper.sample);
    helper.validationNormalizedSample = preparedSample.normalized;
    helper.validationTimestampRemoved = preparedSample.timestampRemoved;
    const std::string normalizedPattern = binder_internal::NormalizeTriggerText(editor.draft.textTrigger.text);
    if (normalizedPattern.empty()) {
        return;
    }
    if (!editor.draft.textTrigger.pattern) {
        helper.validationMatched = helper.validationNormalizedSample == normalizedPattern;
        return;
    }

    const std::string& pattern = editor.draft.textTrigger.text;
    const bool legacyAnchored = pattern.size() >= 2 && pattern.front() == '^' && pattern.back() == '$';
    const bool absoluteAnchored = pattern.size() >= 4
        && pattern.rfind("\\A", 0) == 0
        && pattern.substr(pattern.size() - 2) == "\\z";
    if (!legacyAnchored && !absoluteAnchored) {
        helper.validationWarning = ui.Text(UiText::UnwantedRegexSafetyUnanchored);
    }
    if (text_pattern_ui::ContainsBroadWildcard(pattern)) {
        text_pattern_ui::AppendWarning(helper.validationWarning, ui.Text(UiText::UnwantedRegexBroadWildcard));
    }

    text_pattern::CompileResult compiled = text_pattern::Compile(pattern, false);
    if (!compiled.program) {
        helper.validationError = ui.Format(
            UiText::UnwantedPcreErrorFormat,
            text_pattern_ui::FormatCompilePosition(pattern, compiled.errorOffset).c_str());
        return;
    }
    if (compiled.program->MatchesEmpty()) {
        text_pattern_ui::AppendWarning(helper.validationWarning, ui.Text(UiText::EditorPatternMatchesEmpty));
    }

    helper.validationProgram = std::shared_ptr<text_pattern::Program>(std::move(compiled.program));
    const text_pattern::MatchResult match =
        helper.validationProgram->Match(helper.validationNormalizedSample, &helper.validationCaptures);
    if (match.status == text_pattern::MatchStatus::Match) {
        helper.validationMatched = true;
        helper.validationRegexMatched = true;
    } else if (match.status == text_pattern::MatchStatus::NoMatch) {
        helper.validationMatched = helper.validationNormalizedSample == normalizedPattern;
    } else {
        helper.validationError = ui.Text(UiText::EditorPatternTestStopped);
    }
}

bool CurrentPatternHasCaptureName(const State& editor, std::string_view captureName) {
    const TextPatternHelperState& helper = editor.textPatternHelper;
    if (captureName.empty()
        || !editor.draft.textTrigger.pattern
        || !helper.validationReady
        || !helper.validationPatternEnabled
        || helper.validationPattern != editor.draft.textTrigger.text
        || !helper.validationProgram) {
        return false;
    }
    for (std::size_t groupIndex = 1;
         groupIndex <= helper.validationProgram->CaptureCount();
         ++groupIndex) {
        if (helper.validationProgram->CaptureName(groupIndex) == captureName) {
            return true;
        }
    }
    return false;
}

void DrawOptions(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    bool changed = false;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool first = true;
    float previousRight = 0.0f;
    const auto drawOption = [&](bool& value, UiText label, UiText help) {
        const float width = ImGui::GetFrameHeight()
            + ImGui::GetStyle().ItemInnerSpacing.x
            + ImGui::CalcTextSize(ui.Text(label)).x;
        if (!first && previousRight + ImGui::GetStyle().ItemSpacing.x + width <= right) {
            ImGui::SameLine();
        }
        changed |= ImGui::Checkbox(ui.Text(label), &value);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", ui.Text(help));
        }
        previousRight = ImGui::GetItemRectMax().x;
        first = false;
    };

    drawOption(helper.options.numbers, UiText::UnwantedHelperNumbers, UiText::UnwantedTokenIntegerHelp);
    drawOption(helper.options.money, UiText::UnwantedHelperMoney, UiText::UnwantedTokenMoneyHelp);
    drawOption(helper.options.time, UiText::UnwantedHelperTime, UiText::UnwantedTokenClockHelp);
    drawOption(helper.options.nicknames, UiText::UnwantedHelperNick, UiText::UnwantedTokenNicknameHelp);
    drawOption(helper.options.playerIds, UiText::UnwantedHelperPlayerId, UiText::UnwantedTokenPlayerIdHelp);
    drawOption(helper.options.domains, UiText::UnwantedHelperDomain, UiText::UnwantedTokenDomainHelp);
    drawOption(helper.options.bracketPrefixes, UiText::UnwantedHelperBracketTag, UiText::UnwantedTokenBracketPrefixHelp);
    if (changed) {
        Regenerate(editor);
    }
}

void DrawVariants(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    if (helper.exact.empty() && helper.contains.empty()) {
        return;
    }

    ImGui::SeparatorText(ui.Text(UiText::TextPatternOtherVariants));
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("##binder_pattern_variants", 3, flags)) {
        return;
    }
    ImGui::TableSetupColumn("variant", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(140.0f));
    ImGui::TableSetupColumn("pattern", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(110.0f));
    const auto drawVariant = [&](UiText title, const std::string& pattern, bool valid) {
        if (pattern.empty()) {
            return;
        }
        ImGui::PushID(static_cast<int>(title));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(title));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", pattern.c_str());
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(pattern.c_str());
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button(ui.Text(UiText::UnwantedUseInDraft), ImVec2(-FLT_MIN, 0.0f))) {
            SetTriggerText(editor, pattern);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    };
    drawVariant(UiText::UnwantedHelperExact, helper.exact, helper.exactValid);
    drawVariant(UiText::UnwantedHelperContains, helper.contains, helper.containsValid);
    ImGui::EndTable();
}

void DrawQuickPatternSection(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();

    if (!helper.recommended.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperGeneralized));
        ImGui::TextWrapped("%s", helper.recommended.c_str());
        ImGui::BeginDisabled(!helper.recommendedValid);
        if (ImGui::Button(
                ui.Text(UiText::UnwantedUseInDraft),
                binder_internal::ScaleUi(150.0f, 0.0f))) {
            SetTriggerText(editor, helper.recommended);
        }
        ImGui::EndDisabled();
    }
    if (!helper.builderWarning.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s", helper.builderWarning.c_str());
    }

    if (ImGui::CollapsingHeader(ui.Text(UiText::TextPatternAdvanced))) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedGeneralizations));
        DrawOptions(editor);
        if (!helper.tokens.empty()) {
            ImGui::SeparatorText(ui.Text(UiText::UnwantedDetectedTokens));
            for (const text_pattern_builder::Token& token : helper.tokens) {
                ImGui::PushID(static_cast<int>(token.offset));
                ImGui::TextDisabled("%s", TokenLabel(token.kind));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s", TokenHelp(token.kind));
                }
                ImGui::SameLine();
                ImGui::TextWrapped("%s  ->  %s", token.source.c_str(), token.pattern.c_str());
                ImGui::PopID();
            }
        }
        DrawVariants(editor);
    }

}

void DrawManualPatternSection(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    const text_pattern_constructor_ui::DrawResult manual =
        text_pattern_constructor_ui::DrawInline(helper.constructor, "binder_manual");
    if (manual.applied) {
        SetTriggerText(editor, manual.pattern);
    }
}

void DrawAdvancedCaptureBuilder(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const bool nameValid = IsValidCaptureName(helper.captureGroupName);

    const int presetIndex = std::clamp(
        helper.capturePreset,
        0,
        static_cast<int>(kCapturePresets.size()) - 1);
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureType));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(
            "##binder_capture_type",
            ui.Text(kCapturePresets[static_cast<std::size_t>(presetIndex)].label))) {
        for (std::size_t index = 0; index < kCapturePresets.size(); ++index) {
            const bool selected = helper.capturePreset == static_cast<int>(index);
            if (ImGui::Selectable(ui.Text(kCapturePresets[index].label), selected)) {
                helper.capturePreset = static_cast<int>(index);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const CapturePreset& activePreset =
        kCapturePresets[static_cast<std::size_t>(std::clamp(
            helper.capturePreset,
            0,
            static_cast<int>(kCapturePresets.size()) - 1))];
    if (activePreset.custom) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        binder_internal::InputTextWithHintString(
            "##binder_capture_custom_pattern",
            ui.Text(UiText::EditorPatternCaptureCustomPattern),
            helper.captureCustomPattern,
            ImGuiInputTextFlags_AutoSelectAll,
            256);
    }

    const std::string group = BuildCaptureGroup(editor);
    const std::string variable = nameValid
        ? "[chatwordsex(" + helper.captureGroupName + ")]"
        : std::string{};
    const ImGuiTableFlags previewFlags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##binder_capture_preview", 2, previewFlags)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(150.0f));
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureGroupPreview));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", group.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureVariablePreview));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", variable.c_str());
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureSelectionHint));

    const bool canInsert = !group.empty();
    ImGui::BeginDisabled(!canInsert);
    if (ImGui::Button(
            ui.Text(UiText::EditorPatternCaptureInsert),
            binder_internal::ScaleUi(160.0f, 0.0f))) {
        InsertTriggerText(editor, group);
        RefreshValidation(editor);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool captureApplied = nameValid
        && CurrentPatternHasCaptureName(editor, helper.captureGroupName);
    ImGui::PushID("capture_copy_advanced");
    ImGui::BeginDisabled(!captureApplied);
    if (ImGui::Button(ui.Text(UiText::EditorPatternCaptureCopy))) {
        ImGui::SetClipboardText(variable.c_str());
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (canInsert && !captureApplied) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureApplyFirst));
    }
}

void DrawCaptureBuilder(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();

    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternCaptureHint));
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureName));
    ImGui::SetNextItemWidth(-FLT_MIN);
    binder_internal::InputTextWithHintString(
        "##binder_capture_name",
        ui.Text(UiText::EditorPatternCaptureNameHint),
        helper.captureGroupName,
        ImGuiInputTextFlags_AutoSelectAll,
        64);
    const bool nameValid = IsValidCaptureName(helper.captureGroupName);
    if (!nameValid) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
            "%s",
            ui.Text(UiText::EditorPatternCaptureNameInvalid));
    }

    if (helper.constructor.preparedSample.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperInputHint));
    } else {
        const text_pattern_constructor_ui::DrawResult capture =
            text_pattern_constructor_ui::DrawInline(
                helper.constructor,
                "binder_capture",
                text_pattern_constructor_ui::DrawMode::Capture,
                nameValid ? std::string_view(helper.captureGroupName) : std::string_view{});
        if (capture.applied) {
            SetTriggerText(editor, capture.pattern);
            RefreshValidation(editor);
        }
    }

    const std::string variable = nameValid
        ? "[chatwordsex(" + helper.captureGroupName + ")]"
        : std::string{};
    const bool captureApplied = nameValid
        && CurrentPatternHasCaptureName(editor, helper.captureGroupName);
    if (captureApplied) {
        ImGui::TextDisabled(
            "%s: %s",
            ui.Text(UiText::EditorPatternCaptureVariablePreview),
            variable.c_str());
    } else if (nameValid && !helper.constructor.pattern.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureApplyFirst));
    }
    ImGui::PushID("capture_copy_primary");
    ImGui::BeginDisabled(!captureApplied);
    if (ImGui::Button(ui.Text(UiText::EditorPatternCaptureCopy))) {
        ImGui::SetClipboardText(variable.c_str());
    }
    ImGui::EndDisabled();
    ImGui::PopID();
}

void DrawCaptureResults(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();

    if (!ImGui::CollapsingHeader(ui.Text(UiText::EditorPatternCaptureResults))) {
        return;
    }
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternCaptureResultsHint));
    if (!editor.draft.textTrigger.pattern || !helper.validationProgram) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureNeedsPattern));
        return;
    }
    if (!helper.validationRegexMatched || !helper.validationError.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureNeedsMatch));
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable("##binder_capture_results", 4, flags)) {
        return;
    }
    ImGui::TableSetupColumn(
        ui.Text(UiText::EditorPatternCaptureColumnGroup),
        ImGuiTableColumnFlags_WidthFixed,
        binder_internal::ScaleUi(70.0f));
    ImGui::TableSetupColumn(
        ui.Text(UiText::EditorPatternCaptureColumnName),
        ImGuiTableColumnFlags_WidthFixed,
        binder_internal::ScaleUi(120.0f));
    ImGui::TableSetupColumn(
        ui.Text(UiText::EditorPatternCaptureColumnValue),
        ImGuiTableColumnFlags_WidthStretch,
        1.0f);
    ImGui::TableSetupColumn(
        ui.Text(UiText::EditorPatternCaptureColumnVariable),
        ImGuiTableColumnFlags_WidthStretch,
        1.25f);
    ImGui::TableHeadersRow();

    const std::size_t captureCount = helper.validationProgram->CaptureCount();
    for (std::size_t groupIndex = 0; groupIndex <= captureCount; ++groupIndex) {
        ImGui::PushID(static_cast<int>(groupIndex));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%zu", groupIndex);

        ImGui::TableSetColumnIndex(1);
        const std::string_view groupName = helper.validationProgram->CaptureName(groupIndex);
        if (groupIndex == 0) {
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureWholeMatch));
        } else if (groupName.empty()) {
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCaptureUnnamed));
        } else {
            ImGui::TextWrapped("%.*s", static_cast<int>(groupName.size()), groupName.data());
        }

        ImGui::TableSetColumnIndex(2);
        const std::string_view value = helper.validationProgram->Capture(
            helper.validationNormalizedSample,
            helper.validationCaptures,
            groupIndex);
        ImGui::TextWrapped(
            "%.*s",
            static_cast<int>(value.size()),
            value.empty() ? "" : value.data());

        ImGui::TableSetColumnIndex(3);
        const std::string numericVariable = "[chatwordsex(" + std::to_string(groupIndex) + ")]";
        if (ImGui::SmallButton(numericVariable.c_str())) {
            ImGui::SetClipboardText(numericVariable.c_str());
        }
        if (!groupName.empty()) {
            const std::string namedVariable = "[chatwordsex(" + std::string(groupName) + ")]";
            const float namedWidth = ImGui::CalcTextSize(namedVariable.c_str()).x
                + ImGui::GetStyle().FramePadding.x * 2.0f;
            if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + namedWidth
                <= ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x) {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(namedVariable.c_str())) {
                ImGui::SetClipboardText(namedVariable.c_str());
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void DrawValidationStatus(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();

    if (helper.validationTimestampRemoved) {
        ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternChatlogTimestampRemoved));
    }
    if (ImGui::Button(ui.Text(UiText::UnwantedTestAction), binder_internal::ScaleUi(120.0f, 0.0f))) {
        helper.testRequested = true;
        helper.validationReady = false;
    }
    RefreshValidation(editor);
    if (!helper.validationError.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.36f, 1.0f), "%s", helper.validationError.c_str());
    } else if (helper.validationTested) {
        ImGui::SameLine();
        const ImVec4 color = helper.validationMatched
            ? ImVec4(0.42f, 0.84f, 0.55f, 1.0f)
            : ImVec4(0.92f, 0.62f, 0.38f, 1.0f);
        ImGui::TextColored(
            color,
            "%s",
            ui.Text(helper.validationMatched ? UiText::EditorPatternMatched : UiText::EditorPatternNoMatch));
    }
    if (!helper.validationWarning.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s", helper.validationWarning.c_str());
    }
}

void DrawPatternResult(State& editor) {
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::TextPatternResult));
    ImGui::SetNextItemWidth(-FLT_MIN);
    DrawBoundTriggerInput(editor, "##binder_pattern_result", ui.Text(UiText::EditorTriggerExample), true);
    DrawValidationStatus(editor);
}

void RefreshReferenceFilter(TextPatternHelperState& helper) {
    UiSettings& ui = UiSettings::Instance();
    const int language = static_cast<int>(ui.Language());
    if (helper.referenceFilterLanguage == language
        && helper.referenceFilterSearch == helper.referenceSearch) {
        return;
    }

    helper.referenceFilterLanguage = language;
    helper.referenceFilterSearch = helper.referenceSearch;
    helper.referenceVisibleItems.clear();
    const std::span<const text_pattern_ui::ReferenceItem> items = text_pattern_ui::ReferenceItems();
    helper.referenceVisibleItems.reserve(items.size());
    for (std::size_t itemIndex = 0; itemIndex < items.size(); ++itemIndex) {
        const text_pattern_ui::ReferenceItem& item = items[itemIndex];
        if (item.requiresRawColorCodes) {
            continue;
        }
        if (helper.referenceFilterSearch.empty()) {
            helper.referenceVisibleItems.push_back(itemIndex);
            continue;
        }
        const std::string searchable = std::string(item.expression)
            + "\n" + ui.Text(item.category)
            + "\n" + ui.Text(item.description);
        if (binder_tags::ContainsNoCaseUtf8(searchable, helper.referenceFilterSearch)) {
            helper.referenceVisibleItems.push_back(itemIndex);
        }
    }
}

void DrawReferencePanel(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternReferenceHint));
    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::UnwantedRegexReferenceSearch);
    ImGui::SetNextItemWidth(-FLT_MIN);
    binder_internal::InputTextWithHintString(
        "##binder_pattern_reference_search",
        searchHint.c_str(),
        helper.referenceSearch,
        ImGuiInputTextFlags_AutoSelectAll,
        256);
    RefreshReferenceFilter(helper);
    const std::span<const text_pattern_ui::ReferenceItem> items = text_pattern_ui::ReferenceItems();
    const auto appendItem = [&](const text_pattern_ui::ReferenceItem& item) {
        InsertTriggerText(editor, item.expression);
    };

    const bool anyVisible = !helper.referenceVisibleItems.empty();
    const bool compact = ImGui::GetContentRegionAvail().x < binder_internal::ScaleUi(680.0f);
    if (compact) {
        if (ImGui::BeginChild(
                "##binder_pattern_reference_cards",
                ImVec2(0.0f, binder_internal::ScaleUi(300.0f)),
                ImGuiChildFlags_Borders)) {
            for (const std::size_t itemIndex : helper.referenceVisibleItems) {
                const text_pattern_ui::ReferenceItem& item = items[itemIndex];
                ImGui::PushID(item.expression);
                ImGui::TextDisabled("%s", ui.Text(item.category));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", item.expression);
                ImGui::TextWrapped("%s", ui.Text(item.description));
                if (ImGui::Button(ui.Text(UiText::UnwantedCopy), binder_internal::ScaleUi(100.0f, 0.0f))) {
                    ImGui::SetClipboardText(item.expression);
                }
                ImGui::SameLine();
                if (ImGui::Button(
                        ui.Text(UiText::UnwantedRegexReferenceAppend),
                        binder_internal::ScaleUi(120.0f, 0.0f))) {
                    appendItem(item);
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (!anyVisible) {
                ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRegexReferenceNoResults));
            }
        }
        ImGui::EndChild();
        return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_NoSavedSettings;
    if (!ImGui::BeginTable(
            "##binder_pattern_reference_table",
            4,
            flags,
            ImVec2(0.0f, binder_internal::ScaleUi(300.0f)))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceCategory), ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(105.0f));
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceExpression), ImGuiTableColumnFlags_WidthStretch, 0.85f);
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceDescription), ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("##append", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(105.0f));
    ImGui::TableHeadersRow();
    for (const std::size_t itemIndex : helper.referenceVisibleItems) {
        const text_pattern_ui::ReferenceItem& item = items[itemIndex];
        ImGui::PushID(item.expression);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(item.category));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", item.expression);
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(item.expression);
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::TextWrapped("%s", ui.Text(item.description));
        ImGui::TableSetColumnIndex(3);
        if (ImGui::Button(ui.Text(UiText::UnwantedRegexReferenceAppend), ImVec2(-FLT_MIN, 0.0f))) {
            appendItem(item);
        }
        ImGui::PopID();
    }
    if (!anyVisible) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedRegexReferenceNoResults));
    }
    ImGui::EndTable();
}

} // namespace

bool DrawTextTriggerInput(State& editor, const char* label, const char* hint) {
    return DrawBoundTriggerInput(editor, label, hint, false);
}

void DrawTextPatternHelperPopup(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::EditorPatternHelperTitle)) + "###" + kPatternHelperPopupId;
    if (helper.popupPending) {
        ImGui::OpenPopup(title.c_str());
        helper.popupPending = false;
        helper.validationReady = false;
        if (!helper.sample.empty()) {
            Regenerate(editor);
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 helperMaximum(viewport->WorkSize.x * 0.94f, viewport->WorkSize.y * 0.94f);
    ImGui::SetNextWindowSizeConstraints(
        FitModalMinimum(binder_internal::ScaleUi(520.0f, 400.0f), helperMaximum),
        helperMaximum);
    ImGui::SetNextWindowSize(
        ImVec2(std::min(binder_internal::ScaleUi(820.0f), viewport->WorkSize.x * 0.90f),
            std::min(binder_internal::ScaleUi(720.0f), viewport->WorkSize.y * 0.88f)),
        ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    if (!open || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const float footerHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##binder_pattern_helper_body", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None)) {
        if (ImGui::Checkbox(ui.Text(UiText::EditorPatternEnabled), &editor.draft.textTrigger.pattern)) {
            helper.validationReady = false;
        }
        ImGui::Text("%s", ui.Text(UiText::EditorPatternSample));
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternSampleHint));
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool sampleChanged = binder_internal::InputTextWithHintString(
            "##binder_pattern_sample",
            ui.Text(UiText::UnwantedHelperInputHint),
            helper.sample,
            ImGuiInputTextFlags_AutoSelectAll,
            1024);
        if (sampleChanged) {
            helper.testRequested = true;
            Regenerate(editor);
        } else if (helper.outputLanguage != static_cast<int>(ui.Language()) && !helper.sample.empty()) {
            Regenerate(editor);
        }
        RefreshValidation(editor);

        ImGui::Separator();
        DrawPatternModeBar(editor);
        ImGui::Spacing();
        const PatternWorkspaceMode workspaceMode = CurrentWorkspaceMode(helper);
        switch (workspaceMode) {
        case PatternWorkspaceMode::Automatic:
            DrawQuickPatternSection(editor);
            break;
        case PatternWorkspaceMode::Manual:
            if (helper.constructor.preparedSample.empty()) {
                ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedHelperInputHint));
            } else {
                DrawManualPatternSection(editor);
            }
            break;
        case PatternWorkspaceMode::Capture:
            DrawCaptureBuilder(editor);
            break;
        case PatternWorkspaceMode::Reference:
            DrawReferencePanel(editor);
            break;
        }

        DrawPatternResult(editor);
        if (workspaceMode == PatternWorkspaceMode::Capture) {
            if (ImGui::CollapsingHeader(ui.Text(UiText::EditorPatternCaptureAdvanced))) {
                DrawAdvancedCaptureBuilder(editor);
            }
            DrawCaptureResults(editor);
        }
    }
    ImGui::EndChild();

    if (ImGui::Button(ui.Text(UiText::Close), binder_internal::ScaleUi(120.0f, 0.0f))) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace binder_editor
