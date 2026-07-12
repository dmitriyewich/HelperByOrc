#include "binder_module_impl.h"

BinderModule::BinderModule() : impl_(std::make_unique<Impl>()) {
}

BinderModule::~BinderModule() = default;

BinderModule::BinderModule(BinderModule&&) noexcept = default;
BinderModule& BinderModule::operator=(BinderModule&&) noexcept = default;

void BinderModule::OnProcessAttach(HMODULE module) {
    impl_->OnProcessAttach(module);
}

void BinderModule::SetSampApi(SampApi* sampApi) {
    impl_->SetSampApi(sampApi);
}

void BinderModule::SetSampHooks(SampHooks* sampHooks) {
    impl_->SetSampHooks(sampHooks);
}

void BinderModule::SetSampRakHooks(SampRakHooks* sampRakHooks) {
    impl_->SetSampRakHooks(sampRakHooks);
}

void BinderModule::SetIncomingMessageRouter(IncomingMessageRouter* incomingMessageRouter) {
    impl_->SetIncomingMessageRouter(incomingMessageRouter);
}

void BinderModule::SetNotificationManager(NotificationManager* notificationManager) {
    impl_->SetNotificationManager(notificationManager);
}

void BinderModule::SetTagsModule(TagsModule* tagsModule) {
    impl_->SetTagsModule(tagsModule);
}

void BinderModule::Tick() {
    impl_->Tick();
}

void BinderModule::SetGameInputForeground(bool gameWindowForeground) {
    impl_->gameInputForeground_ = gameWindowForeground;
}

void BinderModule::SetHelperUiActive(bool helperUiActive) {
    impl_->helperUiActive_ = helperUiActive;
}

void BinderModule::Shutdown() {
    impl_->Shutdown();
}

void BinderModule::ReloadConfig() {
    impl_->ReloadConfig();
}

std::string BinderModule::GetThisbindTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThisbindTagValue(runtimeId);
}

std::string BinderModule::GetThisbindNameTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThisbindNameTagValue(runtimeId);
}

std::string BinderModule::GetThisbindFolderTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThisbindFolderTagValue(runtimeId);
}

std::string BinderModule::GetThiscategoryTagValue(std::uint64_t runtimeId) const {
    return impl_->BuildThiscategoryTagValue(runtimeId);
}

binder_tags::Catalog BinderModule::GetBindSelectorCatalog() const {
    return impl_->BuildBindSelectorCatalog();
}

bool BinderModule::IsRuntimeActive(std::uint64_t runtimeId) const {
    return impl_->IsRuntimeActive(runtimeId);
}

bool BinderModule::IsRuntimePaused(std::uint64_t runtimeId) const {
    return impl_->IsRuntimePaused(runtimeId);
}

bool BinderModule::PauseRuntime(std::uint64_t runtimeId) {
    return impl_->PauseRuntime(runtimeId);
}

bool BinderModule::ResumeRuntime(std::uint64_t runtimeId) {
    return impl_->ResumeRuntime(runtimeId);
}

bool BinderModule::StopRuntime(std::uint64_t runtimeId) {
    return impl_->StopRuntime(runtimeId);
}

BinderModule::TagActionResult BinderModule::ExecuteTagAction(
    std::string_view action,
    std::string_view param,
    std::uint64_t sourceRuntimeId) {
    return impl_->ExecuteTagAction(action, param, sourceRuntimeId);
}

bool BinderModule::OnWindowMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    return impl_->OnWindowMessage(message, wparam, lparam);
}

bool BinderModule::WantsOverlayRender() const {
    return impl_->WantsOverlayRender();
}

bool BinderModule::WantsInputCapture() const {
    return impl_->WantsInputCapture();
}

bool BinderModule::WantsInputRouting() const {
    return impl_->WantsInputRouting();
}

bool BinderModule::IsQuickMenuOpen() const {
    return impl_->IsQuickMenuOpen();
}

bool BinderModule::DescribeMainWindowHotkeyConflict(const std::vector<unsigned int>& keys, std::string& description) {
    return impl_->DescribeMainWindowHotkeyConflict(keys, description);
}

void BinderModule::DrawMainTab() {
    impl_->DrawMainTab();
}

std::string BinderModule::QuickMenuHotkeyText() const {
    return impl_->QuickMenuHotkeyText();
}

void BinderModule::DrawSettingsSection(bool includeHeader) {
    impl_->DrawSettingsSection(includeHeader);
}

void BinderModule::DrawBinderSettingsSection(bool includeHeader) {
    impl_->DrawBinderSettingsSection(includeHeader);
}

void BinderModule::DrawOverlay() {
    impl_->DrawOverlay();
}
