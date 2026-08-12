#pragma once

#include "rak_message_history.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class SampRakHooks;

namespace rak_message_history_ui {

struct PickerState {
    bool openRequested = false;
    bool scrollToBottom = false;
    int titleLanguage = -1;
    std::uint64_t snapshotRevision = 0;
    std::string title;
    std::vector<IncomingMessageHistoryEntry> entries;
};

void RequestOpen(PickerState& state);

std::optional<std::string> DrawPicker(
    SampRakHooks* hooks,
    PickerState& state,
    const char* popupId,
    bool includeChatBubbles = true);

} // namespace rak_message_history_ui
