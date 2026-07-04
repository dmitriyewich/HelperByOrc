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
    struct OverlayStats {
        int widgets = 0;
        int enabledWidgets = 0;
        int visibleWidgets = 0;
        int refreshEveryFrameWidgets = 0;
        int elements = 0;
        int visibleElements = 0;
        int refreshedWidgets = 0;
        int expandedElements = 0;
        int staticRefreshSkips = 0;
    };

    struct EditorStats {
        int widgets = 0;
        int elements = 0;
        int selectedElements = 0;
        double totalMs = 0.0;
        double loadMs = 0.0;
        double beginFrameMs = 0.0;
        double toolbarMs = 0.0;
        double workspaceMs = 0.0;
        double widgetListMs = 0.0;
        double layersMs = 0.0;
        double canvasMs = 0.0;
        double inspectorMs = 0.0;
        double variablesPopupMs = 0.0;
    };

    HudModule();
    ~HudModule();

    HudModule(const HudModule&) = delete;
    HudModule& operator=(const HudModule&) = delete;
    HudModule(HudModule&&) noexcept;
    HudModule& operator=(HudModule&&) noexcept;

    void OnProcessAttach(HMODULE module);
    void Shutdown();
    void ReloadConfig();
    void FlushPendingSaves();
    void ReleaseDeviceResources();
    void SetTagsModule(TagsModule* tagsModule);
    void SetNotepadModule(NotepadModule* notepadModule);
    void SetSampApi(SampApi* sampApi);
    void SetPlacementInputBlocked(bool blocked);

    void DrawMainTab(IDirect3DDevice9* device);
    void DrawOverlay(IDirect3DDevice9* device);
    OverlayStats LastOverlayStats() const;
    EditorStats LastEditorStats() const;
    bool WantsOverlayRender();
    bool WantsInputCapture() const;
    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
