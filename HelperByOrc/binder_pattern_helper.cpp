#include "binder_editor.h"

#include "binder_module_impl.h"
#include "binder_tag_selector.h"
#include "text_pattern_input.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <utility>

namespace binder_editor {
namespace {

constexpr char kPatternHelperPopupId[] = "binder_text_pattern_helper";
constexpr char kPatternReferencePopupId[] = "binder_text_pattern_reference";

struct PreparedSample {
    std::string normalized;
    bool timestampRemoved = false;
};

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

const char* TokenLabel(text_pattern_builder::TokenKind kind) {
    UiSettings& ui = UiSettings::Instance();
    switch (kind) {
    case text_pattern_builder::TokenKind::PlayerId: return ui.Text(UiText::UnwantedTokenPlayerId);
    case text_pattern_builder::TokenKind::BracketPrefix: return ui.Text(UiText::UnwantedTokenBracketPrefix);
    case text_pattern_builder::TokenKind::Nickname: return ui.Text(UiText::UnwantedTokenNickname);
    case text_pattern_builder::TokenKind::Integer: return ui.Text(UiText::UnwantedTokenInteger);
    case text_pattern_builder::TokenKind::Decimal: return ui.Text(UiText::UnwantedTokenDecimal);
    case text_pattern_builder::TokenKind::Percentage: return ui.Text(UiText::UnwantedTokenPercentage);
    case text_pattern_builder::TokenKind::CompactAmount: return ui.Text(UiText::UnwantedTokenCompactAmount);
    case text_pattern_builder::TokenKind::Money: return ui.Text(UiText::UnwantedTokenMoney);
    case text_pattern_builder::TokenKind::Clock: return ui.Text(UiText::UnwantedTokenClock);
    case text_pattern_builder::TokenKind::Duration: return ui.Text(UiText::UnwantedTokenDuration);
    case text_pattern_builder::TokenKind::Domain: return ui.Text(UiText::UnwantedTokenDomain);
    case text_pattern_builder::TokenKind::Color:
    default:
        return "?";
    }
}

const char* TokenHelp(text_pattern_builder::TokenKind kind) {
    UiSettings& ui = UiSettings::Instance();
    switch (kind) {
    case text_pattern_builder::TokenKind::PlayerId: return ui.Text(UiText::UnwantedTokenPlayerIdHelp);
    case text_pattern_builder::TokenKind::BracketPrefix: return ui.Text(UiText::UnwantedTokenBracketPrefixHelp);
    case text_pattern_builder::TokenKind::Nickname: return ui.Text(UiText::UnwantedTokenNicknameHelp);
    case text_pattern_builder::TokenKind::Integer: return ui.Text(UiText::UnwantedTokenIntegerHelp);
    case text_pattern_builder::TokenKind::Decimal: return ui.Text(UiText::UnwantedTokenDecimalHelp);
    case text_pattern_builder::TokenKind::Percentage: return ui.Text(UiText::UnwantedTokenPercentageHelp);
    case text_pattern_builder::TokenKind::CompactAmount: return ui.Text(UiText::UnwantedTokenCompactAmountHelp);
    case text_pattern_builder::TokenKind::Money: return ui.Text(UiText::UnwantedTokenMoneyHelp);
    case text_pattern_builder::TokenKind::Clock: return ui.Text(UiText::UnwantedTokenClockHelp);
    case text_pattern_builder::TokenKind::Duration: return ui.Text(UiText::UnwantedTokenDurationHelp);
    case text_pattern_builder::TokenKind::Domain: return ui.Text(UiText::UnwantedTokenDomainHelp);
    case text_pattern_builder::TokenKind::Color:
    default:
        return "";
    }
}

void Regenerate(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    helper.options.colors = false;
    const std::string normalized = PrepareSample(helper.sample).normalized;
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
    helper.validationNormalizedSample = PrepareSample(helper.sample).normalized;
    helper.validationError.clear();
    helper.validationWarning.clear();
    helper.validationMatched = false;
    helper.validationTested = helper.testRequested || !helper.sample.empty();

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

    const text_pattern::MatchResult match = compiled.program->Match(helper.validationNormalizedSample);
    if (match.status == text_pattern::MatchStatus::Match) {
        helper.validationMatched = true;
    } else if (match.status == text_pattern::MatchStatus::NoMatch) {
        helper.validationMatched = helper.validationNormalizedSample == normalizedPattern;
    } else {
        helper.validationError = ui.Text(UiText::EditorPatternTestStopped);
    }
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
    if (helper.recommended.empty() && helper.exact.empty() && helper.contains.empty()) {
        return;
    }

    ImGui::Text("%s", ui.Text(UiText::UnwantedRegexVariants));
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
    drawVariant(UiText::UnwantedHelperGeneralized, helper.recommended, helper.recommendedValid);
    drawVariant(UiText::UnwantedHelperExact, helper.exact, helper.exactValid);
    drawVariant(UiText::UnwantedHelperContains, helper.contains, helper.containsValid);
    ImGui::EndTable();
}

void DrawReferencePopup(State& editor) {
    TextPatternHelperState& helper = editor.textPatternHelper;
    UiSettings& ui = UiSettings::Instance();
    const std::string title = std::string(ui.Text(UiText::UnwantedRegexReference)) + "###" + kPatternReferencePopupId;
    if (helper.referencePending) {
        ImGui::OpenPopup(title.c_str());
        helper.referencePending = false;
    }
    if (!helper.referenceOpen) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(
        binder_internal::ScaleUi(560.0f, 380.0f),
        ImVec2(viewport->WorkSize.x * 0.94f, viewport->WorkSize.y * 0.94f));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->WorkSize.x * 0.68f, viewport->WorkSize.y * 0.72f),
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        helper.referenceOpen = false;
        helper.referencePending = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextWrapped("%s", ui.Text(UiText::EditorPatternReferenceHint));
    const std::string searchHint = std::string(ui_icons::Search) + " " + ui.Text(UiText::UnwantedRegexReferenceSearch);
    ImGui::SetNextItemWidth(-FLT_MIN);
    binder_internal::InputTextWithHintString(
        "##binder_pattern_reference_search",
        searchHint.c_str(),
        helper.referenceSearch,
        ImGuiInputTextFlags_AutoSelectAll,
        256);

    bool anyVisible = false;
    const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_BordersInnerH
        | ImGuiTableFlags_BordersInnerV
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_ScrollY
        | ImGuiTableFlags_NoSavedSettings;
    if (ImGui::BeginTable("##binder_pattern_reference_table", 4, flags, ImVec2(0.0f, -binder_internal::ScaleUi(42.0f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceCategory), ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(105.0f));
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceExpression), ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn(ui.Text(UiText::UnwantedRegexReferenceDescription), ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("##append", ImGuiTableColumnFlags_WidthFixed, binder_internal::ScaleUi(105.0f));
        ImGui::TableHeadersRow();
        for (const text_pattern_ui::ReferenceItem& item : text_pattern_ui::ReferenceItems()) {
            if (item.requiresRawColorCodes) {
                continue;
            }
            const std::string searchable = std::string(item.expression)
                + "\n" + ui.Text(item.category)
                + "\n" + ui.Text(item.description);
            if (!helper.referenceSearch.empty()
                && !binder_tags::ContainsNoCaseUtf8(searchable, helper.referenceSearch)) {
                continue;
            }
            anyVisible = true;
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
                InsertTriggerText(editor, item.expression);
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
    if (ImGui::Button(ui.Text(UiText::Close), binder_internal::ScaleUi(120.0f, 0.0f))) {
        helper.referenceOpen = false;
        helper.referencePending = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
    ImGui::SetNextWindowSizeConstraints(
        binder_internal::ScaleUi(520.0f, 400.0f),
        ImVec2(viewport->WorkSize.x * 0.94f, viewport->WorkSize.y * 0.94f));
    ImGui::SetNextWindowSize(
        ImVec2(std::min(binder_internal::ScaleUi(760.0f), viewport->WorkSize.x * 0.90f),
            std::min(binder_internal::ScaleUi(680.0f), viewport->WorkSize.y * 0.88f)),
        ImGuiCond_Appearing);
    bool open = true;
    if (!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }
    if (!open || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        helper.referenceOpen = false;
        helper.referencePending = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const float footerHeight = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("##binder_pattern_helper_body", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None)) {
        if (ImGui::Checkbox(ui.Text(UiText::EditorPatternEnabled), &editor.draft.textTrigger.pattern)) {
            helper.validationReady = false;
        }
        ImGui::SameLine();
        const std::string referenceLabel = std::string(ui_icons::Book) + " " + ui.Text(UiText::UnwantedRegexReference);
        if (ImGui::Button(referenceLabel.c_str())) {
            helper.referenceOpen = true;
            helper.referencePending = true;
        }
        ImGui::TextDisabled("%s", ui.Text(UiText::EditorPatternCurrent));
        ImGui::SetNextItemWidth(-FLT_MIN);
        DrawBoundTriggerInput(editor, "##binder_pattern_current", ui.Text(UiText::EditorTriggerExample), true);

        RefreshValidation(editor);
        if (!helper.validationError.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.36f, 1.0f), "%s", helper.validationError.c_str());
        } else if (!helper.validationWarning.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s", helper.validationWarning.c_str());
        }

        ImGui::Separator();
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

        const PreparedSample preparedSample = PrepareSample(helper.sample);
        if (preparedSample.timestampRemoved) {
            ImGui::TextDisabled("%s", ui.Text(UiText::TextPatternChatlogTimestampRemoved));
        }
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedNormalizedPreview));
        ImGui::TextWrapped("%s", preparedSample.normalized.c_str());
        ImGui::TextDisabled("%s", ui.Text(UiText::UnwantedGeneralizations));
        DrawOptions(editor);

        if (!helper.tokens.empty()) {
            ImGui::Spacing();
            ImGui::Text("%s", ui.Text(UiText::UnwantedDetectedTokens));
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
        if (!helper.builderWarning.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.30f, 1.0f), "%s", helper.builderWarning.c_str());
        }
        DrawVariants(editor);

        ImGui::Separator();
        if (ImGui::Button(ui.Text(UiText::UnwantedTestAction), binder_internal::ScaleUi(120.0f, 0.0f))) {
            helper.testRequested = true;
            helper.validationReady = false;
        }
        RefreshValidation(editor);
        if (helper.validationTested && helper.validationError.empty()) {
            ImGui::SameLine();
            const ImVec4 color = helper.validationMatched
                ? ImVec4(0.42f, 0.84f, 0.55f, 1.0f)
                : ImVec4(0.92f, 0.62f, 0.38f, 1.0f);
            ImGui::TextColored(
                color,
                "%s",
                ui.Text(helper.validationMatched ? UiText::EditorPatternMatched : UiText::EditorPatternNoMatch));
        }
    }
    ImGui::EndChild();

    if (ImGui::Button(ui.Text(UiText::Close), binder_internal::ScaleUi(120.0f, 0.0f))) {
        helper.referenceOpen = false;
        helper.referencePending = false;
        ImGui::CloseCurrentPopup();
    }
    DrawReferencePopup(editor);
    ImGui::EndPopup();
}

} // namespace binder_editor
