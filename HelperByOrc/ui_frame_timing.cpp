#include "ui_frame_timing.h"

namespace ui_frame_timing {
namespace {

thread_local bool g_hasExternalWait = false;

} // namespace

void BeginFrame() {
    g_hasExternalWait = false;
}

void MarkExternalWait() {
    g_hasExternalWait = true;
}

bool HasExternalWait() {
    return g_hasExternalWait;
}

} // namespace ui_frame_timing
