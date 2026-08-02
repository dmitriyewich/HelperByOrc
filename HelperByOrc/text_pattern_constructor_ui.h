#pragma once

#include "text_pattern_builder.h"

#include <string>
#include <string_view>
#include <vector>

namespace text_pattern_constructor_ui {

enum class DrawMode {
    Pattern,
    Capture,
};

struct DrawResult {
    bool applied = false;
    std::string pattern{};
};

struct SelectedPart {
    text_pattern_builder::Replacement replacement{};
    std::string captureName{};
};

struct State {
    std::string preparedSample{};
    int selectionStartByte = -1;
    int selectionEndByte = -1;
    int selectedSuggestionIndex = -1;
    text_pattern_builder::SelectionResult selectionResult{};
    std::vector<SelectedPart> selectedParts{};
    std::string pattern{};
    std::string error{};
};

void SetPreparedSample(State& state, std::string sample);
void RefreshSelection(State& state);
void AddReplacement(State& state, std::string pattern, std::string captureName = {});
DrawResult DrawInline(
    State& state,
    const char* id,
    DrawMode mode = DrawMode::Pattern,
    std::string_view captureName = {});

} // namespace text_pattern_constructor_ui
