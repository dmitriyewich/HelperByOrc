#pragma once

#include <imgui.h>

#include <cstddef>
#include <string_view>

namespace ui_fonts {

enum class FontFamily {
    Tahoma = 0,
    SegoeUi,
    Arial,
    Trebucbd,
};

constexpr float kDefaultFontSize = 16.0f;
constexpr float kMinFontSize = 12.0f;
constexpr float kMaxFontSize = 22.0f;

std::size_t FontFamilyCount();
FontFamily FontFamilyAt(std::size_t index);
int FontFamilyIndex(FontFamily family);
FontFamily ParseFontFamily(std::string_view value);
const char* FontFamilyId(FontFamily family);
const char* FontFamilyLabel(FontFamily family);

ImFont* LoadDefaultFont(ImGuiIO& io);
void ApplySettings(FontFamily family, float fontSize);
bool HasPendingFontAtlasReload();
bool ReloadPendingFontAtlas();
ImFont* BoldFont();
void LogAtlasDiagnosticsOnce();

float NormalizeFontSize(float fontSize);
float RoundFontSize(float fontSize);
float CurrentBaseFontSize();
float FontSizeForScale(float scale);
float SnapPixel(float value);
ImVec2 SnapPixel(const ImVec2& value);

class ScopedFontSize {
public:
    explicit ScopedFontSize(float fontSizeBaseUnscaled);
    ~ScopedFontSize();

    ScopedFontSize(const ScopedFontSize&) = delete;
    ScopedFontSize& operator=(const ScopedFontSize&) = delete;

private:
    bool pushed_ = false;
};

} // namespace ui_fonts
