#pragma once

#include "binder_types.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace binder_editor {

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

    bool active = false;
    bool isNew = false;
    int hotkeyIndex = -1;
    int selectedInputIndex = -1;
    int selectedInputButtonIndex = -1;
    int bulkMethod = 2;
    int bulkIntervalMs = 0;
    int pendingTargetIndex = -1;
    std::string selectedButtonsText{};
    std::vector<std::string> inputButtonsBulkDrafts{};
    std::vector<ButtonsBulkPreviewState> inputButtonsBulkPreviews{};
    std::string multiText{};
    std::string scenarioAppendText{};
    int scenarioMessageFocusIndex = -1;
    int scenarioMessageNoSelectFocusIndex = -1;
    Tab activeTab = Tab::Scenario;
    PendingAction pendingAction = PendingAction::None;
    bool tabSelectionPending = false;
    bool focusNamePending = false;
    bool conditionsPopupPending = false;
    bool variablesPopupPending = false;
    bool multiInputPopupPending = false;
    bool confirmationSettingsPopupPending = false;
    bool variablesTabSelectionPending = false;
    bool variablesKeyPickerPopupPending = false;
    bool discardPopupPending = false;
    bool scenarioAppendFocusPending = false;
    int variablesActiveTab = 0;
    int selectedVariableInputIndex = -1;
    int selectedSimpleTagIndex = -1;
    int selectedFunctionTagIndex = -1;
    std::string variablesSearch{};
    std::string variablesKeyPickerSearch{};
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
void DrawInline(State& editor, const ShellActions& actions);

} // namespace binder_editor
