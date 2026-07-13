#include "hud_v3_model.h"

#include <algorithm>

namespace hud_v3 {

jsonutil::JsonObject SerializeEditorState(const EditorState& state) {
    jsonutil::JsonObject object;
    object["active_panel"] = state.activePanel;
    object["panel_height"] = state.panelHeight;
    object["panel_collapsed"] = state.panelCollapsed;
    object["properties_category"] = state.propertiesCategory;
    object["snap_enabled"] = state.snapEnabled;
    object["guides_enabled"] = state.guidesEnabled;
    object["grid_size"] = state.gridSize;
    object["canvas_height"] = state.canvasHeight;
    object["canvas_auto_height"] = state.canvasAutoHeight;
    return object;
}

EditorState DeserializeEditorState(const jsonutil::JsonObject* object, const EditorState& fallback) {
    EditorState state = fallback;
    if (!object) {
        return state;
    }
    state.activePanel = jsonutil::JsonStringOr(object, "active_panel", state.activePanel);
    state.panelHeight = std::clamp(jsonutil::JsonNumberOr(object, "panel_height", state.panelHeight), 150.0f, 620.0f);
    state.panelCollapsed = jsonutil::JsonBoolOr(object, "panel_collapsed", state.panelCollapsed);
    state.propertiesCategory = jsonutil::JsonStringOr(object, "properties_category", state.propertiesCategory);
    state.snapEnabled = jsonutil::JsonBoolOr(object, "snap_enabled", state.snapEnabled);
    state.guidesEnabled = jsonutil::JsonBoolOr(object, "guides_enabled", state.guidesEnabled);
    state.gridSize = std::clamp(jsonutil::JsonNumberOr(object, "grid_size", state.gridSize), 1.0f, 64.0f);
    state.canvasHeight = std::clamp(jsonutil::JsonNumberOr(object, "canvas_height", state.canvasHeight), 220.0f, 900.0f);
    state.canvasAutoHeight = jsonutil::JsonBoolOr(object, "canvas_auto_height", state.canvasAutoHeight);
    return state;
}

} // namespace hud_v3
