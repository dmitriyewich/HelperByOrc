#include "markup_renderer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "debug_log.h"
#include "icon_registry.h"
#include "ui_fonts.h"
#include "ui_settings.h"

#include <d3dx9tex.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

float FontDirectiveScale(const MarkupRenderer::DrawOptions& options) {
    return std::max(0.01f, options.fontDirectiveScale);
}

struct ParsedImage {
    std::string source{};
    int width = 0;
    int height = 0;
    int posX = 0;
    int posY = 0;
    bool hasPosition = false;
};

struct RichSegment {
    std::string text{};
    std::string displayText{};
    ImVec4 color{};
    ImVec4 bgColor{};
    ImVec4 hrColor{};
    bool hasColor = false;
    bool hasBgColor = false;
    bool hasHrColor = false;
    float alpha = 1.0f;
    int fontSize = 0;
    float indent = 0.0f;
    std::string icon{};
    bool isIcon = false;
    bool bullet = false;
    bool sameLine = false;
    bool inlineContinuation = false;
    bool isHr = false;
    bool shadow = false;
    bool outline = false;
    int extraBreaks = 0;
    enum class Align { Left, Center, Right } align = Align::Left;
    enum class Transform { None, Upper, Lower } transform = Transform::None;
    std::optional<ParsedImage> image{};
};

struct ParsedTextCacheKey {
    std::string text;
    std::wstring imageRoot;
    bool wrapText = true;
    int fontDirectiveScale = 100;
    int wrapWidth = 0;
    int uiScale = 100;
    const ImFont* baseFont = nullptr;
    int baseFontSize = 0;
    int itemSpacingY = 0;
};

bool operator==(const ParsedTextCacheKey& left, const ParsedTextCacheKey& right) {
    return left.text == right.text
        && left.imageRoot == right.imageRoot
        && left.wrapText == right.wrapText
        && left.fontDirectiveScale == right.fontDirectiveScale
        && left.wrapWidth == right.wrapWidth
        && left.uiScale == right.uiScale
        && left.baseFont == right.baseFont
        && left.baseFontSize == right.baseFontSize
        && left.itemSpacingY == right.itemSpacingY;
}

int QuantizeLayoutFloat(float value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int>(std::lround(value * 100.0f));
}

struct InlineStyle {
    ImVec4 color{};
    ImVec4 bgColor{};
    bool hasColor = false;
    bool hasBgColor = false;
    float alpha = 1.0f;
    int fontSize = 0;
    bool shadow = false;
    bool outline = false;
};

struct TextureCacheEntry {
    IDirect3DTexture9* texture = nullptr;
    UINT width = 0;
    UINT height = 0;
    bool failed = false;
    bool failureLogged = false;
    bool fileStampKnown = false;
    std::uintmax_t fileSize = 0;
    fs::file_time_type lastWriteTime{};
    std::uint64_t nextProbeAtMs = 0;
    std::uint64_t lastUsedAtMs = 0;
};

struct TextureFileStamp {
    std::uintmax_t size = 0;
    fs::file_time_type lastWriteTime{};
};

constexpr std::uint64_t kTextureProbeIntervalMs = 1000;
constexpr std::size_t kMaxCachedTextures = 256;

std::optional<TextureFileStamp> ReadTextureFileStamp(const fs::path& path, std::error_code& error) {
    error.clear();
    const fs::file_time_type lastWriteTime = fs::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }

    TextureFileStamp stamp;
    stamp.size = fs::file_size(path, error);
    if (error) {
        return std::nullopt;
    }
    stamp.lastWriteTime = lastWriteTime;
    return stamp;
}

std::wstring MultiByteToWide(std::string_view text, UINT codePage, DWORD flags = 0) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            codePage,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required)
        <= 0) {
        return {};
    }
    return result;
}

std::string TrimAscii(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

std::string LowerUtf8(std::string_view text) {
    std::wstring wide = MarkupRenderer::Utf8ToWide(text);
    if (wide.empty()) {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return result;
    }
    CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    return MarkupRenderer::WideToUtf8(wide);
}

std::string UpperUtf8(std::string_view text) {
    std::wstring wide = MarkupRenderer::Utf8ToWide(text);
    if (wide.empty()) {
        std::string result(text);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return result;
    }
    CharUpperBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    return MarkupRenderer::WideToUtf8(wide);
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

float InlineSegmentSpacing() {
    return ImGui::GetStyle().ItemSpacing.x;
}

std::optional<ImVec4> ParseColorHex(std::string_view value) {
    if (value.size() != 6) {
        return std::nullopt;
    }
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    int values[6]{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        values[i] = hex(value[i]);
        if (values[i] < 0) {
            return std::nullopt;
        }
    }
    const int r = values[0] * 16 + values[1];
    const int g = values[2] * 16 + values[3];
    const int b = values[4] * 16 + values[5];
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
}

bool HasDirectiveBoundary(std::string_view text, std::size_t consumed) {
    if (consumed >= text.size()) {
        return true;
    }
    const char ch = text[consumed];
    return std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == '#';
}

void TrimTrailingAsciiWhitespace(std::string& value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
}

void SkipLeadingAsciiWhitespace(std::string_view& text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
}

bool ReadDirectiveInt(
    std::string_view lowered,
    std::size_t prefixLen,
    bool allowNegative,
    std::size_t& consumed,
    int& value) {
    std::string digits;
    std::size_t pos = prefixLen;
    if (allowNegative && pos < lowered.size() && lowered[pos] == '-') {
        digits.push_back('-');
        ++pos;
    }
    while (pos < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[pos])) != 0) {
        digits.push_back(lowered[pos++]);
    }
    if (digits.empty() || digits == "-") {
        return false;
    }
    consumed = pos;
    value = std::atoi(digits.c_str());
    return true;
}

void ApplyStyle(RichSegment& segment, const InlineStyle& style) {
    segment.color = style.color;
    segment.bgColor = style.bgColor;
    segment.hasColor = style.hasColor;
    segment.hasBgColor = style.hasBgColor;
    segment.alpha = style.alpha;
    segment.fontSize = style.fontSize;
    segment.shadow = style.shadow;
    segment.outline = style.outline;
}

void ResetInlineStyle(InlineStyle& style) {
    style.color = {};
    style.bgColor = {};
    style.hasColor = false;
    style.hasBgColor = false;
    style.alpha = 1.0f;
    style.fontSize = 0;
    style.shadow = false;
    style.outline = false;
}

std::vector<std::string> SplitDirectiveArgs(std::string_view raw) {
    std::vector<std::string> args;
    std::string current;
    int depth = 0;
    char quote = '\0';
    for (char ch : raw) {
        if ((ch == '\'' || ch == '"') && quote == '\0') {
            quote = ch;
            current.push_back(ch);
            continue;
        }
        if (quote != '\0') {
            if (ch == quote) {
                quote = '\0';
            }
            current.push_back(ch);
            continue;
        }
        if (ch == '(') {
            ++depth;
            current.push_back(ch);
            continue;
        }
        if (ch == ')') {
            depth = std::max(0, depth - 1);
            current.push_back(ch);
            continue;
        }
        if (ch == ',' && depth == 0) {
            args.push_back(TrimAscii(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    args.push_back(TrimAscii(current));
    return args;
}

std::string Unquote(std::string value) {
    value = TrimAscii(value);
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return value.substr(1, value.size() - 2);
        }
    }
    return value;
}

std::optional<ParsedImage> ParseImageArgs(std::string_view args) {
    const std::vector<std::string> parts = SplitDirectiveArgs(args);
    if (parts.empty()) {
        return std::nullopt;
    }
    ParsedImage image;
    image.source = Unquote(parts.front());
    if (image.source.empty()) {
        return std::nullopt;
    }
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const std::string lowered = LowerUtf8(parts[i]);
        int x = 0;
        int y = 0;
        if (sscanf_s(lowered.c_str(), "size(%d,%d)", &x, &y) == 2 && x > 0 && y > 0) {
            image.width = x;
            image.height = y;
        } else if (sscanf_s(lowered.c_str(), "pos(%d,%d)", &x, &y) == 2) {
            image.posX = x;
            image.posY = y;
            image.hasPosition = true;
        }
    }
    return image;
}

std::optional<ParsedImage> ParseImagePrefix(std::string_view text, std::size_t& consumed) {
    const std::string lowered = LowerUtf8(text);
    if (lowered.rfind("#img(", 0) != 0) {
        return std::nullopt;
    }
    int depth = 0;
    for (std::size_t i = 4; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
            if (depth == 0) {
                if (!HasDirectiveBoundary(text, i + 1)) {
                    return std::nullopt;
                }
                consumed = i + 1;
                return ParseImageArgs(text.substr(5, i - 5));
            }
        }
    }
    return std::nullopt;
}

std::string ResolveMarkupIconGlyph(std::string_view name) {
    if (std::string glyph = icon_registry::ResolveGlyph(name); !glyph.empty()) {
        return glyph;
    }
    return "[" + UpperUtf8(name) + "]";
}

std::optional<std::string> ParseIconFunction(std::string_view text, std::size_t& consumed) {
    const std::string lowered = LowerUtf8(text);
    if (lowered.rfind("#icon(", 0) != 0) {
        return std::nullopt;
    }

    int depth = 0;
    for (std::size_t i = 6; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            if (depth == 0) {
                if (!HasDirectiveBoundary(text, i + 1)) {
                    return std::nullopt;
                }
                consumed = i + 1;
                return ResolveMarkupIconGlyph(std::string_view(text).substr(6, i - 6));
            }
            --depth;
        }
    }
    return std::nullopt;
}

std::optional<ImVec4> ParseInlineColorMarker(std::string_view text) {
    if (text.size() < 8 || text.front() != '{' || text[7] != '}') {
        return std::nullopt;
    }
    return ParseColorHex(text.substr(1, 6));
}

bool IsSupportedFontSize(int size) {
    return size == 12 || size == 14 || size == 16 || size == 18 || size == 30;
}

bool TryConsumeCompactIcon(std::string_view text, std::size_t& consumed, std::string& glyph) {
    const std::string lowered = LowerUtf8(text);
    if (lowered.rfind("#icon", 0) != 0) {
        return false;
    }
    std::size_t pos = 5;
    while (pos < lowered.size()
        && (std::isalnum(static_cast<unsigned char>(lowered[pos])) != 0 || lowered[pos] == '_' || lowered[pos] == '-' || lowered[pos] == ':')) {
        ++pos;
    }
    if (pos <= 5 || !HasDirectiveBoundary(text, pos)) {
        return false;
    }
    consumed = pos;
    glyph = ResolveMarkupIconGlyph(lowered.substr(5, pos - 5));
    return true;
}

bool TryConsumeInlineDirective(
    std::string_view text,
    InlineStyle& style,
    std::size_t& consumed,
    std::string& iconGlyph) {
    consumed = 0;
    iconGlyph.clear();

    if (const std::optional<ImVec4> color = ParseInlineColorMarker(text)) {
        style.color = *color;
        style.hasColor = true;
        consumed = 8;
        return true;
    }

    if (std::optional<std::string> icon = ParseIconFunction(text, consumed)) {
        iconGlyph = std::move(*icon);
        return true;
    }
    if (TryConsumeCompactIcon(text, consumed, iconGlyph)) {
        return true;
    }

    const std::string lowered = LowerUtf8(text);
    int value = 0;
    if (lowered.rfind("#font", 0) == 0) {
        if (ReadDirectiveInt(lowered, 5, false, consumed, value)
            && IsSupportedFontSize(value)
            && HasDirectiveBoundary(text, consumed)) {
            style.fontSize = value;
            return true;
        }
        if (HasDirectiveBoundary(text, 5)) {
            style.fontSize = 0;
            consumed = 5;
            return true;
        }
    } else if (lowered.rfind("#small", 0) == 0 && HasDirectiveBoundary(text, 6)) {
        style.fontSize = 12;
        consumed = 6;
        return true;
    } else if (lowered.rfind("#big", 0) == 0 && HasDirectiveBoundary(text, 4)) {
        style.fontSize = 18;
        consumed = 4;
        return true;
    } else if (lowered.rfind("#color", 0) == 0 && lowered.size() >= 12) {
        if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(lowered).substr(6, 6));
            color && HasDirectiveBoundary(text, 12)) {
            style.color = *color;
            style.hasColor = true;
            consumed = 12;
            return true;
        }
    } else if (lowered.rfind("#bg", 0) == 0) {
        if (lowered.size() >= 9) {
            if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(lowered).substr(3, 6));
                color && HasDirectiveBoundary(text, 9)) {
                style.bgColor = *color;
                style.hasBgColor = true;
                consumed = 9;
                return true;
            }
        }
        if (HasDirectiveBoundary(text, 3)) {
            style.bgColor = {};
            style.hasBgColor = false;
            consumed = 3;
            return true;
        }
    } else if (lowered.rfind("#alpha", 0) == 0) {
        if (ReadDirectiveInt(lowered, 6, false, consumed, value) && HasDirectiveBoundary(text, consumed)) {
            style.alpha = std::clamp(value, 0, 100) / 100.0f;
            return true;
        }
        if (HasDirectiveBoundary(text, 6)) {
            style.alpha = 1.0f;
            consumed = 6;
            return true;
        }
    } else if (lowered.rfind("#reset", 0) == 0 && HasDirectiveBoundary(text, 6)) {
        ResetInlineStyle(style);
        consumed = 6;
        return true;
    } else if (lowered.rfind("#shadow", 0) == 0 && HasDirectiveBoundary(text, 7)) {
        style.shadow = true;
        consumed = 7;
        return true;
    } else if (lowered.rfind("#outline", 0) == 0 && HasDirectiveBoundary(text, 8)) {
        style.outline = true;
        consumed = 8;
        return true;
    }

    return false;
}

bool IsInlineDirective(std::string_view text) {
    InlineStyle style;
    std::size_t consumed = 0;
    std::string iconGlyph;
    return TryConsumeInlineDirective(text, style, consumed, iconGlyph);
}

RichSegment MakeInlineRun(const RichSegment& lineMeta, const InlineStyle& style, bool firstRun) {
    RichSegment run = lineMeta;
    run.text.clear();
    run.icon.clear();
    run.image.reset();
    run.isIcon = false;
    run.isHr = false;
    run.inlineContinuation = !firstRun;
    ApplyStyle(run, style);
    if (!firstRun) {
        run.bullet = false;
        run.sameLine = false;
        run.align = RichSegment::Align::Left;
        run.indent = 0.0f;
        run.extraBreaks = 0;
    }
    return run;
}

void AppendTextRun(
    std::vector<RichSegment>& runs,
    const RichSegment& lineMeta,
    const InlineStyle& style,
    std::string text) {
    if (text.empty()) {
        return;
    }
    if (lineMeta.transform == RichSegment::Transform::Upper) {
        text = UpperUtf8(text);
    } else if (lineMeta.transform == RichSegment::Transform::Lower) {
        text = LowerUtf8(text);
    }

    const bool previousWasIcon = !runs.empty() && runs.back().isIcon;
    RichSegment run = MakeInlineRun(lineMeta, style, runs.empty());
    run.text = std::move(text);
    run.inlineContinuation = !runs.empty() && !previousWasIcon;
    runs.push_back(std::move(run));
}

void AppendIconRun(
    std::vector<RichSegment>& runs,
    const RichSegment& lineMeta,
    const InlineStyle& style,
    std::string glyph) {
    if (!runs.empty() && !runs.back().isIcon && !runs.back().image && !runs.back().isHr) {
        TrimTrailingAsciiWhitespace(runs.back().text);
    }

    RichSegment run = MakeInlineRun(lineMeta, style, runs.empty());
    run.icon = std::move(glyph);
    run.isIcon = true;
    run.inlineContinuation = false;
    runs.push_back(std::move(run));
}

bool TryConsumeLineDirective(std::string_view text, RichSegment& lineMeta, std::size_t& consumed) {
    consumed = 0;
    const std::string lowered = LowerUtf8(text);
    int value = 0;
    if (lowered.rfind("#sameline", 0) == 0 && HasDirectiveBoundary(text, 9)) {
        lineMeta.sameLine = true;
        consumed = 9;
        return true;
    }
    if (lowered.rfind("#right", 0) == 0 && HasDirectiveBoundary(text, 6)) {
        lineMeta.align = RichSegment::Align::Right;
        consumed = 6;
        return true;
    }
    if (lowered.rfind("#center", 0) == 0 && HasDirectiveBoundary(text, 7)) {
        lineMeta.align = RichSegment::Align::Center;
        consumed = 7;
        return true;
    }
    if (lowered.rfind("#left", 0) == 0 && HasDirectiveBoundary(text, 5)) {
        lineMeta.align = RichSegment::Align::Left;
        consumed = 5;
        return true;
    }
    if (lowered.rfind("#bullet", 0) == 0 && HasDirectiveBoundary(text, 7)) {
        lineMeta.bullet = true;
        consumed = 7;
        return true;
    }
    if (lowered.rfind("#upper", 0) == 0 && HasDirectiveBoundary(text, 6)) {
        lineMeta.transform = RichSegment::Transform::Upper;
        consumed = 6;
        return true;
    }
    if (lowered.rfind("#lower", 0) == 0 && HasDirectiveBoundary(text, 6)) {
        lineMeta.transform = RichSegment::Transform::Lower;
        consumed = 6;
        return true;
    }
    if (lowered.rfind("#indent", 0) == 0
        && ReadDirectiveInt(lowered, 7, true, consumed, value)
        && HasDirectiveBoundary(text, consumed)) {
        lineMeta.indent += ScaleUi(static_cast<float>(value));
        return true;
    }
    if (lowered.rfind("#pad", 0) == 0
        && ReadDirectiveInt(lowered, 4, true, consumed, value)
        && HasDirectiveBoundary(text, consumed)) {
        lineMeta.indent += ScaleUi(static_cast<float>(value));
        return true;
    }
    if (lowered.rfind("#tab", 0) == 0) {
        if (!ReadDirectiveInt(lowered, 4, false, consumed, value)) {
            consumed = 4;
            value = 1;
        }
        if (HasDirectiveBoundary(text, consumed)) {
            lineMeta.indent += ScaleUi(static_cast<float>(std::max(1, value) * 32));
            return true;
        }
    }
    if (lowered.rfind("#br", 0) == 0) {
        if (!ReadDirectiveInt(lowered, 3, false, consumed, value)) {
            consumed = 3;
            value = 1;
        }
        if (HasDirectiveBoundary(text, consumed)) {
            lineMeta.extraBreaks += std::max(1, value);
            return true;
        }
    }
    return false;
}

bool TryConsumeBlockDirective(std::string_view text, RichSegment& block, std::size_t& consumed) {
    consumed = 0;
    if (std::optional<ParsedImage> image = ParseImagePrefix(text, consumed)) {
        block.image = std::move(*image);
        return true;
    }

    const std::string lowered = LowerUtf8(text);
    if (lowered.rfind("#hr", 0) != 0) {
        return false;
    }

    std::size_t pos = 3;
    while (pos < lowered.size() && std::isxdigit(static_cast<unsigned char>(lowered[pos])) != 0) {
        ++pos;
    }
    if (!HasDirectiveBoundary(text, pos)) {
        return false;
    }

    block.isHr = true;
    if (pos == 9) {
        if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(lowered).substr(3, 6))) {
            block.hrColor = *color;
            block.hasHrColor = true;
        }
    }
    consumed = pos;
    return true;
}

std::size_t FindNextInlineDirective(std::string_view text) {
    for (std::size_t pos = 1; pos < text.size(); ++pos) {
        if ((text[pos] == '#' || text[pos] == '{') && IsInlineDirective(text.substr(pos))) {
            return pos;
        }
    }
    return text.size();
}

std::vector<RichSegment> ParseRichLine(std::string_view rawLine) {
    std::string original(rawLine);
    std::size_t nonSpace = 0;
    while (nonSpace < original.size() && std::isspace(static_cast<unsigned char>(original[nonSpace])) != 0) {
        ++nonSpace;
    }

    const std::string leading = original.substr(0, nonSpace);
    std::string_view rest(original.data() + nonSpace, original.size() - nonSpace);
    RichSegment lineMeta;
    InlineStyle style;
    std::vector<RichSegment> runs;
    bool usedDirectiveBeforeText = false;

    while (!rest.empty()) {
        std::size_t consumed = 0;
        RichSegment block = lineMeta;
        if (runs.empty() && TryConsumeLineDirective(rest, lineMeta, consumed)) {
            usedDirectiveBeforeText = true;
            rest.remove_prefix(consumed);
            SkipLeadingAsciiWhitespace(rest);
            continue;
        }
        if (runs.empty() && TryConsumeBlockDirective(rest, block, consumed)) {
            runs.push_back(std::move(block));
            return runs;
        }

        std::string iconGlyph;
        if (TryConsumeInlineDirective(rest, style, consumed, iconGlyph)) {
            usedDirectiveBeforeText = true;
            if (!iconGlyph.empty()) {
                AppendIconRun(runs, lineMeta, style, std::move(iconGlyph));
            }
            rest.remove_prefix(consumed);
            SkipLeadingAsciiWhitespace(rest);
            continue;
        }

        const std::size_t literalLen = FindNextInlineDirective(rest);
        std::string literal(rest.substr(0, literalLen));
        if (!usedDirectiveBeforeText && runs.empty()) {
            literal = leading + literal;
        }
        AppendTextRun(runs, lineMeta, style, std::move(literal));
        rest.remove_prefix(literalLen);
    }

    if (runs.empty()) {
        RichSegment empty = MakeInlineRun(lineMeta, style, true);
        runs.push_back(std::move(empty));
    }
    return runs;
}

std::vector<std::vector<RichSegment>> ParseRichText(std::string_view text) {
    std::vector<std::vector<RichSegment>> result;
    std::stringstream stream{ std::string(text) };
    std::string line;
    while (std::getline(stream, line)) {
        std::vector<RichSegment> segments = ParseRichLine(line);
        const bool sameLine = !segments.empty() && segments.front().sameLine;
        if (sameLine && !result.empty()) {
            for (RichSegment& item : segments) {
                result.back().push_back(std::move(item));
            }
        } else {
            result.push_back(std::move(segments));
        }
    }
    if (text.empty() || (!text.empty() && text.back() == '\n')) {
        result.push_back({ RichSegment{} });
    }
    return result;
}

std::string SegmentPlainText(const RichSegment& segment) {
    if (segment.image || segment.isHr) {
        return {};
    }
    std::string text = segment.isIcon ? segment.icon : segment.text;
    if (!segment.icon.empty()) {
        text = segment.icon + (text.empty() || segment.isIcon ? "" : " " + text);
    }
    if (segment.bullet) {
        text = "- " + text;
    }
    return text;
}

} // namespace

struct MarkupRenderer::Impl {
    std::map<std::wstring, TextureCacheEntry> textureCache;
    std::optional<ParsedTextCacheKey> parsedTextCacheKey;
    std::vector<std::vector<RichSegment>> parsedTextCache;
    std::vector<float> parsedLineAdvance;
    std::vector<bool> parsedLineHasImage;
    MarkupRenderer::DrawStats lastDrawStats{};

    static void ReleaseTexture(TextureCacheEntry& entry) {
        if (entry.texture) {
            entry.texture->Release();
            entry.texture = nullptr;
        }
        entry.width = 0;
        entry.height = 0;
    }

    void ReleaseDeviceResources() {
        for (auto& [_, entry] : textureCache) {
            ReleaseTexture(entry);
        }
        textureCache.clear();
    }

    fs::path ResolveImagePath(const ParsedImage& image, const fs::path& imageRoot) const {
        if (!MarkupRenderer::IsSafeRelativeAssetPath(image.source)) {
            return {};
        }
        return (imageRoot / fs::path(MarkupRenderer::Utf8ToWide(image.source))).lexically_normal();
    }

    void PruneTextureCacheForInsert() {
        if (textureCache.size() < kMaxCachedTextures) {
            return;
        }

        const auto oldest = std::min_element(
            textureCache.begin(),
            textureCache.end(),
            [](const auto& left, const auto& right) {
                return left.second.lastUsedAtMs < right.second.lastUsedAtMs;
            });
        if (oldest == textureCache.end()) {
            return;
        }
        ReleaseTexture(oldest->second);
        textureCache.erase(oldest);
    }

    static bool SameFileStamp(const TextureCacheEntry& entry, const TextureFileStamp& stamp) {
        return entry.fileStampKnown
            && entry.fileSize == stamp.size
            && entry.lastWriteTime == stamp.lastWriteTime;
    }

    static void StoreFileStamp(TextureCacheEntry& entry, const TextureFileStamp& stamp) {
        entry.fileStampKnown = true;
        entry.fileSize = stamp.size;
        entry.lastWriteTime = stamp.lastWriteTime;
    }

    static void LogTextureFailureOnce(TextureCacheEntry& entry, const fs::path& path, HRESULT result) {
        if (entry.failureLogged) {
            return;
        }
        entry.failureLogged = true;
        debuglog::WriteError(
            "[image] texture unavailable path=\"%s\" hresult=0x%08lX retry=%llums",
            MarkupRenderer::WideToUtf8(path.wstring()).c_str(),
            static_cast<unsigned long>(result),
            static_cast<unsigned long long>(kTextureProbeIntervalMs));
    }

    TextureCacheEntry* LoadTexture(IDirect3DDevice9* device, const fs::path& path) {
        if (!device || path.empty()) {
            return nullptr;
        }
        const std::wstring key = path.wstring();
        if (textureCache.find(key) == textureCache.end()) {
            PruneTextureCacheForInsert();
        }
        auto [it, inserted] = textureCache.try_emplace(key);
        TextureCacheEntry& entry = it->second;
        const std::uint64_t now = GetTickCount64();
        entry.lastUsedAtMs = now;
        if (!inserted && now < entry.nextProbeAtMs) {
            return entry.texture ? &entry : nullptr;
        }
        entry.nextProbeAtMs = now + kTextureProbeIntervalMs;

        std::error_code stampError;
        const std::optional<TextureFileStamp> stamp = ReadTextureFileStamp(path, stampError);
        if (!stamp) {
            const bool stateChanged = entry.texture || !entry.failed;
            ReleaseTexture(entry);
            entry.failed = true;
            entry.fileStampKnown = false;
            if (stateChanged || !entry.failureLogged) {
                const HRESULT result = stampError
                    ? HRESULT_FROM_WIN32(static_cast<DWORD>(stampError.value()))
                    : E_FAIL;
                LogTextureFailureOnce(entry, path, result);
            }
            return nullptr;
        }

        const bool fileChanged = !SameFileStamp(entry, *stamp);
        if (!inserted && entry.texture && !fileChanged) {
            return &entry;
        }

        D3DXIMAGE_INFO info{};
        const HRESULT infoResult = D3DXGetImageInfoFromFileW(path.c_str(), &info);
        if (FAILED(infoResult)) {
            if (!entry.texture) {
                entry.failed = true;
                StoreFileStamp(entry, *stamp);
            }
            LogTextureFailureOnce(entry, path, infoResult);
            return entry.texture ? &entry : nullptr;
        }
        IDirect3DTexture9* texture = nullptr;
        const HRESULT textureResult = D3DXCreateTextureFromFileW(device, path.c_str(), &texture);
        if (FAILED(textureResult) || !texture) {
            if (!entry.texture) {
                entry.failed = true;
                StoreFileStamp(entry, *stamp);
            }
            LogTextureFailureOnce(entry, path, FAILED(textureResult) ? textureResult : E_FAIL);
            return entry.texture ? &entry : nullptr;
        }

        const bool recovered = entry.failed;
        const bool reloaded = entry.texture != nullptr;
        ReleaseTexture(entry);
        entry.texture = texture;
        entry.width = info.Width;
        entry.height = info.Height;
        entry.failed = false;
        entry.failureLogged = false;
        StoreFileStamp(entry, *stamp);
        if (recovered) {
            debuglog::WriteInfo(
                "[image] texture recovered path=\"%s\"",
                MarkupRenderer::WideToUtf8(path.wstring()).c_str());
        } else if (reloaded) {
            debuglog::WriteInfo(
                "[image] texture reloaded path=\"%s\"",
                MarkupRenderer::WideToUtf8(path.wstring()).c_str());
        }
        return &entry;
    }

    bool ResolveImageTexture(
        std::string_view source,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        MarkupRenderer::ImageTexture& out) {
        out = {};
        ParsedImage image;
        image.source = std::string(source);
        if (!MarkupRenderer::IsSafeRelativeAssetPath(image.source)) {
            return false;
        }

        TextureCacheEntry* texture = LoadTexture(device, ResolveImagePath(image, imageRoot));
        if (!texture || !texture->texture) {
            return false;
        }

        out.textureId = reinterpret_cast<ImTextureID>(texture->texture);
        out.width = texture->width;
        out.height = texture->height;
        return true;
    }

    const std::vector<std::vector<RichSegment>>& ParsedLines(
        std::string_view text,
        const fs::path& imageRoot,
        const MarkupRenderer::DrawOptions& options) {
        ParsedTextCacheKey key{};
        key.text = std::string(text);
        key.imageRoot = imageRoot.wstring();
        key.wrapText = options.wrapText;
        key.fontDirectiveScale = QuantizeLayoutFloat(FontDirectiveScale(options));
        key.wrapWidth = options.wrapText ? QuantizeLayoutFloat(ImGui::GetContentRegionAvail().x) : 0;
        key.uiScale = QuantizeLayoutFloat(ScaleUi(1.0f));
        key.baseFont = ImGui::GetFont();
        key.baseFontSize = QuantizeLayoutFloat(ImGui::GetFontSize());
        key.itemSpacingY = QuantizeLayoutFloat(ImGui::GetStyle().ItemSpacing.y);

        if (!parsedTextCacheKey || !(*parsedTextCacheKey == key)) {
            parsedTextCache = ParseRichText(text);
            parsedLineAdvance.assign(parsedTextCache.size(), 0.0f);
            parsedLineHasImage.clear();
            parsedLineHasImage.reserve(parsedTextCache.size());
            for (std::vector<RichSegment>& line : parsedTextCache) {
                bool hasImage = false;
                for (RichSegment& segment : line) {
                    segment.displayText = SegmentPlainText(segment);
                    hasImage = hasImage || segment.image.has_value();
                }
                parsedLineHasImage.push_back(hasImage);
            }
            parsedTextCacheKey = std::move(key);
        }
        return parsedTextCache;
    }

    ImVec2 ResolveImageRenderSize(const ParsedImage& image, const TextureCacheEntry* texture) {
        float width = image.width > 0 ? ScaleUi(static_cast<float>(image.width)) : 0.0f;
        float height = image.height > 0 ? ScaleUi(static_cast<float>(image.height)) : 0.0f;
        if (texture && texture->width > 0 && texture->height > 0) {
            if (width <= 0.0f && height <= 0.0f) {
                width = static_cast<float>(texture->width);
                height = static_cast<float>(texture->height);
            } else if (width > 0.0f && height <= 0.0f) {
                height = width * static_cast<float>(texture->height) / static_cast<float>(texture->width);
            } else if (height > 0.0f && width <= 0.0f) {
                width = height * static_cast<float>(texture->width) / static_cast<float>(texture->height);
            }
        } else if (width <= 0.0f || height <= 0.0f) {
            return ScaleUi(160.0f, 24.0f);
        }
        const float maxWidth = std::max(ScaleUi(80.0f), ImGui::GetContentRegionAvail().x);
        if (!image.hasPosition && width > maxWidth && width > 0.0f) {
            const float ratio = maxWidth / width;
            width *= ratio;
            height *= ratio;
        }
        return ImVec2(std::max(ScaleUi(16.0f), width), std::max(ScaleUi(16.0f), height));
    }

    float CalcSegmentWidth(
        const RichSegment& segment,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const MarkupRenderer::DrawOptions& options) {
        if (segment.image) {
            const fs::path path = ResolveImagePath(*segment.image, imageRoot);
            const TextureCacheEntry* texture = LoadTexture(device, path);
            const ImVec2 size = ResolveImageRenderSize(*segment.image, texture);
            return segment.image->hasPosition ? ScaleUi(static_cast<float>(segment.image->posX)) + size.x : size.x;
        }
        const std::string_view text = segment.displayText;
        const ui_fonts::ScopedFontSize fontScope(
            segment.fontSize > 0 ? static_cast<float>(segment.fontSize) * FontDirectiveScale(options) : 0.0f);
        const float width = ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
        return width;
    }

    float CalcLineWidth(
        const std::vector<RichSegment>& segments,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const MarkupRenderer::DrawOptions& options) {
        float width = 0.0f;
        bool hasVisibleSegment = false;
        for (const RichSegment& segment : segments) {
            if (segment.isHr) {
                continue;
            }
            const float segmentWidth = CalcSegmentWidth(segment, device, imageRoot, options);
            if (segmentWidth <= 0.0f && !segment.image && TrimAscii(segment.displayText).empty()) {
                continue;
            }
            if (hasVisibleSegment) {
                width += segment.inlineContinuation ? 0.0f : InlineSegmentSpacing();
            }
            width += segmentWidth;
            hasVisibleSegment = true;
        }
        return width;
    }

    ImVec2 DrawImageSegment(
        const ParsedImage& image,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const ImVec2& contentOrigin) {
        UiSettings& ui = UiSettings::Instance();
        if (!MarkupRenderer::IsSafeRelativeAssetPath(image.source)) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.25f, 1.0f), "%s", ui.Text(UiText::NotepadInvalidImagePath));
            return ImGui::GetItemRectSize();
        }
        const fs::path path = ResolveImagePath(image, imageRoot);
        TextureCacheEntry* texture = LoadTexture(device, path);
        if (!texture || !texture->texture) {
            const std::string message = ui.Format(UiText::NotepadMissingImageFormat, image.source.c_str());
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", message.c_str());
            return ImGui::GetItemRectSize();
        }
        const ImVec2 size = ResolveImageRenderSize(image, texture);
        if (image.hasPosition) {
            const ImVec2 pos(
                contentOrigin.x + ScaleUi(static_cast<float>(image.posX)),
                contentOrigin.y + ScaleUi(static_cast<float>(image.posY)));
            ImGui::GetWindowDrawList()->AddImage(
                reinterpret_cast<ImTextureID>(texture->texture),
                pos,
                ImVec2(pos.x + size.x, pos.y + size.y));
            const ImVec2 extent(
                std::max(ScaleUi(1.0f), ScaleUi(static_cast<float>(image.posX)) + size.x),
                std::max(ScaleUi(1.0f), ScaleUi(static_cast<float>(image.posY)) + size.y));
            ImGui::Dummy(extent);
            return extent;
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(texture->texture), size);
        return size;
    }

    ImVec2 DrawSegment(
        const RichSegment& segment,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const ImVec2& contentOrigin,
        const MarkupRenderer::DrawOptions& options,
        std::optional<std::string_view> textOverride = std::nullopt) {
        ImVec4 textColor = segment.hasColor ? segment.color : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        textColor.w *= segment.alpha;
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        const ui_fonts::ScopedFontSize fontScope(
            segment.fontSize > 0 ? static_cast<float>(segment.fontSize) * FontDirectiveScale(options) : 0.0f);

        ImVec2 drawnSize{};
        if (segment.image) {
            drawnSize = DrawImageSegment(*segment.image, device, imageRoot, contentOrigin);
        } else {
            const std::string_view text = textOverride.value_or(std::string_view(segment.displayText));
            const char* textBegin = text.empty() ? "" : text.data();
            const char* textEnd = textBegin + text.size();
            const ImVec2 start = ui_fonts::SnapPixel(ImGui::GetCursorScreenPos());
            ImGui::SetCursorScreenPos(start);
            const ImVec2 textSize = text.empty()
                ? ImGui::CalcTextSize(" ")
                : ImGui::CalcTextSize(textBegin, textEnd);
            if (segment.hasBgColor && !text.empty()) {
                ImVec4 bg = segment.bgColor;
                bg.w *= segment.alpha;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(start.x - ScaleUi(3.0f), start.y - ScaleUi(2.0f)),
                    ImVec2(start.x + textSize.x + ScaleUi(3.0f), start.y + textSize.y + ScaleUi(2.0f)),
                    ImGui::GetColorU32(bg),
                    ScaleUi(3.0f));
            }
            if ((segment.shadow || segment.outline) && !text.empty()) {
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                ImFont* font = ImGui::GetFont();
                const float fontSize = ImGui::GetFontSize();
                const float wrapWidth = options.wrapText ? ImGui::GetContentRegionAvail().x : 0.0f;
                const float outlineOffset = ui_fonts::RoundFontSize(std::max(1.0f, ScaleUi(1.0f)));
                const ImU32 outlineColor = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, textColor.w * 0.65f));
                const ImU32 shadowColor = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, textColor.w * 0.55f));
                if (segment.outline) {
                    drawList->AddText(font, fontSize, ImVec2(start.x - outlineOffset, start.y), outlineColor, textBegin, textEnd, wrapWidth);
                    drawList->AddText(font, fontSize, ImVec2(start.x + outlineOffset, start.y), outlineColor, textBegin, textEnd, wrapWidth);
                    drawList->AddText(font, fontSize, ImVec2(start.x, start.y - outlineOffset), outlineColor, textBegin, textEnd, wrapWidth);
                    drawList->AddText(font, fontSize, ImVec2(start.x, start.y + outlineOffset), outlineColor, textBegin, textEnd, wrapWidth);
                }
                if (segment.shadow) {
                    const float shadowOffset = ui_fonts::RoundFontSize(std::max(1.0f, ScaleUi(1.25f)));
                    drawList->AddText(
                        font,
                        fontSize,
                        ImVec2(start.x + shadowOffset, start.y + shadowOffset),
                        shadowColor,
                        textBegin,
                        textEnd,
                        wrapWidth);
                }
            }
            if (options.wrapText) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(textBegin, textEnd);
                ImGui::PopTextWrapPos();
            } else {
                ImGui::TextUnformatted(text.empty() ? " " : textBegin, text.empty() ? nullptr : textEnd);
            }
            drawnSize = ImGui::GetItemRectSize();
        }

        ImGui::PopStyleColor();
        return drawnSize;
    }

    void DrawWrappedRichLine(
        const std::vector<RichSegment>& segments,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const ImVec2& contentOrigin,
        const MarkupRenderer::DrawOptions& options,
        float lineStartX,
        float lineStartY,
        float avail,
        float lineWidth) {
        float wrapStartX = lineStartX + segments.front().indent;
        if (segments.front().align == RichSegment::Align::Center) {
            wrapStartX = lineStartX + std::max(0.0f, (avail - lineWidth) * 0.5f);
        } else if (segments.front().align == RichSegment::Align::Right) {
            wrapStartX = lineStartX + std::max(0.0f, avail - lineWidth);
        }
        wrapStartX = std::max(lineStartX, wrapStartX);

        const float wrapEndX = lineStartX + avail;
        float cursorX = wrapStartX;
        float cursorY = lineStartY;
        float visualLineHeight = 0.0f;
        bool visualLineHasContent = false;
        bool hasDrawn = false;

        auto finishVisualLine = [&] {
            cursorY += std::max(visualLineHeight, ImGui::GetTextLineHeight());
            cursorX = wrapStartX;
            visualLineHeight = 0.0f;
            visualLineHasContent = false;
        };

        MarkupRenderer::DrawOptions noWrapOptions = options;
        noWrapOptions.wrapText = false;

        for (std::size_t segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
            const RichSegment& segment = segments[segmentIndex];
            if (segment.isHr) {
                continue;
            }

            const std::string_view text = segment.displayText;
            if (!segment.image && text.empty()) {
                continue;
            }

            if (visualLineHasContent && !segment.inlineContinuation) {
                const float spacing = InlineSegmentSpacing();
                if (cursorX + spacing >= wrapEndX) {
                    finishVisualLine();
                } else {
                    cursorX += spacing;
                }
            }
            if (segmentIndex > 0) {
                cursorX += segment.indent;
            }

            if (segment.image) {
                const float segmentWidth = CalcSegmentWidth(segment, device, imageRoot, noWrapOptions);
                if (visualLineHasContent && cursorX + segmentWidth > wrapEndX) {
                    finishVisualLine();
                }
                ImGui::SetCursorPos(ImVec2(cursorX, cursorY));
                const ImVec2 drawnSize = DrawSegment(segment, device, imageRoot, contentOrigin, noWrapOptions);
                cursorX += drawnSize.x;
                visualLineHeight = std::max(visualLineHeight, drawnSize.y);
                visualLineHasContent = true;
                hasDrawn = true;
                continue;
            }

            const char* textCursor = text.data();
            const char* textEnd = textCursor + text.size();
            while (textCursor < textEnd) {
                const float availableWidth = std::max(1.0f, wrapEndX - cursorX);
                const char* chunkEnd = textEnd;
                {
                    const ui_fonts::ScopedFontSize fontScope(
                        segment.fontSize > 0
                            ? static_cast<float>(segment.fontSize) * FontDirectiveScale(options)
                            : 0.0f);
                    ImFont* font = ImGui::GetFont();
                    const float fontSize = ImGui::GetFontSize();
                    const float fullWidth = font->CalcTextSizeA(
                        fontSize,
                        FLT_MAX,
                        0.0f,
                        textCursor,
                        textEnd).x;
                    if (fullWidth > availableWidth) {
                        const float scale = fontSize / std::max(1.0f, font->LegacySize);
                        chunkEnd = font->CalcWordWrapPositionA(scale, textCursor, textEnd, availableWidth);
                    }
                }

                if (chunkEnd <= textCursor) {
                    if (visualLineHasContent) {
                        finishVisualLine();
                        continue;
                    }
                    unsigned int codepoint = 0;
                    const int bytes = ImTextCharFromUtf8(&codepoint, textCursor, textEnd);
                    chunkEnd = textCursor + std::max(1, bytes);
                }

                ImGui::SetCursorPos(ImVec2(cursorX, cursorY));
                const ImVec2 drawnSize = DrawSegment(
                    segment,
                    device,
                    imageRoot,
                    contentOrigin,
                    noWrapOptions,
                    std::string_view(textCursor, static_cast<std::size_t>(chunkEnd - textCursor)));
                cursorX += drawnSize.x;
                visualLineHeight = std::max(visualLineHeight, drawnSize.y);
                visualLineHasContent = true;
                hasDrawn = true;
                textCursor = chunkEnd;

                while (textCursor < textEnd && (*textCursor == ' ' || *textCursor == '\t')) {
                    ++textCursor;
                }
                if (textCursor < textEnd) {
                    finishVisualLine();
                }
            }
        }

        if (!hasDrawn) {
            ImGui::SetCursorPos(ImVec2(lineStartX, lineStartY));
            ImGui::TextUnformatted("");
            cursorY = lineStartY + ImGui::GetItemRectSize().y;
        } else if (visualLineHasContent) {
            cursorY += std::max(visualLineHeight, ImGui::GetTextLineHeight());
        }

        ImGui::SetCursorPos(ImVec2(lineStartX, cursorY));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        for (int i = 0; i < segments.front().extraBreaks; ++i) {
            ImGui::SetCursorPosX(lineStartX);
            ImGui::TextUnformatted("");
        }
    }

    void DrawRichLine(
        const std::vector<RichSegment>& segments,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const ImVec2& contentOrigin,
        const MarkupRenderer::DrawOptions& options) {
        if (segments.empty()) {
            ImGui::TextUnformatted("");
            return;
        }
        const float lineStartX = ImGui::GetCursorPosX();
        const float lineStartY = ImGui::GetCursorPosY();
        const float avail = ImGui::GetContentRegionAvail().x;
        const bool needsLineWidth = std::any_of(
            segments.begin(),
            segments.end(),
            [](const RichSegment& segment) {
                return segment.align != RichSegment::Align::Left;
            });
        const float lineWidth = needsLineWidth
            ? CalcLineWidth(segments, device, imageRoot, options)
            : 0.0f;
        float maxHeight = 0.0f;
        bool hasDrawn = false;

        if (options.wrapText && segments.size() > 1) {
            DrawWrappedRichLine(
                segments,
                device,
                imageRoot,
                contentOrigin,
                options,
                lineStartX,
                lineStartY,
                avail,
                lineWidth);
            return;
        }

        for (std::size_t i = 0; i < segments.size(); ++i) {
            const RichSegment& segment = segments[i];
            if (segment.isHr) {
                const ImVec2 cursor = ImGui::GetCursorScreenPos();
                const ImVec4 color = segment.hasHrColor ? segment.hrColor : ImGui::GetStyleColorVec4(ImGuiCol_Separator);
                const float separatorSize = ImGui::GetStyle().SeparatorSize;
                if (separatorSize > 0.0f) {
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(cursor.x, cursor.y + ImGui::GetTextLineHeight() * 0.5f),
                        ImVec2(cursor.x + avail, cursor.y + ImGui::GetTextLineHeight() * 0.5f),
                        ImGui::GetColorU32(color),
                        separatorSize);
                }
                ImGui::Dummy(ImVec2(avail, ImGui::GetTextLineHeightWithSpacing()));
                return;
            }

            float targetX = lineStartX + segment.indent;
            if (segment.align == RichSegment::Align::Center) {
                targetX = lineStartX + std::max(0.0f, (avail - lineWidth) * 0.5f);
            } else if (segment.align == RichSegment::Align::Right) {
                targetX = lineStartX + std::max(0.0f, avail - lineWidth);
            } else if (i > 0) {
                ImGui::SameLine(0.0f, segment.inlineContinuation ? 0.0f : InlineSegmentSpacing());
                targetX = ImGui::GetCursorPosX() + segment.indent;
            }
            ImGui::SetCursorPosX(std::max(lineStartX, targetX));
            const ImVec2 drawnSize = DrawSegment(segment, device, imageRoot, contentOrigin, options);
            maxHeight = std::max(maxHeight, drawnSize.y);
            hasDrawn = true;
        }

        if (!hasDrawn) {
            ImGui::TextUnformatted("");
            maxHeight = ImGui::GetItemRectSize().y;
        }
        const float currentY = ImGui::GetCursorPosY();
        const float targetY = lineStartY + maxHeight + ImGui::GetStyle().ItemSpacing.y;
        const float advanceY = targetY - currentY;
        if (advanceY > ImGui::GetStyle().ItemSpacing.y) {
            ImGui::Dummy(ImVec2(0.0f, advanceY - ImGui::GetStyle().ItemSpacing.y));
        }
        for (int i = 0; i < segments.front().extraBreaks; ++i) {
            ImGui::SetCursorPosX(lineStartX);
            ImGui::TextUnformatted("");
        }
    }

    void DrawText(
        std::string_view text,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const MarkupRenderer::DrawOptions& options) {
        lastDrawStats = {};
        const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
        const float lineStartX = ImGui::GetCursorPosX();
        const auto& lines = ParsedLines(text, imageRoot, options);
        lastDrawStats.totalLines = static_cast<int>(lines.size());

        const ImVec2 clipMin = ImGui::GetWindowDrawList()->GetClipRectMin();
        const ImVec2 clipMax = ImGui::GetWindowDrawList()->GetClipRectMax();
        const float overscan = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        const auto outsideClip = [&](float screenY, float advance) {
            return screenY + advance < clipMin.y - overscan
                || screenY > clipMax.y + overscan;
        };
        const auto advanceCachedSpan = [](float advance) {
            const float dummyHeight = std::max(0.0f, advance - ImGui::GetStyle().ItemSpacing.y);
            ImGui::Dummy(ImVec2(0.0f, dummyHeight));
        };

        for (std::size_t lineIndex = 0; lineIndex < lines.size();) {
            ImGui::SetCursorPosX(lineStartX);
            const float firstScreenY = ImGui::GetCursorScreenPos().y;
            std::size_t skipEnd = lineIndex;
            float skippedAdvance = 0.0f;
            while (skipEnd < lines.size()
                && parsedLineAdvance[skipEnd] > 0.0f
                && !parsedLineHasImage[skipEnd]
                && outsideClip(firstScreenY + skippedAdvance, parsedLineAdvance[skipEnd])) {
                skippedAdvance += parsedLineAdvance[skipEnd];
                ++skipEnd;
            }
            if (skipEnd > lineIndex) {
                advanceCachedSpan(skippedAdvance);
                lastDrawStats.skippedLines += static_cast<int>(skipEnd - lineIndex);
                lineIndex = skipEnd;
                continue;
            }

            const float lineStartY = ImGui::GetCursorPosY();
            DrawRichLine(lines[lineIndex], device, imageRoot, contentOrigin, options);
            const float lineAdvance = ImGui::GetCursorPosY() - lineStartY;
            if (!parsedLineHasImage[lineIndex] && lineAdvance > 0.0f) {
                parsedLineAdvance[lineIndex] = lineAdvance;
            }
            ++lastDrawStats.drawnLines;
            ++lineIndex;
        }
        lastDrawStats.cachedLines = static_cast<int>(std::count_if(
            parsedLineAdvance.begin(),
            parsedLineAdvance.end(),
            [](float advance) { return advance > 0.0f; }));
    }
};

MarkupRenderer::MarkupRenderer() : impl_(std::make_unique<Impl>()) {
}

MarkupRenderer::~MarkupRenderer() = default;

MarkupRenderer::MarkupRenderer(MarkupRenderer&&) noexcept = default;

MarkupRenderer& MarkupRenderer::operator=(MarkupRenderer&&) noexcept = default;

void MarkupRenderer::ReleaseDeviceResources() {
    impl_->ReleaseDeviceResources();
}

void MarkupRenderer::DrawText(std::string_view text, IDirect3DDevice9* device, const std::filesystem::path& imageRoot) {
    impl_->DrawText(text, device, imageRoot, DrawOptions{});
}

void MarkupRenderer::DrawText(
    std::string_view text,
    IDirect3DDevice9* device,
    const std::filesystem::path& imageRoot,
    const DrawOptions& options) {
    impl_->DrawText(text, device, imageRoot, options);
}

MarkupRenderer::DrawStats MarkupRenderer::LastDrawStats() const {
    return impl_->lastDrawStats;
}

bool MarkupRenderer::ResolveImageTexture(
    std::string_view source,
    IDirect3DDevice9* device,
    const std::filesystem::path& imageRoot,
    ImageTexture& out) {
    return impl_->ResolveImageTexture(source, device, imageRoot, out);
}

bool MarkupRenderer::HasVisibleContent(std::string_view text) {
    const auto lines = ParseRichText(text);
    for (const std::vector<RichSegment>& line : lines) {
        for (const RichSegment& segment : line) {
            if (segment.image || segment.isHr || !TrimAscii(SegmentPlainText(segment)).empty()) {
                return true;
            }
        }
    }
    return false;
}

std::string MarkupRenderer::StripMarkupLine(std::string_view line) {
    const std::vector<RichSegment> segments = ParseRichLine(line);
    std::string output;
    for (const RichSegment& segment : segments) {
        output += SegmentPlainText(segment);
    }
    return output;
}

std::string MarkupRenderer::StripMarkup(std::string_view text) {
    std::stringstream stream{ std::string(text) };
    std::string line;
    std::string out;
    bool first = true;
    while (std::getline(stream, line)) {
        if (!first) {
            out += "\n";
        }
        first = false;
        out += StripMarkupLine(line);
    }
    return out;
}

bool MarkupRenderer::IsSafeRelativeAssetPath(std::string_view path) {
    const std::wstring wide = Utf8ToWide(path);
    if (wide.empty()) {
        return false;
    }
    const fs::path relative(wide);
    if (relative.is_absolute() || relative.has_root_directory() || relative.has_root_name()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == L"." || part == L"..") {
            return false;
        }
    }
    return true;
}

std::wstring MarkupRenderer::Utf8ToWide(std::string_view text) {
    std::wstring wide = MultiByteToWide(text, CP_UTF8, MB_ERR_INVALID_CHARS);
    if (wide.empty() && !text.empty()) {
        wide = MultiByteToWide(text, CP_ACP);
    }
    return wide;
}

std::string MarkupRenderer::WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            result.data(),
            required,
            nullptr,
            nullptr)
        <= 0) {
        return {};
    }
    return result;
}
