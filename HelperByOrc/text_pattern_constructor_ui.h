#pragma once

#include "text_pattern_builder.h"

#include <cstddef>
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
    std::string addedCaptureName{};
    std::string nextCaptureName{};
};

struct SelectedPart {
    text_pattern_builder::Replacement replacement{};
    text_pattern_builder::TokenKind kind = text_pattern_builder::TokenKind::Literal;
    std::string captureName{};
    bool automatic = false;
    bool enabled = true;
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
void SetAutomaticParts(State& state, std::span<const text_pattern_builder::Token> tokens);
void RefreshSelection(State& state);
void AddReplacement(
    State& state,
    std::string pattern,
    std::string captureName = {},
    text_pattern_builder::TokenKind kind = text_pattern_builder::TokenKind::Literal);
void SetPartEnabled(State& state, std::size_t index, bool enabled);
void RemovePart(State& state, std::size_t index);
std::string NextAvailableCaptureName(const State& state);
DrawResult DrawInline(
    State& state,
    const char* id,
    DrawMode mode = DrawMode::Pattern,
    std::string_view captureName = {});

} // namespace text_pattern_constructor_ui
