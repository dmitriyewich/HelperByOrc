#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <d3d9.h>

#include <memory>

class NotepadModule;
class SampApi;
class TagsModule;

class HudModule {
public:
    HudModule();
    ~HudModule();

    HudModule(const HudModule&) = delete;
    HudModule& operator=(const HudModule&) = delete;
    HudModule(HudModule&&) noexcept;
    HudModule& operator=(HudModule&&) noexcept;

    void OnProcessAttach(HMODULE module);
    void Shutdown();
    void ReloadConfig();
    void ReleaseDeviceResources();
    void SetTagsModule(TagsModule* tagsModule);
    void SetNotepadModule(NotepadModule* notepadModule);
    void SetSampApi(SampApi* sampApi);
    void SetPlacementInputBlocked(bool blocked);

    void DrawMainTab(IDirect3DDevice9* device);
    void DrawOverlay(IDirect3DDevice9* device, bool helperWindowOpen);
    bool WantsOverlayRender();
    bool WantsInputCapture() const;
    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
