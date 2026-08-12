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

ImVec2 FitModalMinimum(const ImVec2& preferredMinimum, const ImVec2& maximum) {
    return {
        std::min(preferredMinimum.x, maximum.x),
        std::min(preferredMinimum.y, maximum.y),
    };
}

struct PreparedSample {
    std::string normalized;
    bool timestampRemoved = false;
    bool colorsRemoved = false;
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
        binder_internal::StripColorTags(chatlog.payload) != chatlog.payload,
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

bool DrawBoundTriggerInput(
    State& editor,
    std::string& value,
    const char* label,
    const char* hint,
    bool helperField,
    ImGuiInputTextFlags extraFlags = ImGuiInputTextFlags_None) {
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
        | ImGuiInputTextFlags_CallbackAlways
        | extraFlags;
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
    TextPatternHelperState& helper = editor.textPatternHelper;
    helper.workingTriggerText = std::move(value);
    helper.cursorByte = static_cast<int>(helper.workingTriggerText.size());
    helper.selectionStartByte = helper.cursorByte;
    helper.selectionEndByte = helper.cursorByte;
    helper.restoreHelperFocus = true;
    helper.validationReady = false;
}

void InsertTriggerText(State& editor, std::string_view value) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    std::string& target = helper.workingTriggerText;
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
    const std::string& target = helper.workingTriggerText;
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
    const PreparedSample preparedSample = PrepareSample(helper.sample);
    const std::string& normalized = preparedSample.normalized;
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
    helper.alternateValidationReady = false;

    UiSettings& ui = UiSettings::Instance();
    if (!built.error.empty()) {
        helper.builderWarning = ui.Text(UiText::UnwantedHelperInvalidUtf8);
        helper.exact.clear();
        helper.recommended.clear();
        helper.contains.clear();
        helper.tokens.clear();
        helper.workingTriggerText.clear();
        return;
    }

    text_pattern_constructor_ui::SetAutomaticParts(helper.constructor, helper.tokens);
    helper.workingTriggerText = helper.constructor.pattern.empty()
        ? helper.exact
        : helper.constructor.pattern;

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
    if (!helper.workingTriggerText.empty() && !matchesSource(helper.workingTriggerText)) {
        helper.workingTriggerText = helper.exact;
        helper.builderWarning = ui.Text(UiText::UnwantedHelperExactFallback);
    }
}

void RefreshValidation(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const int language = static_cast<int>(ui.Language());
    if (helper.validationReady
        && helper.validationPattern == helper.workingTriggerText
        && helper.validationSample == helper.sample
        && helper.validationLanguage == language) {
        return;
    }

    helper.validationReady = true;
    helper.validationPattern = helper.workingTriggerText;
    helper.validationSample = helper.sample;
    helper.validationLanguage = language;
    helper.validationError.clear();
    helper.validationWarning.clear();
    helper.validationMatched = false;
    helper.validationRegexMatched = false;
    helper.validationMatchesEmpty = false;
    helper.validationTimestampRemoved = false;
    helper.validationColorsRemoved = false;
    helper.validationProgram.reset();
    helper.validationCaptures = {};

    const PreparedSample preparedSample = PrepareSample(helper.sample);
    helper.validationNormalizedSample = preparedSample.normalized;
    helper.validationTimestampRemoved = preparedSample.timestampRemoved;
    helper.validationColorsRemoved = preparedSample.colorsRemoved;
    const std::string normalizedPattern = binder_internal::NormalizeTriggerText(helper.workingTriggerText);
    if (normalizedPattern.empty()) {
        return;
    }

    const std::string& pattern = helper.workingTriggerText;
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
        helper.validationMatchesEmpty = true;
        text_pattern_ui::AppendWarning(helper.validationWarning, ui.Text(UiText::EditorPatternMatchesEmpty));
    }

    helper.validationProgram = std::shared_ptr<text_pattern::Program>(std::move(compiled.program));
    const text_pattern::MatchResult match =
        helper.validationProgram->Match(helper.validationNormalizedSample, &helper.validationCaptures);
    if (match.status == text_pattern::MatchStatus::Match) {
        helper.validationMatched = true;
        helper.validationRegexMatched = true;
    } else if (match.status == text_pattern::MatchStatus::NoMatch) {
        helper.validationMatched = false;
    } else {
        helper.validationError = ui.Text(UiText::EditorPatternTestStopped);
    }
}

bool CurrentPatternHasCaptureName(const State& editor, std::string_view captureName) {
    const TextPatternHelperState& helper = editor.textPatternHelper;
    if (captureName.empty()
        || !helper.validationReady
        || helper.validationPattern != helper.workingTriggerText
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
        if (ImGui::Button(ui.Text(UiText::EditorPatternVariantSelect), ImVec2(-FLT_MIN, 0.0f))) {
            SetTriggerText(editor, pattern);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    };
    drawVariant(UiText::EditorPatternVariantExact, helper.exact, helper.exactValid);
    drawVariant(UiText::EditorPatternVariantRecommended, helper.recommended, helper.recommendedValid);
    drawVariant(UiText::EditorPatternVariantContains, helper.contains, helper.containsValid);
    ImGui::EndTable();
}

std::string_view PartSource(
    const text_pattern_constructor_ui::State& constructor,
    const text_pattern_constructor_ui::SelectedPart& part) {
    const text_pattern_builder::Replacement& replacement = part.replacement;
    if (replacement.offset > constructor.preparedSample.size()
        || replacement.length > constructor.preparedSample.size() - replacement.offset) {
        return {};
    }
    return std::string_view(constructor.preparedSample).substr(
        replacement.offset,
        replacement.length);
}

void SyncWorkingTriggerFromParts(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    helper.workingTriggerText = helper.constructor.pattern;
    helper.validationReady = false;
    helper.alternateValidationReady = false;
}

void DrawVariableParts(State& editor, bool showManualAction = true) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternPartsHint));

    if (helper.constructor.selectedParts.empty()) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternNoParts));
    } else {
        std::size_t removeIndex = helper.constructor.selectedParts.size();
        const bool compact = ImGui::GetContentRegionAvail().x < binder_internal::ScaleUi(680.0f);
        if (compact) {
            for (std::size_t index = 0; index < helper.constructor.selectedParts.size(); ++index) {
                const text_pattern_constructor_ui::SelectedPart& part = helper.constructor.selectedParts[index];
                const std::string_view source = PartSource(helper.constructor, part);
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::BeginChild(
                        "##binder_pattern_part_card",
                        ImVec2(0.0f, 0.0f),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
                    bool enabled = part.enabled;
                    if (ImGui::Checkbox("##enabled", &enabled)) {
                        text_pattern_constructor_ui::SetPartEnabled(helper.constructor, index, enabled);
                        SyncWorkingTriggerFromParts(editor);
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", TokenLabel(part.kind));
                    ImGui::TextWrapped(
                        "%.*s",
                        static_cast<int>(source.size()),
                        source.empty() ? "" : source.data());
                    ImGui::TextDisabled("%s", TokenHelp(part.kind));
                    if (!part.captureName.empty()) {
                        ImGui::TextDisabled("[chatwordsex(%s)]", part.captureName.c_str());
                    }
                    if (!part.automatic
                        && ImGui::SmallButton(ui.Text(UiText::TextPatternRemovePart))) {
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
            if (ImGui::BeginTable("##binder_pattern_parts", 5, flags)) {
                ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(32.0f));
                ImGui::TableSetupColumn("kind", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(135.0f));
                ImGui::TableSetupColumn("source", ImGuiTableColumnFlags_WidthStretch, 0.9f);
                ImGui::TableSetupColumn("help", ImGuiTableColumnFlags_WidthStretch, 1.4f);
                ImGui::TableSetupColumn("remove", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(82.0f));
                for (std::size_t index = 0; index < helper.constructor.selectedParts.size(); ++index) {
                    const text_pattern_constructor_ui::SelectedPart& part = helper.constructor.selectedParts[index];
                    const std::string_view source = PartSource(helper.constructor, part);
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool enabled = part.enabled;
                    if (ImGui::Checkbox("##enabled", &enabled)) {
                        text_pattern_constructor_ui::SetPartEnabled(helper.constructor, index, enabled);
                        SyncWorkingTriggerFromParts(editor);
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", TokenLabel(part.kind));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped(
                        "%.*s",
                        static_cast<int>(source.size()),
                        source.empty() ? "" : source.data());
                    if (!part.captureName.empty()) {
                        ImGui::TextDisabled("[chatwordsex(%s)]", part.captureName.c_str());
                    }
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextDisabled("%s", TokenHelp(part.kind));
                    ImGui::TableSetColumnIndex(4);
                    if (!part.automatic
                        && ImGui::SmallButton(ui.Text(UiText::TextPatternRemovePart))) {
                        removeIndex = index;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        if (removeIndex < helper.constructor.selectedParts.size()) {
            text_pattern_constructor_ui::RemovePart(helper.constructor, removeIndex);
            SyncWorkingTriggerFromParts(editor);
        }
    }

    if (showManualAction) {
        if (ImGui::Button(
                ui.Text(UiText::EditorPatternManualAction),
                binder_internal::ScaleUi(170.0f, 0.0f))) {
            helper.manualSelectionOpen = true;
            helper.constructor.selectionResult = {};
            helper.constructor.selectionStartByte = -1;
            helper.constructor.selectionEndByte = -1;
        }
    }
}

int TrackManualSelection(ImGuiInputTextCallbackData* data) {
    auto* state = static_cast<text_pattern_constructor_ui::State*>(data->UserData);
    if (state && data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
        state->selectionStartByte = data->SelectionStart;
        state->selectionEndByte = data->SelectionEnd;
    }
    return 0;
}

void DrawManualSelection(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    text_pattern_constructor_ui::State& constructor = helper.constructor;
    UiSettings& ui = UiSettings::Instance();

    if (ImGui::Button(ui.Text(UiText::EditorPatternManualDone))) {
        helper.manualSelectionOpen = false;
        return;
    }
    ImGui::SameLine();
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternManualIntro));
    ImGui::SeparatorText(ui.Text(UiText::EditorPatternManualStepSelect));

    const int previousStart = constructor.selectionStartByte;
    const int previousEnd = constructor.selectionEndByte;
    ImGui::InputTextMultiline(
        "##binder_pattern_manual_sample",
        constructor.preparedSample.data(),
        constructor.preparedSample.capacity() + 1,
        binder_internal::ScaleUi(0.0f, 78.0f),
        ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CallbackAlways,
        TrackManualSelection,
        &constructor);
    if (previousStart != constructor.selectionStartByte
        || previousEnd != constructor.selectionEndByte) {
        text_pattern_constructor_ui::RefreshSelection(constructor);
    }

    const bool hasSelection = constructor.selectionResult.error.empty()
        && !constructor.selectionResult.source.empty()
        && !constructor.selectionResult.suggestions.empty();
    if (hasSelection) {
        ImGui::TextDisabled(
            "%s: %s",
            ui.Text(UiText::EditorPatternSelectedLabel),
            constructor.selectionResult.source.c_str());
    } else {
        ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternSelectionRequired));
    }

    if (!hasSelection) {
        ImGui::SeparatorText(ui.Text(UiText::EditorPatternAddedParts));
        DrawVariableParts(editor, false);
        return;
    }

    ImGui::SeparatorText(ui.Text(UiText::EditorPatternManualStepAction));
    const std::size_t suggestionCount = constructor.selectionResult.suggestions.size();
    const bool customPattern = constructor.selectedSuggestionIndex == static_cast<int>(suggestionCount);
    const text_pattern_builder::Suggestion* selected = nullptr;
    if (hasSelection && !customPattern) {
        constructor.selectedSuggestionIndex = std::clamp(
            constructor.selectedSuggestionIndex,
            0,
            static_cast<int>(suggestionCount) - 1);
        selected = &constructor.selectionResult.suggestions[
            static_cast<std::size_t>(constructor.selectedSuggestionIndex)];
    }
    const char* selectedLabel = customPattern
        ? ui.Text(UiText::EditorPatternCaptureCustom)
        : selected ? TokenLabel(selected->kind) : ui.Text(UiText::EditorPatternTypeLabel);
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternTypeLabel));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##binder_pattern_part_type", selectedLabel)) {
        for (std::size_t index = 0; index < suggestionCount; ++index) {
            const text_pattern_builder::Suggestion& suggestion = constructor.selectionResult.suggestions[index];
            const bool isSelected = constructor.selectedSuggestionIndex == static_cast<int>(index);
            if (ImGui::Selectable(TokenLabel(suggestion.kind), isSelected)) {
                constructor.selectedSuggestionIndex = static_cast<int>(index);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        if (ImGui::Selectable(
                ui.Text(UiText::EditorPatternCaptureCustom),
                customPattern)) {
            constructor.selectedSuggestionIndex = static_cast<int>(suggestionCount);
        }
        ImGui::EndCombo();
    }

    std::string_view selectedPattern;
    text_pattern_builder::TokenKind selectedKind = text_pattern_builder::TokenKind::LineText;
    if (customPattern) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        binder_internal::InputTextWithHintString(
            "##binder_pattern_manual_custom",
            ui.Text(UiText::EditorPatternCaptureCustomPattern),
            helper.captureCustomPattern,
            ImGuiInputTextFlags_AutoSelectAll,
            256);
        selectedPattern = helper.captureCustomPattern;
    } else if (selected) {
        selectedPattern = selected->pattern;
        selectedKind = selected->kind;
        ImGui::TextDisabled("%s", TokenHelp(selected->kind));
    }

    bool customPatternValid = !customPattern;
    if (customPattern && hasSelection && !selectedPattern.empty()) {
        if (!helper.manualCustomValidationReady
            || helper.manualCustomValidationPattern != selectedPattern
            || helper.manualCustomValidationSource != constructor.selectionResult.source) {
            helper.manualCustomValidationReady = true;
            helper.manualCustomValidationPattern = selectedPattern;
            helper.manualCustomValidationSource = constructor.selectionResult.source;
            const std::string wrapped = "\\A(?:" + std::string(selectedPattern) + ")\\z";
            text_pattern::CompileResult compiled = text_pattern::Compile(wrapped, false);
            helper.manualCustomPatternValid = compiled.program
                && compiled.program->Match(constructor.selectionResult.source).status
                    == text_pattern::MatchStatus::Match;
        }
        customPatternValid = helper.manualCustomPatternValid;
        if (!customPatternValid) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                "%s",
                ui.Text(UiText::TextPatternSelectionInvalid));
        }
    }

    ImGui::Checkbox(ui.Text(UiText::EditorPatternSaveValue), &helper.saveSelectionValue);
    const bool captureNameValid = IsValidCaptureName(helper.captureGroupName);
    if (helper.saveSelectionValue) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternVariableName));
        ImGui::SetNextItemWidth(binder_internal::ScaleUi(260.0f));
        binder_internal::InputTextWithHintString(
            "##binder_pattern_variable_name",
            ui.Text(UiText::EditorPatternCaptureNameHint),
            helper.captureGroupName,
            ImGuiInputTextFlags_AutoSelectAll,
            64);
        if (!captureNameValid) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                "%s",
                ui.Text(UiText::EditorPatternCaptureNameInvalid));
        } else {
            const bool currentCaptureApplied = CurrentPatternHasCaptureName(editor, helper.captureGroupName);
            std::string_view copyCaptureName = helper.captureGroupName;
            if (!currentCaptureApplied) {
                copyCaptureName = helper.lastCaptureGroupName;
            }
            const bool captureApplied = currentCaptureApplied
                || (IsValidCaptureName(copyCaptureName)
                    && CurrentPatternHasCaptureName(editor, copyCaptureName));
            const std::string variable = "[chatwordsex("
                + std::string(captureApplied ? copyCaptureName : std::string_view(helper.captureGroupName))
                + ")]";
            ImGui::TextDisabled(
                "%s: %s",
                ui.Text(UiText::EditorPatternVariableUsage),
                variable.c_str());
            ImGui::SameLine();
            ImGui::BeginDisabled(!captureApplied);
            if (ImGui::SmallButton(ui.Text(UiText::UnwantedCopy))) {
                ImGui::SetClipboardText(variable.c_str());
            }
            ImGui::EndDisabled();
        }
    }

    const bool canAdd = hasSelection
        && !selectedPattern.empty()
        && customPatternValid
        && (!helper.saveSelectionValue || captureNameValid);
    ImGui::BeginDisabled(!canAdd);
    if (ImGui::Button(
            ui.Text(UiText::EditorPatternAddPart),
            binder_internal::ScaleUi(220.0f, 0.0f))) {
        const std::string captureName = helper.saveSelectionValue
            ? helper.captureGroupName
            : std::string{};
        text_pattern_constructor_ui::AddReplacement(
            constructor,
            std::string(selectedPattern),
            captureName,
            selectedKind);
        if (!captureName.empty()) {
            helper.lastCaptureGroupName = captureName;
            helper.captureGroupName = text_pattern_constructor_ui::NextAvailableCaptureName(constructor);
        }
        SyncWorkingTriggerFromParts(editor);
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText(ui.Text(UiText::EditorPatternAddedParts));
    DrawVariableParts(editor, false);
}

std::string BuildHumanPreview(const TextPatternHelperState& helper) {
    const text_pattern_constructor_ui::State& constructor = helper.constructor;
    std::vector<std::size_t> ordered;
    ordered.reserve(constructor.selectedParts.size());
    for (std::size_t index = 0; index < constructor.selectedParts.size(); ++index) {
        if (constructor.selectedParts[index].enabled) {
            ordered.push_back(index);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [&](std::size_t left, std::size_t right) {
        return constructor.selectedParts[left].replacement.offset
            < constructor.selectedParts[right].replacement.offset;
    });

    std::string preview;
    std::size_t cursor = 0;
    for (const std::size_t index : ordered) {
        const text_pattern_constructor_ui::SelectedPart& part = constructor.selectedParts[index];
        const text_pattern_builder::Replacement& replacement = part.replacement;
        if (replacement.offset < cursor
            || replacement.offset > constructor.preparedSample.size()
            || replacement.length > constructor.preparedSample.size() - replacement.offset) {
            continue;
        }
        preview.append(constructor.preparedSample, cursor, replacement.offset - cursor);
        preview.push_back('<');
        preview += TokenLabel(part.kind);
        if (!part.captureName.empty()) {
            preview += " -> ";
            preview += part.captureName;
        }
        preview.push_back('>');
        cursor = replacement.offset + replacement.length;
    }
    preview.append(constructor.preparedSample, cursor, std::string::npos);
    return preview;
}

void DrawHumanPreview(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    RefreshValidation(editor);

    if (ImGui::BeginChild(
            "##binder_pattern_human_preview",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        if (!helper.validationError.empty()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.40f, 0.36f, 1.0f),
                "%s",
                ui.Text(UiText::EditorPatternInvalid));
        } else if (helper.validationMatched) {
            ImGui::TextColored(
                ImVec4(0.42f, 0.84f, 0.55f, 1.0f),
                "%s",
                ui.Text(UiText::EditorPatternExampleMatches));
        } else {
            ImGui::TextColored(
                ImVec4(0.92f, 0.62f, 0.38f, 1.0f),
                "%s",
                ui.Text(UiText::EditorPatternNoMatchDetailed));
        }
        if (helper.workingTriggerText == helper.constructor.pattern) {
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternPreviewHint));
            const std::string preview = BuildHumanPreview(helper);
            ImGui::TextWrapped("%s", preview.c_str());
        } else {
            ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCustomPreview));
            ImGui::TextWrapped("%s", helper.validationNormalizedSample.c_str());
        }
        if (!helper.validationMatched && !helper.constructor.pattern.empty()) {
            if (ImGui::Button(ui.Text(UiText::EditorPatternRestoreRecommended))) {
                SetTriggerText(editor, helper.constructor.pattern);
                RefreshValidation(editor);
            }
        }
    }
    ImGui::EndChild();
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

void DrawCaptureResults(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();

    if (!helper.validationProgram
        || helper.validationProgram->CaptureCount() == 0
        || !helper.validationRegexMatched
        || !helper.validationError.empty()) {
        return;
    }
    if (!ImGui::CollapsingHeader(ui.Text(UiText::EditorPatternCaptureResults))) {
        return;
    }
    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternCaptureResultsHint));

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
            5,
            flags,
            ImVec2(0.0f, binder_internal::ScaleUi(300.0f)))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceCategory), ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(105.0f));
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceExpression), ImGuiTableColumnFlags_WidthStretch, 0.85f);
    ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceDescription), ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("##copy", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(92.0f));
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
        ImGui::TableSetColumnIndex(2);
        ImGui::TextWrapped("%s", ui.Text(item.description));
        ImGui::TableSetColumnIndex(3);
        if (ImGui::Button(ui.Text(UiText::UnwantedCopy), ImVec2(-FLT_MIN, 0.0f))) {
            ImGui::SetClipboardText(item.expression);
        }
        ImGui::TableSetColumnIndex(4);
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

void RefreshAlternateValidation(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    RefreshValidation(editor);
    if (helper.alternateValidationReady
        && helper.alternateValidationPattern == helper.workingTriggerText
        && helper.alternateValidationSample == helper.alternateSample) {
        return;
    }

    helper.alternateValidationReady = true;
    helper.alternateValidationPattern = helper.workingTriggerText;
    helper.alternateValidationSample = helper.alternateSample;
    helper.alternateValidationError.clear();
    helper.alternateValidationMatched = false;
    if (helper.alternateSample.empty()
        || !helper.validationProgram
        || !helper.validationError.empty()) {
        return;
    }

    const PreparedSample prepared = PrepareSample(helper.alternateSample);
    const text_pattern::MatchResult match = helper.validationProgram->Match(prepared.normalized);
    if (match.status == text_pattern::MatchStatus::Match) {
        helper.alternateValidationMatched = true;
    } else if (match.status != text_pattern::MatchStatus::NoMatch) {
        helper.alternateValidationError = UiSettings::Instance().Text(UiText::EditorPatternTestStopped);
    }
}

void DrawAdvancedSettings(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    if (!ImGui::CollapsingHeader(ui.Text(UiText::EditorPatternAdvancedTitle))) {
        return;
    }

    ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternAdvancedForExperts));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (DrawBoundTriggerInput(
            editor,
            helper.workingTriggerText,
            "##binder_pattern_advanced_value",
            ui.Text(UiText::EditorTriggerExample),
            true)) {
        helper.alternateValidationReady = false;
    }
    RefreshValidation(editor);
    if (!helper.validationError.empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.40f, 0.36f, 1.0f),
            "%s",
            helper.validationError.c_str());
    }
    if (!helper.validationWarning.empty()) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
            "%s",
            helper.validationWarning.c_str());
    }

    DrawVariants(editor);
    ImGui::SeparatorText(ui.Text(UiText::UnwantedGeneralizations));
    DrawOptions(editor);

    if (ImGui::CollapsingHeader(ui.Text(UiText::EditorPatternAlternateTest))) {
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternAlternateHint));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (binder_internal::InputTextWithHintString(
                "##binder_pattern_alternate_sample",
                ui.Text(UiText::EditorPatternMessageHint),
                helper.alternateSample,
                ImGuiInputTextFlags_AutoSelectAll,
                1024)) {
            helper.alternateValidationReady = false;
        }
        RefreshAlternateValidation(editor);
        if (!helper.alternateValidationError.empty()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.40f, 0.36f, 1.0f),
                "%s",
                helper.alternateValidationError.c_str());
        } else if (!helper.alternateSample.empty()) {
            ImGui::TextColored(
                helper.alternateValidationMatched
                    ? ImVec4(0.42f, 0.84f, 0.55f, 1.0f)
                    : ImVec4(0.92f, 0.62f, 0.38f, 1.0f),
                "%s",
                ui.Text(helper.alternateValidationMatched
                    ? UiText::EditorPatternAlternateMatches
                    : UiText::EditorPatternAlternateNoMatch));
        }
    }

    if (ImGui::Button(ui.Text(helper.referenceOpen
            ? UiText::EditorPatternReferenceClose
            : UiText::EditorPatternReferenceOpen))) {
        helper.referenceOpen = !helper.referenceOpen;
    }
    if (helper.referenceOpen) {
        DrawReferencePanel(editor);
    }

    if (ImGui::CollapsingHeader(ui.Text(UiText::EditorPatternCaptureAdvanced))) {
        DrawAdvancedCaptureBuilder(editor);
    }
    DrawCaptureResults(editor);
}

void BeginPatternHelper(State& editor) {
    TextPatternHelperState fresh;
    fresh.transactionActive = true;
    fresh.originalTriggerText = editor.draft.textTrigger.text;
    fresh.originalTriggerPattern = editor.draft.textTrigger.pattern;
    fresh.workingTriggerText = fresh.originalTriggerText;
    if (!fresh.originalTriggerPattern && !fresh.originalTriggerText.empty()) {
        fresh.sample = fresh.originalTriggerText;
    }
    editor.textPatternHelper = std::move(fresh);
    if (!editor.textPatternHelper.sample.empty()) {
        Regenerate(editor);
    }
}

void CancelPatternHelper(State& editor) {
    editor.textPatternHelper.transactionActive = false;
}

bool CanApplyWorkingTrigger(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    RefreshValidation(editor);
    return helper.transactionActive
        && !helper.validationNormalizedSample.empty()
        && !helper.workingTriggerText.empty()
        && helper.validationProgram
        && helper.validationError.empty()
        && helper.validationMatched
        && !helper.validationMatchesEmpty
        && (!helper.saveSelectionValue || IsValidCaptureName(helper.captureGroupName));
}

void ApplyWorkingTrigger(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    if (!CanApplyWorkingTrigger(editor)) {
        return;
    }
    editor.draft.textTrigger.text = helper.workingTriggerText;
    editor.draft.textTrigger.pattern = true;
    editor.draft.textTrigger.InvalidateRuntimeCache();
    helper.transactionActive = false;
}

void DrawMessageSection(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    ImGui::SeparatorText(ui.Text(UiText::EditorPatternStepMessage));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (binder_internal::InputTextWithHintString(
            "##binder_pattern_sample",
            ui.Text(UiText::EditorPatternMessageHint),
            helper.sample,
            ImGuiInputTextFlags_AutoSelectAll,
            1024)) {
        Regenerate(editor);
    }
    if (ImGui::Button(ui.Text(UiText::MessageHistoryOpen))) {
        rak_message_history_ui::RequestOpen(helper.messageHistoryPicker);
    }
    if (std::optional<std::string> selectedMessage = rak_message_history_ui::DrawPicker(
            editor.sampRakHooks,
            helper.messageHistoryPicker,
            "binder_message_history",
            false)) {
        helper.sample = std::move(*selectedMessage);
        Regenerate(editor);
    }
    RefreshValidation(editor);
    ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternNormalizationHint));
    if (helper.validationTimestampRemoved) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", ui.Text(UiText::EditorPatternTimestampRemoved));
    }
    if (helper.validationColorsRemoved) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", ui.Text(UiText::EditorPatternColorsRemoved));
    }
}

void DrawMainPatternFlow(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    DrawMessageSection(editor);
    if (helper.validationNormalizedSample.empty()) {
        return;
    }

    ImGui::SeparatorText(ui.Text(UiText::EditorPatternStepVariableParts));
    DrawVariableParts(editor);
    ImGui::SeparatorText(ui.Text(UiText::EditorPatternStepPreview));
    DrawHumanPreview(editor);
    DrawAdvancedSettings(editor);
}

void DrawFooterStatus(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    RefreshValidation(editor);

    ImVec4 color(0.62f, 0.68f, 0.76f, 1.0f);
    UiText text = UiText::EditorPatternNeedMessage;
    if (!helper.validationNormalizedSample.empty()) {
        if (!helper.validationError.empty()
            || helper.validationMatchesEmpty
            || helper.workingTriggerText.empty()) {
            color = ImVec4(1.0f, 0.40f, 0.36f, 1.0f);
            text = UiText::EditorPatternInvalid;
        } else if (!helper.validationMatched) {
            color = ImVec4(1.0f, 0.40f, 0.36f, 1.0f);
            text = UiText::EditorPatternNoMatchDetailed;
        } else if (!helper.validationWarning.empty() || !helper.builderWarning.empty()) {
            color = ImVec4(1.0f, 0.72f, 0.30f, 1.0f);
            text = UiText::EditorPatternWarning;
        } else {
            color = ImVec4(0.42f, 0.84f, 0.55f, 1.0f);
            text = UiText::EditorPatternReady;
        }
    }
    ImGui::TextColored(color, "%s", ui.Text(text));
}

} // namespace

bool DrawTextTriggerInput(State& editor, const char* label, const char* hint) {
    return DrawBoundTriggerInput(editor, editor.draft.textTrigger.text, label, hint, false);
}

void DrawTextPatternHelperPopup(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::EditorPatternHelperTitle)) + "###" + kPatternHelperPopupId;
    if (helper.popupPending) {
        helper.popupPending = false;
        BeginPatternHelper(editor);
        ImGui::OpenPopup(title.c_str());
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 helperMaximum(viewport->WorkSize.x * 0.94f, viewport->WorkSize.y * 0.94f);
    ImGui::SetNextWindowSizeConstraints(
        FitModalMinimum(binder_internal::ScaleUi(520.0f, 400.0f), helperMaximum),
        helperMaximum);
    ImGui::SetNextWindowSize(
        ImVec2(std::min(binder_internal::ScaleUi(820.0f), viewport->WorkSize.x * 0.90f),
            std::min(binder_internal::ScaleUi(600.0f), viewport->WorkSize.y * 0.88f)),
        ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
    const bool ctrlEnterPressed = ImGui::IsKeyPressed(ImGuiKey_Enter)
        && (ImGui::GetIO().KeyMods & ImGuiMod_Ctrl) != 0;
    const float footerHeight = ImGui::GetFrameHeightWithSpacing()
        + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("##binder_pattern_helper_body", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None)) {
        ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternIntro));
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternIntroTechnical));
        if (helper.manualSelectionOpen) {
            DrawManualSelection(editor);
        } else {
            DrawMainPatternFlow(editor);
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    const bool canApply = CanApplyWorkingTrigger(editor);
    bool cancelRequested = !open || escapePressed;
    bool applyRequested = ctrlEnterPressed && canApply;
    if (ImGui::BeginTable("##binder_pattern_helper_footer", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("actions", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        DrawFooterStatus(editor);
        ImGui::TableSetColumnIndex(1);
        if (ImGui::Button(
                ui.Text(UiText::EditorPatternCancel),
                binder_internal::ScaleUi(110.0f, 0.0f))) {
            cancelRequested = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!canApply);
        if (ImGui::Button(
                ui.Text(UiText::EditorPatternApply),
                binder_internal::ScaleUi(120.0f, 0.0f))) {
            applyRequested = true;
        }
        ImGui::EndDisabled();
        ImGui::EndTable();
    }

    if (applyRequested) {
        ApplyWorkingTrigger(editor);
        ImGui::CloseCurrentPopup();
    } else if (cancelRequested) {
        CancelPatternHelper(editor);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace binder_editor
