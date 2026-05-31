#include "markup_renderer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "ui_icons.h"
#include "ui_settings.h"

#include <d3dx9tex.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

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
    bool bullet = false;
    bool sameLine = false;
    bool inlineContinuation = false;
    bool isHr = false;
    int extraBreaks = 0;
    enum class Align { Left, Center, Right } align = Align::Left;
    enum class Transform { None, Upper, Lower } transform = Transform::None;
    std::optional<ParsedImage> image{};
};

struct TextureCacheEntry {
    IDirect3DTexture9* texture = nullptr;
    UINT width = 0;
    UINT height = 0;
    bool failed = false;
};

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

std::string ResolveIconGlyph(std::string_view name) {
    const std::string normalized = LowerUtf8(name);
    static const std::map<std::string, const char*> iconsByName = {
        { "bars", ui_icons::Bars },
        { "book", ui_icons::Book },
        { "car", ui_icons::Cubes },
        { "check", ui_icons::Check },
        { "compass", ui_icons::Compass },
        { "copy", ui_icons::Copy },
        { "file", ui_icons::Book },
        { "folder", ui_icons::Folder },
        { "gun", ui_icons::Bolt },
        { "image", ui_icons::Image },
        { "note", ui_icons::Book },
        { "save", ui_icons::SaveDisk },
        { "star", ui_icons::Star },
        { "user", ui_icons::Tags },
        { "weapon", ui_icons::Bolt },
        { "wrench", ui_icons::Sliders },
    };
    const auto it = iconsByName.find(normalized);
    if (it != iconsByName.end()) {
        return it->second;
    }
    return "[" + UpperUtf8(name) + "]";
}

RichSegment ParseRichSegment(std::string_view rawLine) {
    RichSegment segment;
    std::string original(rawLine);
    std::size_t nonSpace = 0;
    while (nonSpace < original.size() && std::isspace(static_cast<unsigned char>(original[nonSpace])) != 0) {
        ++nonSpace;
    }
    std::string leading = original.substr(0, nonSpace);
    std::string rest = original.substr(nonSpace);
    bool usedDirective = false;

    while (!rest.empty()) {
        bool consumedAny = false;
        std::size_t consumed = 0;
        const std::string lowered = LowerUtf8(rest);
        if (rest.size() >= 8 && rest.front() == '{' && rest[7] == '}') {
            if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(rest).substr(1, 6))) {
                segment.color = *color;
                segment.hasColor = true;
                consumedAny = true;
                consumed = 8;
            }
        } else if (lowered.rfind("#sameline", 0) == 0 && HasDirectiveBoundary(rest, 9)) {
            segment.sameLine = true;
            consumedAny = true;
            consumed = 9;
        } else if (lowered.rfind("#right", 0) == 0 && HasDirectiveBoundary(rest, 6)) {
            segment.align = RichSegment::Align::Right;
            consumedAny = true;
            consumed = 6;
        } else if (lowered.rfind("#center", 0) == 0 && HasDirectiveBoundary(rest, 7)) {
            segment.align = RichSegment::Align::Center;
            consumedAny = true;
            consumed = 7;
        } else if (lowered.rfind("#left", 0) == 0 && HasDirectiveBoundary(rest, 5)) {
            segment.align = RichSegment::Align::Left;
            consumedAny = true;
            consumed = 5;
        } else if (lowered.rfind("#bullet", 0) == 0 && HasDirectiveBoundary(rest, 7)) {
            segment.bullet = true;
            consumedAny = true;
            consumed = 7;
        } else if (lowered.rfind("#upper", 0) == 0 && HasDirectiveBoundary(rest, 6)) {
            segment.transform = RichSegment::Transform::Upper;
            consumedAny = true;
            consumed = 6;
        } else if (lowered.rfind("#lower", 0) == 0 && HasDirectiveBoundary(rest, 6)) {
            segment.transform = RichSegment::Transform::Lower;
            consumedAny = true;
            consumed = 6;
        } else if (std::optional<ParsedImage> image = ParseImagePrefix(rest, consumed)) {
            segment.image = std::move(*image);
            consumedAny = true;
        }

        if (!consumedAny) {
            std::string digits;
            auto readDigits = [&](std::size_t prefixLen, bool allowNegative = false) -> std::string {
                std::string out;
                std::size_t pos = prefixLen;
                if (allowNegative && pos < lowered.size() && lowered[pos] == '-') {
                    out.push_back('-');
                    ++pos;
                }
                while (pos < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[pos])) != 0) {
                    out.push_back(lowered[pos++]);
                }
                consumed = pos;
                return out;
            };

            if (lowered.rfind("#font", 0) == 0) {
                digits = readDigits(5);
                const int size = digits.empty() ? 0 : std::atoi(digits.c_str());
                if ((size == 12 || size == 14 || size == 16 || size == 18 || size == 30) && HasDirectiveBoundary(rest, consumed)) {
                    segment.fontSize = size;
                    consumedAny = true;
                }
            } else if (lowered.rfind("#icon", 0) == 0) {
                std::size_t pos = 5;
                while (pos < lowered.size() && (std::isalnum(static_cast<unsigned char>(lowered[pos])) != 0 || lowered[pos] == '_')) {
                    ++pos;
                }
                if (pos > 5 && HasDirectiveBoundary(rest, pos)) {
                    segment.icon = ResolveIconGlyph(lowered.substr(5, pos - 5));
                    consumed = pos;
                    consumedAny = true;
                }
            } else if (lowered.rfind("#color", 0) == 0 && lowered.size() >= 12) {
                if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(lowered).substr(6, 6));
                    color && HasDirectiveBoundary(rest, 12)) {
                    segment.color = *color;
                    segment.hasColor = true;
                    consumed = 12;
                    consumedAny = true;
                }
            } else if (lowered.rfind("#bg", 0) == 0 && lowered.size() >= 9) {
                if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(lowered).substr(3, 6));
                    color && HasDirectiveBoundary(rest, 9)) {
                    segment.bgColor = *color;
                    segment.hasBgColor = true;
                    consumed = 9;
                    consumedAny = true;
                }
            } else if (lowered.rfind("#alpha", 0) == 0) {
                digits = readDigits(6);
                if (!digits.empty() && HasDirectiveBoundary(rest, consumed)) {
                    segment.alpha = std::clamp(std::atoi(digits.c_str()), 0, 100) / 100.0f;
                    consumedAny = true;
                }
            } else if (lowered.rfind("#indent", 0) == 0) {
                digits = readDigits(7, true);
                if (!digits.empty() && HasDirectiveBoundary(rest, consumed)) {
                    segment.indent += ScaleUi(static_cast<float>(std::atoi(digits.c_str())));
                    consumedAny = true;
                }
            } else if (lowered.rfind("#pad", 0) == 0) {
                digits = readDigits(4, true);
                if (!digits.empty() && HasDirectiveBoundary(rest, consumed)) {
                    segment.indent += ScaleUi(static_cast<float>(std::atoi(digits.c_str())));
                    consumedAny = true;
                }
            } else if (lowered.rfind("#tab", 0) == 0) {
                digits = readDigits(4);
                if (HasDirectiveBoundary(rest, consumed)) {
                    segment.indent += ScaleUi(static_cast<float>(std::max(1, std::atoi(digits.empty() ? "1" : digits.c_str())) * 32));
                    consumedAny = true;
                }
            } else if (lowered.rfind("#br", 0) == 0) {
                digits = readDigits(3);
                if (HasDirectiveBoundary(rest, consumed)) {
                    segment.extraBreaks += std::max(1, std::atoi(digits.empty() ? "1" : digits.c_str()));
                    consumedAny = true;
                }
            } else if (lowered.rfind("#hr", 0) == 0) {
                std::size_t pos = 3;
                while (pos < lowered.size() && std::isxdigit(static_cast<unsigned char>(lowered[pos])) != 0) {
                    ++pos;
                }
                if (HasDirectiveBoundary(rest, pos)) {
                    segment.isHr = true;
                    if (pos == 9) {
                        if (const std::optional<ImVec4> color = ParseColorHex(std::string_view(lowered).substr(3, 6))) {
                            segment.hrColor = *color;
                            segment.hasHrColor = true;
                        }
                    }
                    consumed = pos;
                    consumedAny = true;
                }
            }
        }

        if (!consumedAny) {
            break;
        }
        usedDirective = true;
        rest.erase(0, consumed);
        while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0) {
            rest.erase(rest.begin());
        }
    }

    segment.text = usedDirective ? rest : leading + rest;
    if (segment.transform == RichSegment::Transform::Upper) {
        segment.text = UpperUtf8(segment.text);
    } else if (segment.transform == RichSegment::Transform::Lower) {
        segment.text = LowerUtf8(segment.text);
    }
    return segment;
}

std::optional<ImVec4> ParseInlineColorMarker(std::string_view text, std::size_t pos) {
    if (pos + 8 > text.size() || text[pos] != '{' || text[pos + 7] != '}') {
        return std::nullopt;
    }
    return ParseColorHex(text.substr(pos + 1, 6));
}

RichSegment MakeInlineColorPiece(
    const RichSegment& source,
    std::string text,
    const ImVec4& color,
    bool hasColor,
    bool firstPiece) {
    RichSegment piece = source;
    piece.text = std::move(text);
    piece.color = color;
    piece.hasColor = hasColor;
    piece.image.reset();
    piece.isHr = false;
    piece.inlineContinuation = !firstPiece;
    if (!firstPiece) {
        piece.icon.clear();
        piece.bullet = false;
        piece.sameLine = false;
        piece.align = RichSegment::Align::Left;
        piece.indent = 0.0f;
        piece.extraBreaks = 0;
    }
    return piece;
}

std::vector<RichSegment> SplitSegmentByInlineColors(const RichSegment& segment) {
    if (segment.image || segment.isHr || segment.text.empty()) {
        return { segment };
    }

    std::vector<RichSegment> parts;
    ImVec4 currentColor = segment.color;
    bool currentHasColor = segment.hasColor;
    std::size_t textStart = 0;
    bool sawColorMarker = false;

    for (std::size_t pos = 0; pos < segment.text.size();) {
        const std::optional<ImVec4> color = ParseInlineColorMarker(segment.text, pos);
        if (!color) {
            ++pos;
            continue;
        }

        sawColorMarker = true;
        if (pos > textStart) {
            parts.push_back(MakeInlineColorPiece(
                segment,
                segment.text.substr(textStart, pos - textStart),
                currentColor,
                currentHasColor,
                parts.empty()));
        }
        currentColor = *color;
        currentHasColor = true;
        pos += 8;
        textStart = pos;
    }

    if (!sawColorMarker) {
        return { segment };
    }

    if (textStart < segment.text.size()) {
        parts.push_back(MakeInlineColorPiece(
            segment,
            segment.text.substr(textStart),
            currentColor,
            currentHasColor,
            parts.empty()));
    }

    if (parts.empty()) {
        RichSegment empty = segment;
        empty.text.clear();
        empty.color = currentColor;
        empty.hasColor = currentHasColor;
        return { std::move(empty) };
    }
    return parts;
}

std::vector<std::vector<RichSegment>> ParseRichText(std::string_view text) {
    std::vector<std::vector<RichSegment>> result;
    std::stringstream stream{ std::string(text) };
    std::string line;
    while (std::getline(stream, line)) {
        RichSegment segment = ParseRichSegment(line);
        std::vector<RichSegment> segments = SplitSegmentByInlineColors(segment);
        if (segment.sameLine && !result.empty()) {
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
    std::string text = segment.text;
    if (!segment.icon.empty()) {
        text = segment.icon + (text.empty() ? "" : " " + text);
    }
    if (segment.bullet) {
        text = "- " + text;
    }
    return text;
}

} // namespace

struct MarkupRenderer::Impl {
    std::map<std::wstring, TextureCacheEntry> textureCache;

    void ReleaseDeviceResources() {
        for (auto& [_, entry] : textureCache) {
            if (entry.texture) {
                entry.texture->Release();
                entry.texture = nullptr;
            }
        }
        textureCache.clear();
    }

    fs::path ResolveImagePath(const ParsedImage& image, const fs::path& imageRoot) const {
        if (!MarkupRenderer::IsSafeRelativeAssetPath(image.source)) {
            return {};
        }
        return (imageRoot / fs::path(MarkupRenderer::Utf8ToWide(image.source))).lexically_normal();
    }

    TextureCacheEntry* LoadTexture(IDirect3DDevice9* device, const fs::path& path) {
        if (!device || path.empty()) {
            return nullptr;
        }
        const std::wstring key = path.wstring();
        auto [it, inserted] = textureCache.try_emplace(key);
        TextureCacheEntry& entry = it->second;
        if (!inserted) {
            return entry.failed ? nullptr : &entry;
        }
        D3DXIMAGE_INFO info{};
        const HRESULT infoResult = D3DXGetImageInfoFromFileW(path.c_str(), &info);
        if (FAILED(infoResult)) {
            entry.failed = true;
            return nullptr;
        }
        IDirect3DTexture9* texture = nullptr;
        const HRESULT textureResult = D3DXCreateTextureFromFileW(device, path.c_str(), &texture);
        if (FAILED(textureResult) || !texture) {
            entry.failed = true;
            return nullptr;
        }
        entry.texture = texture;
        entry.width = info.Width;
        entry.height = info.Height;
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

    ImVec2 ResolveImageRenderSize(const ParsedImage& image, IDirect3DDevice9* device, const fs::path& imageRoot) {
        const fs::path path = ResolveImagePath(image, imageRoot);
        const TextureCacheEntry* texture = LoadTexture(device, path);
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

    float CalcSegmentWidth(const RichSegment& segment, IDirect3DDevice9* device, const fs::path& imageRoot) {
        if (segment.image) {
            const ImVec2 size = ResolveImageRenderSize(*segment.image, device, imageRoot);
            return segment.image->hasPosition ? ScaleUi(static_cast<float>(segment.image->posX)) + size.x : size.x;
        }
        std::string text = SegmentPlainText(segment);
        const float previousScale = 1.0f;
        if (segment.fontSize > 0) {
            ImGui::SetWindowFontScale(segment.fontSize / 16.0f);
        }
        const float width = ImGui::CalcTextSize(text.c_str()).x;
        if (segment.fontSize > 0) {
            ImGui::SetWindowFontScale(previousScale);
        }
        return width;
    }

    float CalcLineWidth(const std::vector<RichSegment>& segments, IDirect3DDevice9* device, const fs::path& imageRoot) {
        float width = 0.0f;
        bool hasVisibleSegment = false;
        for (const RichSegment& segment : segments) {
            if (segment.isHr) {
                continue;
            }
            const float segmentWidth = CalcSegmentWidth(segment, device, imageRoot);
            if (segmentWidth <= 0.0f && !segment.image && TrimAscii(SegmentPlainText(segment)).empty()) {
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
        const ImVec2 size = ResolveImageRenderSize(image, device, imageRoot);
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
        const MarkupRenderer::DrawOptions& options) {
        ImVec4 textColor = segment.hasColor ? segment.color : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        textColor.w *= segment.alpha;
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        const float previousScale = 1.0f;
        if (segment.fontSize > 0) {
            ImGui::SetWindowFontScale(segment.fontSize / 16.0f);
        }

        ImVec2 drawnSize{};
        if (segment.image) {
            drawnSize = DrawImageSegment(*segment.image, device, imageRoot, contentOrigin);
        } else {
            const std::string text = SegmentPlainText(segment);
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const ImVec2 textSize = ImGui::CalcTextSize(text.empty() ? " " : text.c_str());
            if (segment.hasBgColor && !text.empty()) {
                ImVec4 bg = segment.bgColor;
                bg.w *= segment.alpha;
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(start.x - ScaleUi(3.0f), start.y - ScaleUi(2.0f)),
                    ImVec2(start.x + textSize.x + ScaleUi(3.0f), start.y + textSize.y + ScaleUi(2.0f)),
                    ImGui::GetColorU32(bg),
                    ScaleUi(3.0f));
            }
            if (options.wrapText) {
                ImGui::TextWrapped("%s", text.c_str());
            } else {
                ImGui::TextUnformatted(text.empty() ? " " : text.c_str());
            }
            drawnSize = ImGui::GetItemRectSize();
        }

        if (segment.fontSize > 0) {
            ImGui::SetWindowFontScale(previousScale);
        }
        ImGui::PopStyleColor();
        return drawnSize;
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
        const float lineWidth = CalcLineWidth(segments, device, imageRoot);
        float maxHeight = ImGui::GetTextLineHeightWithSpacing();
        bool hasDrawn = false;

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
            maxHeight = std::max(maxHeight, drawnSize.y + ImGui::GetStyle().ItemSpacing.y);
            hasDrawn = true;
        }

        if (!hasDrawn) {
            ImGui::TextUnformatted("");
        }
        const float currentY = ImGui::GetCursorPosY();
        const float targetY = lineStartY + maxHeight;
        if (targetY > currentY) {
            ImGui::Dummy(ImVec2(0.0f, targetY - currentY));
        }
        for (int i = 0; i < segments.front().extraBreaks; ++i) {
            ImGui::TextUnformatted("");
        }
    }

    void DrawText(
        std::string_view text,
        IDirect3DDevice9* device,
        const fs::path& imageRoot,
        const MarkupRenderer::DrawOptions& options) {
        const ImVec2 contentOrigin = ImGui::GetCursorScreenPos();
        const auto lines = ParseRichText(text);
        for (const std::vector<RichSegment>& line : lines) {
            DrawRichLine(line, device, imageRoot, contentOrigin, options);
        }
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
    const std::vector<RichSegment> segments = SplitSegmentByInlineColors(ParseRichSegment(line));
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
