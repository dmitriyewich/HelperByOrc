#include "ui_fonts.h"

#include "debug_log.h"
#include "font_awesome7_data.h"
#include "icon_registry.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ui_fonts {
namespace {

struct FontCandidate {
    const char* path;
    const char* label;
};

struct FontFamilyDefinition {
    FontFamily family;
    const char* id;
    const char* label;
    FontCandidate regular;
    FontCandidate bold;
};

constexpr std::array<FontFamilyDefinition, 4> kFontFamilies = {{
    {
        FontFamily::Tahoma,
        "tahoma",
        "Tahoma",
        {"C:\\Windows\\Fonts\\tahoma.ttf", "Tahoma"},
        {"C:\\Windows\\Fonts\\tahomabd.ttf", "Tahoma Bold"},
    },
    {
        FontFamily::SegoeUi,
        "segoe_ui",
        "Segoe UI",
        {"C:\\Windows\\Fonts\\segoeui.ttf", "Segoe UI"},
        {"C:\\Windows\\Fonts\\segoeuib.ttf", "Segoe UI Bold"},
    },
    {
        FontFamily::Arial,
        "arial",
        "Arial",
        {"C:\\Windows\\Fonts\\arial.ttf", "Arial"},
        {"C:\\Windows\\Fonts\\arialbd.ttf", "Arial Bold"},
    },
    {
        FontFamily::Trebucbd,
        "trebucbd",
        "Trebuchet MS Bold",
        {"C:\\Windows\\Fonts\\trebucbd.ttf", "Trebuchet MS Bold"},
        {"C:\\Windows\\Fonts\\trebucbd.ttf", "Trebuchet MS Bold"},
    },
}};

constexpr ImWchar kExtraTextGlyphs[] = {
    0x2018, // left single quotation mark
    0x2019, // right single quotation mark
    0x201A, // single low-9 quotation mark
    0x201C, // left double quotation mark
    0x201D, // right double quotation mark
    0x201E, // double low-9 quotation mark
    0x2020, // dagger
    0x2021, // double dagger
    0x2022, // bullet
    0x2026, // ellipsis
    0x2030, // per mille sign
    0x2039, // single left-pointing angle quotation mark
    0x203A, // single right-pointing angle quotation mark
    0x20AC, // euro sign
    0x2116, // numero sign
    0x2122, // trade mark sign
};

ImVector<ImWchar> g_textGlyphRanges;
ImFont* g_activeFont = nullptr;
ImFont* g_activeBoldFont = nullptr;
const char* g_activeRegularFontPath = nullptr;
const char* g_activeBoldFontPath = nullptr;
FontFamily g_requestedFamily = FontFamily::Tahoma;
FontFamily g_activeFamily = FontFamily::Tahoma;
float g_activeFontSize = kDefaultFontSize;
bool g_activeIsFallback = false;
bool g_atlasLoaded = false;
bool g_reloadPending = true;
unsigned int g_fontStateVersion = 0;
unsigned int g_loggedFontStateVersion = 0;

std::size_t FamilyArrayIndex(FontFamily family) {
    for (std::size_t index = 0; index < kFontFamilies.size(); ++index) {
        if (kFontFamilies[index].family == family) {
            return index;
        }
    }
    return 0;
}

const FontFamilyDefinition& FamilyDefinition(FontFamily family) {
    return kFontFamilies[FamilyArrayIndex(family)];
}

bool FileExists(const char* path) {
    return path && GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

const ImWchar* TextGlyphRanges(ImFontAtlas& atlas) {
    if (!g_textGlyphRanges.empty()) {
        return g_textGlyphRanges.Data;
    }

    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(atlas.GetGlyphRangesCyrillic());
    for (ImWchar glyph : kExtraTextGlyphs) {
        builder.AddChar(glyph);
    }
    builder.BuildRanges(&g_textGlyphRanges);
    return g_textGlyphRanges.Data;
}

bool MergeFontAwesomeIcons(ImGuiIO& io, ImFont* targetFont) {
    if (!targetFont) {
        return false;
    }

    ImFontConfig iconConfig{};
    iconConfig.MergeMode = true;
    iconConfig.PixelSnapH = true;
    iconConfig.DstFont = targetFont;

    ImFont* solidIcons = io.Fonts->AddFontFromMemoryCompressedTTF(
        FontAwesome7Data::kSolidCompressedData,
        static_cast<int>(FontAwesome7Data::kSolidCompressedSize),
        kDefaultFontSize,
        &iconConfig,
        icon_registry::SolidRanges());
    if (!solidIcons) {
        debuglog::WriteError("[ui][font] failed to merge Font Awesome 7 solid icon font");
        return false;
    }

    ImFont* brandsIcons = io.Fonts->AddFontFromMemoryCompressedTTF(
        FontAwesome7Data::kBrandsCompressedData,
        static_cast<int>(FontAwesome7Data::kBrandsCompressedSize),
        kDefaultFontSize,
        &iconConfig,
        icon_registry::BrandsRanges());
    if (!brandsIcons) {
        debuglog::WriteError("[ui][font] failed to merge Font Awesome 7 brands icon font");
        return false;
    }

    return true;
}

ImFont* LoadBoldRole(
    ImGuiIO& io,
    const FontFamilyDefinition& family,
    const ImWchar* glyphRanges,
    const ImFontConfig& baseConfig,
    ImFont* regularFont,
    const char* regularPath,
    const char** outPath) {
    if (outPath) {
        *outPath = "<regular>";
    }
    if (!family.bold.path || !regularFont) {
        return regularFont;
    }
    if (regularPath && std::strcmp(family.bold.path, regularPath) == 0) {
        if (outPath) {
            *outPath = regularPath;
        }
        return regularFont;
    }
    if (!FileExists(family.bold.path)) {
        return regularFont;
    }

    ImFontConfig boldConfig = baseConfig;
    boldConfig.RasterizerMultiply = 1.02f;

    ImFont* font = io.Fonts->AddFontFromFileTTF(family.bold.path, kDefaultFontSize, &boldConfig, glyphRanges);
    if (!font) {
        return regularFont;
    }
    if (outPath) {
        *outPath = family.bold.path;
    }
    return font;
}

void ResetActiveFontPointers() {
    g_activeFont = nullptr;
    g_activeBoldFont = nullptr;
    g_activeRegularFontPath = nullptr;
    g_activeBoldFontPath = nullptr;
    g_activeIsFallback = false;
}

void ResetAtlasForReload(ImGuiIO& io) {
    ImFontAtlas* atlas = io.Fonts;
    if (!atlas) {
        return;
    }

    atlas->Clear();
    atlas->TexList.clear_delete();
    atlas->TexData = nullptr;
    atlas->TexRef = ImTextureRef();
    atlas->TexIsBuilt = false;
    atlas->RendererHasTextures = (io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0;
    io.FontDefault = nullptr;
    ImGui::GetPlatformIO().Textures.resize(0);
    ResetActiveFontPointers();
}

void ApplyActiveFontToContext() {
    if (ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (g_activeFont) {
        io.FontDefault = g_activeFont;
    }
    ImGui::GetStyle().FontSizeBase = g_activeFontSize;
}

bool TryLoadFamily(ImGuiIO& io, const FontFamilyDefinition& family, ImFontConfig& textConfig, const ImWchar* glyphRanges) {
    if (!FileExists(family.regular.path)) {
        debuglog::WriteError("[ui][font] missing regular font family=%s path=\"%s\"", family.id, family.regular.path);
        return false;
    }

    ImFont* regular = io.Fonts->AddFontFromFileTTF(family.regular.path, kDefaultFontSize, &textConfig, glyphRanges);
    if (!regular) {
        debuglog::WriteError("[ui][font] failed to load regular font family=%s path=\"%s\"", family.id, family.regular.path);
        return false;
    }

    g_activeFont = regular;
    g_activeRegularFontPath = family.regular.path;
    MergeFontAwesomeIcons(io, regular);
    g_activeBoldFont = LoadBoldRole(
        io,
        family,
        glyphRanges,
        textConfig,
        regular,
        family.regular.path,
        &g_activeBoldFontPath);
    g_activeFamily = family.family;
    g_activeIsFallback = false;
    return true;
}

bool LoadRequestedFontFamily(ImGuiIO& io, const char* reason) {
    ResetActiveFontPointers();

    ImFontConfig textConfig{};
    textConfig.OversampleH = 2;
    textConfig.OversampleV = 1;
    textConfig.RasterizerMultiply = 1.08f;

    const ImWchar* glyphRanges = TextGlyphRanges(*io.Fonts);
    const FontFamilyDefinition& requested = FamilyDefinition(g_requestedFamily);
    bool loaded = TryLoadFamily(io, requested, textConfig, glyphRanges);

    if (!loaded && g_requestedFamily != FontFamily::Tahoma) {
        loaded = TryLoadFamily(io, FamilyDefinition(FontFamily::Tahoma), textConfig, glyphRanges);
    }
    if (!loaded) {
        for (const FontFamilyDefinition& family : kFontFamilies) {
            if (family.family == g_requestedFamily || family.family == FontFamily::Tahoma) {
                continue;
            }
            if (TryLoadFamily(io, family, textConfig, glyphRanges)) {
                loaded = true;
                break;
            }
        }
    }

    if (!loaded) {
        ImFontConfig fallbackConfig{};
        fallbackConfig.SizePixels = kDefaultFontSize;
        fallbackConfig.RasterizerMultiply = textConfig.RasterizerMultiply;
        g_activeFont = io.Fonts->AddFontDefault(&fallbackConfig);
        g_activeBoldFont = g_activeFont;
        g_activeRegularFontPath = "<imgui-default>";
        g_activeBoldFontPath = "<regular>";
        g_activeFamily = FontFamily::Tahoma;
        g_activeIsFallback = true;
        MergeFontAwesomeIcons(io, g_activeFont);
        debuglog::WriteError("[ui][font] failed to load selectable Windows fonts, using ImGui default");
    }

    g_atlasLoaded = true;
    g_reloadPending = false;
    ++g_fontStateVersion;
    ApplyActiveFontToContext();

    debuglog::WriteInfo(
        "[ui][font] loaded reason=%s family=%s requested=%s regular=\"%s\" bold=\"%s\" sourceSize=%.1f uiSize=%.0f "
        "rasterMul=%.2f oversample=%dx%d fallback=%d",
        reason ? reason : "unknown",
        FontFamilyId(g_activeFamily),
        FontFamilyId(g_requestedFamily),
        g_activeRegularFontPath ? g_activeRegularFontPath : "<none>",
        g_activeBoldFontPath ? g_activeBoldFontPath : "<regular>",
        kDefaultFontSize,
        g_activeFontSize,
        textConfig.RasterizerMultiply,
        static_cast<int>(textConfig.OversampleH),
        static_cast<int>(textConfig.OversampleV),
        g_activeIsFallback ? 1 : 0);
    return g_activeFont != nullptr;
}

} // namespace

std::size_t FontFamilyCount() {
    return kFontFamilies.size();
}

FontFamily FontFamilyAt(std::size_t index) {
    return index < kFontFamilies.size() ? kFontFamilies[index].family : FontFamily::Tahoma;
}

int FontFamilyIndex(FontFamily family) {
    return static_cast<int>(FamilyArrayIndex(family));
}

FontFamily ParseFontFamily(std::string_view value) {
    for (const FontFamilyDefinition& family : kFontFamilies) {
        if (value == family.id || value == family.label) {
            return family.family;
        }
    }
    return FontFamily::Tahoma;
}

const char* FontFamilyId(FontFamily family) {
    return FamilyDefinition(family).id;
}

const char* FontFamilyLabel(FontFamily family) {
    return FamilyDefinition(family).label;
}

ImFont* LoadDefaultFont(ImGuiIO& io) {
    ResetAtlasForReload(io);
    LoadRequestedFontFamily(io, "initial");
    return g_activeFont;
}

void ApplySettings(FontFamily family, float fontSize) {
    const FontFamily nextFamily = FamilyDefinition(family).family;
    const float nextSize = NormalizeFontSize(fontSize);
    const bool familyChanged = nextFamily != g_requestedFamily;
    const bool sizeChanged = std::abs(g_activeFontSize - nextSize) >= 0.5f;

    g_requestedFamily = nextFamily;
    g_activeFontSize = nextSize;
    if (familyChanged) {
        g_reloadPending = true;
        debuglog::WriteInfo(
            "[ui][font] requested family=%s active=%s reloadPending=1",
            FontFamilyId(g_requestedFamily),
            FontFamilyId(g_activeFamily));
    }

    ApplyActiveFontToContext();

    static FontFamily s_lastLoggedRequestedFamily = FontFamily::Tahoma;
    static FontFamily s_lastLoggedActiveFamily = FontFamily::Tahoma;
    static float s_lastLoggedSize = -1.0f;
    if (sizeChanged || familyChanged || s_lastLoggedRequestedFamily != g_requestedFamily
        || s_lastLoggedActiveFamily != g_activeFamily || std::abs(s_lastLoggedSize - g_activeFontSize) >= 0.5f) {
        if (sizeChanged) {
            ++g_fontStateVersion;
        }
        s_lastLoggedRequestedFamily = g_requestedFamily;
        s_lastLoggedActiveFamily = g_activeFamily;
        s_lastLoggedSize = g_activeFontSize;
        debuglog::WriteInfo(
            "[ui][font] active family=%s requested=%s size=%.0f regular=\"%s\" bold=\"%s\" reloadPending=%d fallback=%d",
            FontFamilyId(g_activeFamily),
            FontFamilyId(g_requestedFamily),
            g_activeFontSize,
            g_activeRegularFontPath ? g_activeRegularFontPath : "<not-loaded>",
            g_activeBoldFontPath ? g_activeBoldFontPath : "<regular>",
            g_reloadPending ? 1 : 0,
            g_activeIsFallback ? 1 : 0);
    }
}

bool HasPendingFontAtlasReload() {
    return g_reloadPending && ImGui::GetCurrentContext() != nullptr;
}

bool ReloadPendingFontAtlas() {
    if (!HasPendingFontAtlasReload()) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ResetAtlasForReload(io);
    return LoadRequestedFontFamily(io, "reload");
}

ImFont* BoldFont() {
    return g_activeBoldFont ? g_activeBoldFont : g_activeFont;
}

void LogAtlasDiagnosticsOnce() {
    if (g_loggedFontStateVersion == g_fontStateVersion || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImFontAtlas* atlas = io.Fonts;
    const ImTextureData* texture = atlas ? atlas->TexData : nullptr;
    if (!texture || texture->Width <= 0 || texture->Height <= 0) {
        return;
    }

    g_loggedFontStateVersion = g_fontStateVersion;
    const ImGuiStyle& style = ImGui::GetStyle();
    debuglog::WriteInfo(
        "[ui][font] atlas family=%s requested=%s size=%.0f sourceSize=%.1f regular=\"%s\" bold=\"%s\" styleScale=%.3f "
        "legacyIoScale=%.3f texture=%dx%d bpp=%d loader=%s fallback=%d",
        FontFamilyId(g_activeFamily),
        FontFamilyId(g_requestedFamily),
        g_activeFontSize,
        kDefaultFontSize,
        g_activeRegularFontPath ? g_activeRegularFontPath : "<imgui-default>",
        g_activeBoldFontPath ? g_activeBoldFontPath : "<regular>",
        style.FontScaleMain,
        io.FontGlobalScale,
        texture->Width,
        texture->Height,
        texture->BytesPerPixel,
        atlas->FontLoaderName ? atlas->FontLoaderName : "<unknown>",
        g_activeIsFallback ? 1 : 0);
}

float NormalizeFontSize(float fontSize) {
    if (!std::isfinite(fontSize)) {
        return kDefaultFontSize;
    }
    return std::clamp(std::round(fontSize), kMinFontSize, kMaxFontSize);
}

float RoundFontSize(float fontSize) {
    return std::max(1.0f, std::round(fontSize));
}

float CurrentBaseFontSize() {
    if (ImGui::GetCurrentContext() != nullptr) {
        const float styleSize = ImGui::GetStyle().FontSizeBase;
        if (styleSize > 0.0f) {
            return styleSize;
        }
    }
    return g_activeFontSize;
}

float FontSizeForScale(float scale) {
    return RoundFontSize(CurrentBaseFontSize() * std::max(0.01f, scale));
}

float SnapPixel(float value) {
    return std::floor(value + 0.5f);
}

ImVec2 SnapPixel(const ImVec2& value) {
    return ImVec2(SnapPixel(value.x), SnapPixel(value.y));
}

ScopedFontSize::ScopedFontSize(float fontSizeBaseUnscaled) {
    if (fontSizeBaseUnscaled <= 0.0f || ImGui::GetCurrentContext() == nullptr) {
        return;
    }

    ImGui::PushFont(nullptr, RoundFontSize(fontSizeBaseUnscaled));
    pushed_ = true;
}

ScopedFontSize::~ScopedFontSize() {
    if (pushed_ && ImGui::GetCurrentContext() != nullptr) {
        ImGui::PopFont();
    }
}

} // namespace ui_fonts
