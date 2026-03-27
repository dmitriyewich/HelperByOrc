#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <memory>

class SampApi;
class SampHooks;
class SampRakHooks;

class BinderModule {
public:
    BinderModule();
    ~BinderModule();

    BinderModule(const BinderModule&) = delete;
    BinderModule& operator=(const BinderModule&) = delete;
    BinderModule(BinderModule&&) noexcept;
    BinderModule& operator=(BinderModule&&) noexcept;

    void OnProcessAttach(HMODULE module);
    void SetSampApi(SampApi* sampApi);
    void SetSampHooks(SampHooks* sampHooks);
    void SetSampRakHooks(SampRakHooks* sampRakHooks);

    void Tick();
    void Shutdown();

    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool WantsOverlayRender() const;
    bool WantsInputCapture() const;
    bool WantsQuickMenuCursor() const;

    void DrawMainTab();
    void DrawSettingsSection();
    void DrawOverlay();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
