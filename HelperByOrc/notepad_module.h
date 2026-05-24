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

    NotepadModule();
    ~NotepadModule();

    NotepadModule(const NotepadModule&) = delete;
    NotepadModule& operator=(const NotepadModule&) = delete;
    NotepadModule(NotepadModule&&) noexcept;
    NotepadModule& operator=(NotepadModule&&) noexcept;

    void OnProcessAttach(HMODULE module);
    void Shutdown();
    void ReloadConfig();
    void FlushPendingEdits();
    void ReleaseDeviceResources();
    void SetTagsModule(TagsModule* tagsModule);
    void DrawMainTab(IDirect3DDevice9* device);
    bool TryGetNote(std::string_view id, NoteContent& out);
    std::vector<NoteSummary> NoteSummaries();
    std::filesystem::path ImagesDirectoryPath();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
