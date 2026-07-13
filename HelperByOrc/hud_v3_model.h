#pragma once

#include "json_utils.h"

#include <string>

namespace hud_v3 {

struct EditorState {
    std::string activePanel = "widgets";
    float panelHeight = 250.0f;
    bool panelCollapsed = false;
    std::string propertiesCategory = "content";
    bool snapEnabled = true;
    bool guidesEnabled = true;
    float gridSize = 8.0f;
    float canvasHeight = 360.0f;
    bool canvasAutoHeight = true;
};

jsonutil::JsonObject SerializeEditorState(const EditorState& state);
EditorState DeserializeEditorState(const jsonutil::JsonObject* object, const EditorState& fallback = {});

} // namespace hud_v3
