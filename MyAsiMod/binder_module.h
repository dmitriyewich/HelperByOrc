#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class SampApi;
class SampHooks;
class SampRakHooks;
class TagsModule;

class BinderModule {
public:
    struct TagActionResult {
        bool success = false;
        std::string value{};
        std::string error{};
        int affected = 0;
    };

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
    void SetTagsModule(TagsModule* tagsModule);

    void Tick();
    void Shutdown();

    std::string GetThisbindTagValue(std::uint64_t runtimeId) const;
    bool IsRuntimeActive(std::uint64_t runtimeId) const;
    bool IsRuntimePaused(std::uint64_t runtimeId) const;
    bool PauseRuntime(std::uint64_t runtimeId);
    bool ResumeRuntime(std::uint64_t runtimeId);
    bool StopRuntime(std::uint64_t runtimeId);
    TagActionResult ExecuteTagAction(std::string_view action, std::string_view param, std::uint64_t sourceRuntimeId);
    void ShowToast(std::string_view text, bool error = false, double durationMs = 2500.0);

    bool OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam);
    bool WantsOverlayRender() const;
    bool WantsInputCapture() const;
    bool WantsQuickMenuCursor() const;
    bool DescribeMainWindowHotkeyConflict(const std::vector<unsigned int>& keys, std::string& description);

    void DrawMainTab();
    void DrawSettingsSection();
    void DrawOverlay();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
