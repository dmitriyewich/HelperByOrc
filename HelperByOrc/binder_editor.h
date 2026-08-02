#pragma once

#include "binder_types.h"
#include "icon_picker_ui.h"
#include "text_pattern_builder.h"
#include "text_pattern_constructor_ui.h"
#include "variables_picker_ui.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace binder_editor {

struct TextPatternHelperState {
    TextPatternHelperState() {
        options.colors = false;
    }

    bool popupPending = false;
    int workspaceMode = 0;
    bool testRequested = false;
    bool restoreHelperFocus = false;
    int cursorByte = -1;
    int selectionStartByte = -1;
    int selectionEndByte = -1;
    int outputLanguage = -1;
    int validationLanguage = -1;
    std::string sample{};
    text_pattern_builder::Options options{};
    std::string exact{};
    std::string recommended{};
    std::string contains{};
    std::vector<text_pattern_builder::Token> tokens{};
    text_pattern_constructor_ui::State constructor{};
    std::string builderWarning{};
    bool exactValid = false;
    bool recommendedValid = false;
    bool containsValid = false;
    std::string referenceSearch{};
    int referenceFilterLanguage = -1;
    std::string referenceFilterSearch{};
    std::vector<std::size_t> referenceVisibleItems{};
    bool validationReady = false;
    std::string validationPattern{};
    std::string validationSample{};
    bool validationPatternEnabled = false;
    std::string validationNormalizedSample{};
    std::string validationError{};
    std::string validationWarning{};
    bool validationMatched = false;
    bool validationRegexMatched = false;
    bool validationTested = false;
    bool validationTimestampRemoved = false;
    std::shared_ptr<text_pattern::Program> validationProgram{};
    text_pattern::CaptureSnapshot validationCaptures{};
    std::string captureGroupName{"value"};
    std::string lastCaptureGroupName{};
    std::string captureCustomPattern{R"(\S+)"};
    int capturePreset = 0;
};

struct State {
    enum class Tab {
        Scenario = 0,
        InputFields = 1,
    };

    enum class PendingAction {
        None = 0,
        Close,
        Navigate,
    };

    enum class VariableInsertTarget {
        None = 0,
        ScenarioMessage,
        ScenarioAppend,
    };

    bool active = false;
    bool isNew = false;
    int hotkeyIndex = -1;
    int selectedInputIndex = -1;
    int selectedInputButtonIndex = -1;
    int expandedInputHintIndex = -1;
    int bulkMethod = 2;
    int bulkIntervalMs = 0;
    int pendingTargetIndex = -1;
    std::string selectedButtonsText{};
    std::vector<std::string> inputButtonsBulkDrafts{};
    std::string multiText{};
    std::string scenarioAppendText{};
    int scenarioMessageFocusIndex = -1;
    int scenarioMessageFocusCaretByte = -1;
    int scenarioDragMessageIndex = -1;
    Tab activeTab = Tab::Scenario;
    PendingAction pendingAction = PendingAction::None;
    bool tabSelectionPending = false;
    bool focusNamePending = false;
    bool conditionsPopupPending = false;
    bool variablesPopupPending = false;
    bool multiInputPopupPending = false;
    bool confirmationSettingsPopupPending = false;
    bool variablesKeyPickerPopupPending = false;
    bool discardPopupPending = false;
    bool scenarioAppendFocusPending = false;
    VariableInsertTarget variablesInsertTarget = VariableInsertTarget::None;
    int variablesInsertMessageIndex = -1;
    int variablesInsertCursorByte = -1;
    int variablesInsertSelectionStartByte = -1;
    int variablesInsertSelectionEndByte = -1;
    std::string variablesKeyPickerSearch{};
    variables_picker::State variablesPicker{};
    std::vector<variables_picker::Entry> variablePickerEntries{};
    icon_picker::State iconPicker{};
    TextPatternHelperState textPatternHelper{};
    HotkeyEntry baseline{};
    HotkeyEntry draft{};
};

struct LaunchPanelActions {
    std::function<void()> beginHotkeyCapture;
    std::function<void()> beginConfirmKeyCapture;
    std::function<void()> beginCancelKeyCapture;
};

struct ShellActions {
    std::function<bool()> hasUnsavedChanges;
    std::function<std::pair<int, int>()> editorNeighborIndices;
    std::function<void(State::PendingAction, int)> requestAction;
    std::function<void()> drawScenarioTab;
    std::function<void()> drawInputEditor;
    std::function<void()> saveRequested;
    std::function<void()> drawCapturePopup;
    std::function<void()> drawConditionsPopup;
    std::function<void()> drawVariablesPopup;
    std::function<void()> drawMultiInputPopup;
    std::function<void()> drawDiscardPopup;
    LaunchPanelActions launchActions;
};

void NormalizeDraftForOpen(State& editor);
HotkeyEntry BuildComparableDraft(const State& editor);
void NormalizeDraftForSave(HotkeyEntry& hotkey);

void DrawLaunchPanel(State& editor, const LaunchPanelActions& actions);
void DrawConfirmationSettingsPopup(State& editor, const LaunchPanelActions& actions);
bool DrawTextTriggerInput(State& editor, const char* label, const char* hint);
void DrawTextPatternHelperPopup(State& editor);
void DrawInline(State& editor, const ShellActions& actions);

} // namespace binder_editor
