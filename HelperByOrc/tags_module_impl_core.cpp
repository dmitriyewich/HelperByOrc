#include "tags_module_impl.h"
#include "tags_module_detail.h"
#include "samp_rak_hooks.h"

TagsModule::Impl::Impl() = default;

const std::vector<TagsModule::Impl::CatalogEntry>& TagsModule::Impl::CatalogEntries() const {
    EnsureCodeCatalogEntries();
    return catalogEntries_;
}

const std::vector<std::pair<std::string, std::string>>& TagsModule::Impl::CustomVariables() const {
    return customVariables_;
}

const std::vector<TagsModule::Impl::VirtualKeyPickerEntry>& TagsModule::Impl::VirtualKeyPickerEntries() const {
    return GetVirtualKeyPickerEntries();
}

std::string TagsModule::Impl::MakeKeyEmulateToken(unsigned int keyCode) {
    return MakeKeyEmulateTokenImpl(static_cast<UINT>(keyCode));
}

void TagsModule::Impl::OnProcessAttach() {
    debuglog::WriteInfo("TagsModule::OnProcessAttach begin");
    InitializeRegistry();
    LoadConfig();
    RefreshCodeVariableReservedNames();
    codevars::Runtime::Instance().OnProcessAttach();
    EnsureCodeCatalogEntries();
    LoadTransliterationDictionary();
    ResetTargetTracker();
    if (sampRakHooks_ && !sampRakHandlerAttached_) {
        sampRakHooks_->AddOnSendRpcHandler(
            [this](std::uint8_t rpcId, RakNetBitStreamView& view) {
                return HandleOutgoingDamageRpc(rpcId, view);
            });
        sampRakHandlerAttached_ = true;
        debuglog::WriteInfo("[tags][damage] outgoing RPC observer attached");
    }
    currentPage_ = MiscPage::Home;
    debuglog::WriteInfo(
        "TagsModule::OnProcessAttach done tags=%llu customVars=%llu",
        static_cast<unsigned long long>(tagRegistry_.Entries().size()),
        static_cast<unsigned long long>(customVariables_.size()));
}

void TagsModule::Impl::Shutdown() {
    debuglog::WriteInfo("TagsModule::Shutdown begin");
    if (!reloadingConfig_) {
        codevars::Runtime::Instance().Shutdown();
    }
    for (ActiveVirtualKeyHold& hold : activeVirtualKeyHolds_) {
        ReleaseVirtualKeyHold(hold);
    }
    activeVirtualKeyHolds_.clear();
    pendingBindDelayOverrides_.clear();
    pendingKeyHoldWaits_.clear();
    pendingArzDialogQueryWaits_.clear();
    readyArzDialogQueries_.clear();
    blockedCurrentDispatchRuntimes_.clear();
    skippedCurrentDispatchRuntimes_.clear();
    skipEmptyCurrentDispatchRuntimes_.clear();
    waitIfRuntimeStates_.clear();
    pendingDialogWaits_.clear();
    arzDialogTransitionBaselines_.clear();
    clipboardCache_ = {};
    vehicleNameCache_.clear();
    customVariables_.clear();
    customVariableIndex_.clear();
    builtinCatalogEntries_.clear();
    catalogEntries_.clear();
    variablePickerEntriesCache_.clear();
    customVariablesRevision_ = 0;
    codeCatalogRevision_ = 0;
    variablePickerEntriesCatalogRevision_ = 0;
    variablePickerEntriesCustomRevision_ = 0;
    variablePickerEntriesCodeRevision_ = 0;
    externalTagPerfStats_.send.store(0, std::memory_order_relaxed);
    externalTagPerfStats_.local.store(0, std::memory_order_relaxed);
    externalTagPerfStats_.changed.store(0, std::memory_order_relaxed);
    externalTagPerfStats_.suppressed.store(0, std::memory_order_relaxed);
    externalTagPerfStats_.bypassed.store(0, std::memory_order_relaxed);
    externalTagPerfStats_.failed.store(0, std::memory_order_relaxed);
    externalTagPerfStats_.windowStartMs = 0;
    externalTagPerfStats_.lastFailureLogAtMs.store(0, std::memory_order_relaxed);
    variablesPickerState_ = {};
    dialogItemPickerSearchQuery_.clear();
    dialogTextPickerSearchQuery_.clear();
    dialogItemPickerArizonaQueryStarted_ = false;
    ResetTargetTracker();
    currentPage_ = MiscPage::Home;
    g_activeContextStack.clear();
    debuglog::WriteInfo("TagsModule::Shutdown done");
}

void TagsModule::Impl::ReloadConfig() {
    debuglog::WriteInfo("TagsModule::ReloadConfig begin");
    reloadingConfig_ = true;
    Shutdown();
    reloadingConfig_ = false;
    InitializeRegistry();
    LoadConfig();
    RefreshCodeVariableReservedNames();
    EnsureCodeCatalogEntries();
    LoadTransliterationDictionary();
    debuglog::WriteInfo(
        "TagsModule::ReloadConfig done tags=%llu customVars=%llu",
        static_cast<unsigned long long>(tagRegistry_.Entries().size()),
        static_cast<unsigned long long>(customVariables_.size()));
}

void TagsModule::Impl::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
    debuglog::WriteInfo("TagsModule::SetSampApi assigned=%d", sampApi_ ? 1 : 0);
}

void TagsModule::Impl::SetSampRakHooks(SampRakHooks* sampRakHooks) {
    sampRakHooks_ = sampRakHooks;
    debuglog::WriteInfo("TagsModule::SetSampRakHooks assigned=%d", sampRakHooks_ ? 1 : 0);
}

void TagsModule::Impl::SetBinderModule(BinderModule* binderModule) {
    binderModule_ = binderModule;
    debuglog::WriteInfo("TagsModule::SetBinderModule assigned=%d", binderModule_ ? 1 : 0);
}

void TagsModule::Impl::SetNotificationManager(NotificationManager* notificationManager) {
    notificationManager_ = notificationManager;
    debuglog::WriteInfo("TagsModule::SetNotificationManager assigned=%d", notificationManager_ ? 1 : 0);
}

void TagsModule::Impl::SetArizonaCefDialogs(ArizonaCefDialogs* arizonaCefDialogs) {
    arizonaCefDialogs_ = arizonaCefDialogs;
    debuglog::WriteInfo("TagsModule::SetArizonaCefDialogs assigned=%d", arizonaCefDialogs_ ? 1 : 0);
}

void TagsModule::Impl::NotifyTagError(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::TagErrors, NotificationSeverity::Error, text, durationMs);
}

void TagsModule::Impl::NotifyDialogError(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::SampDialogErrors, NotificationSeverity::Error, text, durationMs);
}

void TagsModule::Impl::NotifyTagSuccess(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::TagSuccess, NotificationSeverity::Success, text, durationMs);
}

void TagsModule::Impl::NotifyClipboardSuccess(std::string_view text, double durationMs) const {
    if (!notificationManager_ || text.empty()) {
        return;
    }

    notificationManager_->Notify(NotificationGroup::ClipboardSuccess, NotificationSeverity::Success, text, durationMs);
}
