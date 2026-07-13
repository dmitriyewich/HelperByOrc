#pragma once

namespace ui_frame_timing {

void BeginFrame();
void MarkExternalWait();
bool HasExternalWait();

} // namespace ui_frame_timing
