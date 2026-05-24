#pragma once

#include <d3d9.h>
#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

class MarkupRenderer {
public:
    struct DrawOptions {
        bool wrapText = true;
    };

    MarkupRenderer();
    ~MarkupRenderer();

    MarkupRenderer(const MarkupRenderer&) = delete;
    MarkupRenderer& operator=(const MarkupRenderer&) = delete;
    MarkupRenderer(MarkupRenderer&&) noexcept;
    MarkupRenderer& operator=(MarkupRenderer&&) noexcept;

    void ReleaseDeviceResources();
    void DrawText(std::string_view text, IDirect3DDevice9* device, const std::filesystem::path& imageRoot);
    void DrawText(
        std::string_view text,
        IDirect3DDevice9* device,
        const std::filesystem::path& imageRoot,
        const DrawOptions& options);

    static bool HasVisibleContent(std::string_view text);
    static std::string StripMarkupLine(std::string_view line);
    static std::string StripMarkup(std::string_view text);
    static bool IsSafeRelativeAssetPath(std::string_view path);
    static std::wstring Utf8ToWide(std::string_view text);
    static std::string WideToUtf8(std::wstring_view text);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
