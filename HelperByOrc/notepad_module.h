#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d9.h>

#include <memory>

class TagsModule;

class NotepadModule {
public:
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
