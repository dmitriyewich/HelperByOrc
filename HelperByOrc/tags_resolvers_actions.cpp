#include "tags_module_impl.h"
#include "tags_module_detail.h"

std::optional<std::string> TagsModule::Impl::ResolveBuiltinScreenTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    const ScreenCaptureResult result = CaptureGameScreenshot({});
    if (!result.Ok() && binderModule_) {
        NotifyTagError(DescribeScreenCaptureError(result.error, result.detail), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTPhotoTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    if (!TakeGameCameraPhoto() && binderModule_) {
        NotifyTagError(UiSettings::Instance().Text(UiText::ToastTPhotoFailed), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinChatClearTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->ClearChatLocal()) {
        return std::string();
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinKeyEmulateFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> keyCode = ParseInteger(param);
    if (!keyCode.has_value() || *keyCode < 1 || *keyCode > 0xFF) {
        return std::string();
    }

    if (context.allowSideEffects) {
        QueueVirtualKeyHold(
            static_cast<UINT>(*keyCode),
            kKeyEmulateStartDelayMs,
            kKeyEmulateTapMs);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinKeyDownFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(param, ';');
    if (parts.size() != 2) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastKeyDownInvalidFormat), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> keyCode = ParseInteger(parts[0]);
    if (!keyCode.has_value() || *keyCode < 1 || *keyCode > 0xFF) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastKeyDownInvalidKey), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> durationMs = ParseInteger(parts[1]);
    if (!durationMs.has_value() || *durationMs < 1) {
        if (context.allowSideEffects && binderModule_) {
            NotifyTagError(UiSettings::Instance().Text(UiText::ToastKeyDownInvalidDuration), 2800.0);
        }
        return std::string();
    }

    if (context.allowSideEffects) {
        const std::uint64_t releaseAtMs =
            QueueVirtualKeyHold(static_cast<UINT>(*keyCode), 0, *durationMs);
        if (releaseAtMs != 0 && context.runningBindRuntimeId != 0 && binderModule_) {
            binderModule_->PauseRuntime(context.runningBindRuntimeId);
            QueuePendingKeyHoldWait(context.runningBindRuntimeId, static_cast<unsigned int>(*keyCode), releaseAtMs);
        }
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinScreenFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    std::string folder = Unquote(Trim(param));
    const ScreenCaptureResult result = CaptureGameScreenshot(folder);
    if (!result.Ok() && binderModule_) {
        NotifyTagError(DescribeScreenCaptureError(result.error, result.detail), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinWaitFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> delayMs = ParseInteger(param);
    if (!delayMs.has_value() || *delayMs < 0) {
        return std::string();
    }

    if (context.allowSideEffects && context.runningBindRuntimeId != 0) {
        QueuePendingBindDelayOverride(context.runningBindRuntimeId, *delayMs);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBinderActionFunctionTag(
    std::string_view action,
    std::string_view param,
    const EvaluationContext& context) const {
    const bool isPureCheck = action == "ended";
    if (!binderModule_) {
        return isPureCheck ? std::string("0") : std::string();
    }
    if (context.runningBindRuntimeId == 0) {
        return isPureCheck ? std::string("0") : std::string();
    }
    if (!context.allowSideEffects && !isPureCheck) {
        return std::string();
    }

    const BinderModule::TagActionResult result =
        binderModule_->ExecuteTagAction(action, param, context.runningBindRuntimeId);
    if (isPureCheck) {
        return result.value.empty() ? std::string("0") : result.value;
    }
    return result.value;
}
