#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d9.h>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class TagsModule;

class NotepadModule {
public:
    struct NoteSummary {
        std::string id{};
        std::string title{};
        std::string folderPath{};
    };

    struct NoteContent {
        std::string id{};
        std::string title{};
        std::string folderPath{};
        std::string text{};
    };

    struct RenderStats {
        int folders = 0;
        int notes = 0;
        bool editing = false;
        bool copyLineMode = false;
        bool applyTags = false;
        std::size_t renderedBytes = 0;
        int previewCacheHits = 0;
        int previewCacheMisses = 0;
        int previewLines = 0;
        int previewDrawnLines = 0;
        int previewSkippedLines = 0;
        int previewCachedLines = 0;
        int copyLinesTotal = 0;
        int copyLinesVisible = 0;
        double totalMs = 0.0;
        double loadMs = 0.0;
        double shortcutsMs = 0.0;
        double leftPanelMs = 0.0;
        double rightPanelMs = 0.0;
        double readPreviewMs = 0.0;
        double editPreviewMs = 0.0;
        double copyLinesMs = 0.0;
        double tagsMs = 0.0;
        double drawPreviewMs = 0.0;
        double modalsMs = 0.0;
    };

    NotepadModule();
    ~NotepadModule();

    NotepadModule(const NotepadModule&) = delete;
    NotepadModule& operator=(const NotepadModule&) = delete;
    NotepadModule(NotepadModule&&) noexcept;
    NotepadModule& operator=(NotepadModule&&) noexcept;

    void OnProcessAttach(HMODULE module);
    void Shutdown();
    void ReloadConfig();
    void OnProfileChanged();
    bool FlushPendingEdits();
    void ReleaseDeviceResources();
    void SetTagsModule(TagsModule* tagsModule);
    void DrawMainTab(IDirect3DDevice9* device);
    RenderStats LastRenderStats() const;
    bool TryGetNote(std::string_view id, NoteContent& out);
    std::vector<NoteSummary> NoteSummaries();
    std::filesystem::path ImagesDirectoryPath();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
