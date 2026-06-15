#include "tags_module.h"

#include "tags_module_impl.h"

#include <memory>
#include <utility>

TagsModule::TagsModule()
    : impl_(std::make_unique<Impl>()) {}

TagsModule::~TagsModule() = default;

TagsModule::TagsModule(TagsModule&&) noexcept = default;

TagsModule& TagsModule::operator=(TagsModule&&) noexcept = default;

void TagsModule::OnProcessAttach() {
    impl_->OnProcessAttach();
}

void TagsModule::Shutdown() {
    impl_->Shutdown();
}

void TagsModule::ReloadConfig() {
    impl_->ReloadConfig();
}

void TagsModule::SetSampApi(SampApi* sampApi) {
    impl_->SetSampApi(sampApi);
}

void TagsModule::SetBinderModule(BinderModule* binderModule) {
    impl_->SetBinderModule(binderModule);
}

void TagsModule::SetNotificationManager(NotificationManager* notificationManager) {
    impl_->SetNotificationManager(notificationManager);
}

void TagsModule::SetArizonaCefDialogs(ArizonaCefDialogs* arizonaCefDialogs) {
    impl_->SetArizonaCefDialogs(arizonaCefDialogs);
}

void TagsModule::PushContext(const EvaluationContext& context) const {
    impl_->PushContext(context);
}

void TagsModule::PopContext() const {
    impl_->PopContext();
}

std::optional<int> TagsModule::ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const {
    return impl_->ConsumePendingBindDelayOverride(runtimeId);
}

bool TagsModule::ConsumeCurrentDispatchBlocked(std::uint64_t runtimeId) const {
    return impl_->ConsumeCurrentDispatchBlocked(runtimeId);
}

void TagsModule::Tick() {
    impl_->Tick();
}

bool TagsModule::IsMiscHomePage() const {
    return impl_->IsMiscHomePage();
}

void TagsModule::DrawMiscTab() {
    impl_->DrawMiscTab();
}

std::string TagsModule::ExpandText(std::string_view text) const {
    return impl_->ExpandText(text);
}

std::string TagsModule::ExpandText(std::string_view text, const EvaluationContext& context) const {
    return impl_->ExpandText(text, context);
}

std::string TagsModule::ExpandHudText(std::string_view text) const {
    return impl_->ExpandHudText(text);
}

std::string TagsModule::ExpandOutgoingText(
    std::string_view text,
    std::string_view activationSource,
    std::string_view activationText) const {
    return impl_->ExpandOutgoingText(text, activationSource, activationText);
}

const std::vector<TagsModule::CatalogEntry>& TagsModule::CatalogEntries() const {
    return impl_->CatalogEntries();
}

const std::vector<std::pair<std::string, std::string>>& TagsModule::CustomVariables() const {
    return impl_->CustomVariables();
}

const std::vector<TagsModule::VirtualKeyPickerEntry>& TagsModule::VirtualKeyPickerEntries() const {
    return impl_->VirtualKeyPickerEntries();
}

std::string TagsModule::MakeKeyEmulateToken(unsigned int keyCode) {
    return Impl::MakeKeyEmulateToken(keyCode);
}

void TagsModule::OpenKeyEmulatePicker() {
    impl_->OpenKeyEmulatePicker();
}

void TagsModule::OpenDialogItemPicker() {
    impl_->OpenDialogItemPicker();
}

void TagsModule::OpenSampDialogTextPicker() {
    impl_->OpenSampDialogTextPicker();
}

void TagsModule::OpenArizonaDialogTextPicker() {
    impl_->OpenArizonaDialogTextPicker();
}

void TagsModule::DrawVariableHelperPopups(std::function<void(std::string_view)> tokenAction) {
    impl_->DrawVariableHelperPopups(std::move(tokenAction));
}

std::vector<variables_picker::Entry> TagsModule::BuildVariablePickerEntriesForInsert() const {
    return impl_->BuildVariablePickerEntriesForInsert();
}

void TagsModule::HandleVariablePickerUtilityRequest(const variables_picker::Request& request) {
    impl_->HandleVariablePickerUtilityRequest(request);
}
