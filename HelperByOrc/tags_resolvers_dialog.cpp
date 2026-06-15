#include "tags_module_impl.h"
#include "tags_module_detail.h"

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogActiveTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string("false");
    }

    return sampApi->isDialogActive() ? std::string("true") : std::string("false");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogCaptionTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    return NormalizeDialogCaptionVisibleText(sampApi->get_dialog_caption());
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogGetSelectedItemTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const SampApi::DialogSelectionText selection = sampApi->getDialogSelectedItemText();
    if (!selection.found) {
        return std::string();
    }

    return NormalizeDialogVisibleText(selection.text);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogEditboxTextTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    if (style < 0 || !sampApi->isDialogInputStyle(style) || !sampApi->pDialogInput_pEditBox_active_func()) {
        return std::string();
    }

    return sampApi->sampGetDialogEditboxText();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogSelectedIndexTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    if (style < 0 || !sampApi->isDialogListStyle(style)) {
        return std::string();
    }

    const int selectedIndex = sampApi->GetCurrentDialogListItem();
    if (selectedIndex < 0) {
        return std::string();
    }

    return std::to_string(selectedIndex);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogWaitOpenTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool dialogActive = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion() && sampApi->isDialogActive();
    if (dialogActive) {
        return std::string();
    }

    binderModule_->PauseRuntime(context.runningBindRuntimeId);
    const std::uint64_t deadlineAtMs = GetTickCount64() + kDialogWaitOpenTimeoutMs;
    const_cast<Impl*>(this)->QueuePendingDialogWait(
        context.runningBindRuntimeId,
        PendingDialogWaitKind::Open,
        deadlineAtMs);
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogWaitCloseTag(const EvaluationContext& context) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool dialogActive = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion() && sampApi->isDialogActive();
    if (!dialogActive) {
        return std::string();
    }

    binderModule_->PauseRuntime(context.runningBindRuntimeId);
    const_cast<Impl*>(this)->QueuePendingDialogWait(
        context.runningBindRuntimeId,
        PendingDialogWaitKind::Close,
        0);
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogGetIdTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        return std::string();
    }

    const int dialogId = sampApi->SAMP_DIALOG_ID();
    if (dialogId < 0) {
        return std::string();
    }

    return std::to_string(dialogId);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetInputTextTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->CachedInputText() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetListItemTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->CachedListItem() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogIsDialogActiveTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ && arizonaCefDialogs_->IsDialogActive() ? std::string("true") : std::string("false");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetIdTag(const EvaluationContext&) const {
    if (!arizonaCefDialogs_) {
        return std::string();
    }
    const int id = arizonaCefDialogs_->LastDialogId();
    return id < 0 ? std::string() : std::to_string(id);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetStyleTag(const EvaluationContext&) const {
    if (!arizonaCefDialogs_) {
        return std::string();
    }
    const int style = arizonaCefDialogs_->LastDialogStyle();
    return style < 0 ? std::string() : std::to_string(style);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetTitleTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->LastDialogTitle() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetButton1Tag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->LastDialogButton1() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetButton2Tag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->LastDialogButton2() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetDialogTextTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->LastDialogText() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetDialogTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!arizonaCefDialogs_ || arizonaCefDialogs_->LastDialogId() < 0) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogTextNoCached), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogTextEmptyParam), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> index = ParseInteger(rawValue);
    if (!index.has_value() || *index < 0) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogTextInvalidIndex), 2800.0);
        }
        return std::string();
    }

    const DialogTextItems items = CollectDialogTextItems(arizonaCefDialogs_->LastDialogText());
    if (*index >= static_cast<int>(items.flat.size())) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(
                    UiText::ToastArzDialogTextOutOfRange,
                    std::to_string(*index).c_str(),
                    std::to_string(items.flat.empty() ? 0 : static_cast<int>(items.flat.size() - 1)).c_str()),
                2800.0);
        }
        return std::string();
    }

    return items.flat[static_cast<std::size_t>(*index)].text;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetRespondTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->LastRespondJoined() : std::string("-1;-1;-1;");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogRespondIdTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? std::to_string(arizonaCefDialogs_->LastRespondId()) : std::string("-1");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogRespondButtonTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? std::to_string(arizonaCefDialogs_->LastRespondButton()) : std::string("-1");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogRespondListTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? std::to_string(arizonaCefDialogs_->LastRespondList()) : std::string("-1");
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogRespondInputTag(const EvaluationContext&) const {
    return arizonaCefDialogs_ ? arizonaCefDialogs_->LastRespondInput() : std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogCloseFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogCloseNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    const std::optional<int> button = ParseInteger(rawValue);
    if (!button.has_value() || (*button != 0 && *button != 1)) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogCloseInvalidButton), 2800.0);
        }
        return std::string();
    }

    const SampApi::DialogSubmitResult result = sampApi->submitCurrentDialog(*button);
    if (!result.ok && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogCloseFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogSetTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSetTextNoActive), 2800.0);
        }
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    if (style < 0 || !sampApi->isDialogInputStyle(style) || !sampApi->pDialogInput_pEditBox_active_func()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSetTextNoEditbox), 2800.0);
        }
        return std::string();
    }

    const std::string text = Unquote(std::string(param));
    if (!sampApi->sampSetDialogEditboxText(text, false) && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSetTextFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogWaitIdFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects || context.runningBindRuntimeId == 0 || !binderModule_) {
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    const std::optional<int> dialogId = ParseInteger(rawValue);
    if (!dialogId.has_value() || *dialogId < 0) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogWaitIdInvalidId), 2800.0);
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    const bool dialogMatches = sampApi && sampApi->sampModule() && sampApi->isSupportedVersion() && sampApi->isDialogActive()
        && sampApi->SAMP_DIALOG_ID() == *dialogId;
    if (dialogMatches) {
        return std::string();
    }

    binderModule_->PauseRuntime(context.runningBindRuntimeId);
    const_cast<Impl*>(this)->QueuePendingDialogWait(
        context.runningBindRuntimeId,
        PendingDialogWaitKind::SpecificId,
        GetTickCount64() + kDialogWaitOpenTimeoutMs,
        *dialogId);
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogItemFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemEmptyParam), 2800.0);
        }
        return std::string();
    }

    std::string error;
    const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi, error);
    if (!items.has_value()) {
        if (binderModule_) {
            const UiText textId = error == "not_list" ? UiText::ToastDialogItemNotList : UiText::ToastDialogItemReadFailed;
            NotifyDialogError(UiSettings::Instance().Text(textId), 2800.0);
        }
        return std::string();
    }

    int targetIndex = -1;
    if (const std::optional<int> parsed = ParseInteger(rawValue); parsed.has_value()) {
        targetIndex = *parsed >= 1 ? (*parsed - 1) : *parsed;
    } else if (const std::optional<int> foundIndex = FindDialogItemIndexByText(*items, rawValue); foundIndex.has_value()) {
        targetIndex = *foundIndex;
    } else {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemNotFound), 2800.0);
        }
        return std::string();
    }

    if (targetIndex < 0 || targetIndex >= static_cast<int>(items->items.size())) {
        if (binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(UiText::ToastDialogItemOutOfRange, std::to_string(targetIndex + 1).c_str()),
                2800.0);
        }
        return std::string();
    }

    const SampApi::DialogSubmitResult result = sampApi->submitCurrentDialog(1, targetIndex);
    if (!result.ok && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogItemFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogSelectFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectEmptyParam), 2800.0);
        }
        return std::string();
    }

    std::string error;
    const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi, error);
    if (!items.has_value()) {
        if (binderModule_) {
            const UiText textId = error == "not_list" ? UiText::ToastDialogSelectNotList : UiText::ToastDialogSelectReadFailed;
            NotifyDialogError(UiSettings::Instance().Text(textId), 2800.0);
        }
        return std::string();
    }

    int targetIndex = -1;
    if (const std::optional<int> parsed = ParseInteger(rawValue); parsed.has_value()) {
        targetIndex = *parsed >= 1 ? (*parsed - 1) : *parsed;
    } else if (const std::optional<int> foundIndex = FindDialogItemIndexByText(*items, rawValue); foundIndex.has_value()) {
        targetIndex = *foundIndex;
    } else {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectNotFound), 2800.0);
        }
        return std::string();
    }

    if (targetIndex < 0 || targetIndex >= static_cast<int>(items->items.size())) {
        if (binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(
                    UiText::ToastDialogSelectOutOfRange,
                    std::to_string(targetIndex + 1).c_str()),
                2800.0);
        }
        return std::string();
    }

    if (!sampApi->SetCurrentDialogListItem(targetIndex) && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogSelectFailed), 2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogResponseFunctionTag(
    std::string_view rawParam,
    const EvaluationContext& context,
    int depth) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseNoActive), 2800.0);
        }
        return std::string();
    }

    const DialogResponseParams parsed = ParseDialogResponseParams(rawParam);
    if (!parsed.valid) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseInvalidFormat), 3000.0);
        }
        return std::string();
    }

    EvaluationContext dataContext = context;
    dataContext.allowSideEffects = false;

    const std::string buttonValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.button, dataContext, depth + 1)));
    const std::optional<int> button = ParseInteger(buttonValue);
    if (!button.has_value() || (*button != 0 && *button != 1)) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseInvalidButton), 3000.0);
        }
        return std::string();
    }

    const int style = sampApi->GetCurrentDialogStyle();
    std::optional<int> listItem = std::nullopt;
    std::optional<std::string> inputText = std::nullopt;

    if (*button == 1 && parsed.hasItemPart && style >= 0 && sampApi->isDialogListStyle(style)) {
        const std::string itemValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.item, dataContext, depth + 1)));
        if (!itemValue.empty()) {
            std::string error;
            const std::optional<DialogListItems> items = ReadActiveDialogListItems(sampApi, error);
            if (!items.has_value()) {
                if (binderModule_) {
                    const UiText textId =
                        error == "not_list" ? UiText::ToastDialogResponseItemNotList : UiText::ToastDialogResponseReadFailed;
                    NotifyDialogError(UiSettings::Instance().Text(textId), 3000.0);
                }
                return std::string();
            }

            int targetIndex = -1;
            if (const std::optional<int> parsedIndex = ParseInteger(itemValue); parsedIndex.has_value()) {
                targetIndex = *parsedIndex >= 1 ? (*parsedIndex - 1) : *parsedIndex;
            } else if (const std::optional<int> foundIndex = FindDialogItemIndexByText(*items, itemValue); foundIndex.has_value()) {
                targetIndex = *foundIndex;
            } else {
                if (binderModule_) {
                    NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseItemNotFound), 3000.0);
                }
                return std::string();
            }

            if (targetIndex < 0 || targetIndex >= static_cast<int>(items->items.size())) {
                if (binderModule_) {
                    NotifyDialogError(
                        UiSettings::Instance().Format(
                            UiText::ToastDialogResponseItemOutOfRange,
                            std::to_string(targetIndex + 1).c_str()),
                        3000.0);
                }
                return std::string();
            }

            listItem = targetIndex;
        }
    }

    if (*button == 1 && parsed.hasTextPart && style >= 0 && sampApi->isDialogInputStyle(style)) {
        inputText = Unquote(TrimAscii(ExpandTextRecursive(parsed.text, dataContext, depth + 1)));
    }

    const SampApi::DialogSubmitResult result = sampApi->submitCurrentDialog(*button, listItem, inputText, false);
    if (!result.ok && binderModule_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogResponseFailed), 3000.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinDialogTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextNoActive), 2800.0);
        }
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    if (rawValue.empty()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextEmptyParam), 2800.0);
        }
        return std::string();
    }

    const std::optional<int> index = ParseInteger(rawValue);
    if (!index.has_value() || *index < 0) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextInvalidIndex), 2800.0);
        }
        return std::string();
    }

    std::string error;
    const std::optional<DialogTextItems> items = ReadActiveDialogTextItems(sampApi, error);
    if (!items.has_value()) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastDialogTextReadFailed), 2800.0);
        }
        return std::string();
    }

    if (*index >= static_cast<int>(items->flat.size())) {
        if (context.allowSideEffects && binderModule_) {
            NotifyDialogError(
                UiSettings::Instance().Format(
                    UiText::ToastDialogTextOutOfRange,
                    std::to_string(*index).c_str(),
                    std::to_string(items->flat.empty() ? 0 : static_cast<int>(items->flat.size() - 1)).c_str()),
                2800.0);
        }
        return std::string();
    }

    return items->flat[static_cast<std::size_t>(*index)].text;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinSaveDialogFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }

    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || !sampApi->isDialogActive()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogNoActive), 2800.0);
        }
        return std::string();
    }

    const std::optional<std::filesystem::path> helperDataPath = helper_paths::ResolveHelperDataDirectory();
    if (!helperDataPath.has_value()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogDocumentsUnavailable), 2800.0);
        }
        return std::string();
    }

    const std::filesystem::path targetDirectory = GetHelperSavedDialogsRoot(*helperDataPath);
    std::error_code directoryError;
    std::filesystem::create_directories(targetDirectory, directoryError);
    if (directoryError) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogCreateDirFailed), 2800.0);
        }
        return std::string();
    }

    const std::string caption = NormalizeDialogCaptionVisibleText(sampApi->get_dialog_caption());
    std::string desiredName = Unquote(Trim(param));
    if (desiredName.empty()) {
        desiredName = caption;
    }
    if (desiredName.empty()) {
        const int dialogId = sampApi->SAMP_DIALOG_ID();
        desiredName = dialogId >= 0 ? "dialog_" + std::to_string(dialogId) : std::string("dialog");
    }

    std::wstring stem = Utf8ToWide(desiredName);
    if (stem.empty()) {
        stem = L"dialog";
    }

    const std::filesystem::path targetPath = MakeUniqueTextFilePath(targetDirectory, std::move(stem));
    std::ofstream stream(targetPath, std::ios::binary);
    if (!stream.is_open()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogWriteFailed), 2800.0);
        }
        return std::string();
    }

    const int dialogId = sampApi->SAMP_DIALOG_ID();
    const int dialogStyle = sampApi->GetCurrentDialogStyle();
    const int selectedIndex = sampApi->GetCurrentDialogListItem();
    const SampApi::DialogSelectionText selection = sampApi->getDialogSelectedItemText();
    const std::string editboxText = sampApi->sampGetDialogEditboxText();
    const std::string dialogText = sampApi->sampGetDialogText();

    stream << "Caption: " << caption << "\r\n";
    stream << "Dialog ID: " << dialogId << "\r\n";
    stream << "Style: " << DialogStyleName(dialogStyle) << " (" << dialogStyle << ")\r\n";
    if (selectedIndex >= 0) {
        stream << "Selected Item Index: " << selectedIndex << "\r\n";
    }
    if (selection.found) {
        stream << "Selected Item Text: " << selection.text << "\r\n";
    }
    if (!editboxText.empty()) {
        stream << "Editbox Text: " << editboxText << "\r\n";
    }
    stream << "\r\n----- Dialog Text -----\r\n";
    stream << dialogText;
    stream.flush();

    if (!stream.good()) {
        if (binderModule_) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastSaveDialogWriteFailed), 2800.0);
        }
        return std::string();
    }

    if (binderModule_) {
        std::string savedPath = WideToMultiByte(targetPath.native(), CP_UTF8);
        if (savedPath.empty()) {
            savedPath = WideToMultiByte(targetPath.filename().native(), CP_UTF8);
        }
        NotifySuccess(
            UiSettings::Instance().Format(UiText::ToastSaveDialogSuccess, savedPath.c_str()),
            2800.0);
    }

    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogSetInputTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }
    if (!arizonaCefDialogs_ || !arizonaCefDialogs_->IsDialogActive()) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogCefUnavailable), 2800.0);
        return std::string();
    }

    if (!arizonaCefDialogs_->SetInputText(Unquote(std::string(param)))) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSetInputFailed), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetInputTextFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!arizonaCefDialogs_) {
        return std::string();
    }
    if (!context.allowSideEffects) {
        return arizonaCefDialogs_->CachedInputText();
    }

    if (context.runningBindRuntimeId != 0
        && ConsumeReadyArzDialogQuery(context.runningBindRuntimeId, PendingArzDialogQueryKind::InputText)) {
        return arizonaCefDialogs_->CachedInputText();
    }

    const std::string rawTimeout = Unquote(Trim(param));
    const std::optional<int> timeoutMs = rawTimeout.empty() ? std::nullopt : ParseInteger(rawTimeout);
    const int timeout = std::clamp(timeoutMs.value_or(kArzDialogQueryDefaultTimeoutMs), 1, kArzDialogQueryMaxTimeoutMs);
    const std::string value = arizonaCefDialogs_->QueryInputText(timeout);
    if (context.runningBindRuntimeId != 0 && arizonaCefDialogs_->HasPendingInputTextQuery()) {
        QueuePendingArzDialogQueryWait(
            context.runningBindRuntimeId,
            PendingArzDialogQueryKind::InputText,
            GetTickCount64() + static_cast<std::uint64_t>(timeout));
        MarkCurrentDispatchBlocked(context.runningBindRuntimeId);
        if (binderModule_) {
            binderModule_->PauseRuntime(context.runningBindRuntimeId);
        }
        return std::string();
    }

    return value;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogCloseWithButtonFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }
    if (!arizonaCefDialogs_ || !arizonaCefDialogs_->IsDialogActive()) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogCefUnavailable), 2800.0);
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    const std::optional<int> button = ParseInteger(rawValue);
    if (!button.has_value() || (*button != 0 && *button != 1)) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogCloseInvalidButton), 2800.0);
        return std::string();
    }

    if (!arizonaCefDialogs_->CloseWithButton(*button)) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogCloseFailed), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogSetListItemFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects) {
        return std::string();
    }
    if (!arizonaCefDialogs_ || !arizonaCefDialogs_->IsDialogActive()) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogCefUnavailable), 2800.0);
        return std::string();
    }

    const std::string rawValue = Unquote(Trim(param));
    const std::optional<int> index = ParseInteger(rawValue);
    if (!index.has_value() || *index < 0) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSetListInvalidIndex), 2800.0);
        return std::string();
    }

    if (!arizonaCefDialogs_->SetListItem(*index)) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSetListFailed), 2800.0);
    }
    return std::string();
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogGetListItemFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (!arizonaCefDialogs_) {
        return std::string();
    }
    if (!context.allowSideEffects) {
        return arizonaCefDialogs_->CachedListItem();
    }

    if (context.runningBindRuntimeId != 0
        && ConsumeReadyArzDialogQuery(context.runningBindRuntimeId, PendingArzDialogQueryKind::ListItem)) {
        return arizonaCefDialogs_->CachedListItem();
    }

    const std::string rawTimeout = Unquote(Trim(param));
    const std::optional<int> timeoutMs = rawTimeout.empty() ? std::nullopt : ParseInteger(rawTimeout);
    const int timeout = std::clamp(timeoutMs.value_or(kArzDialogQueryDefaultTimeoutMs), 1, kArzDialogQueryMaxTimeoutMs);
    const std::string value = arizonaCefDialogs_->QueryListItem(timeout);
    if (context.runningBindRuntimeId != 0 && arizonaCefDialogs_->HasPendingListItemQuery()) {
        QueuePendingArzDialogQueryWait(
            context.runningBindRuntimeId,
            PendingArzDialogQueryKind::ListItem,
            GetTickCount64() + static_cast<std::uint64_t>(timeout));
        MarkCurrentDispatchBlocked(context.runningBindRuntimeId);
        if (binderModule_) {
            binderModule_->PauseRuntime(context.runningBindRuntimeId);
        }
        return std::string();
    }

    return value;
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinArzDialogSendRespondFunctionTag(
    std::string_view rawParam,
    const EvaluationContext& context,
    int depth) const {
    if (!context.allowSideEffects) {
        return std::string();
    }
    if (!arizonaCefDialogs_) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondFailed), 3000.0);
        return std::string();
    }

    const ArzDialogSendRespondParams parsed = ParseArzDialogSendRespondParams(rawParam);
    if (!parsed.valid) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondInvalidFormat), 3000.0);
        return std::string();
    }

    EvaluationContext dataContext = context;
    dataContext.allowSideEffects = false;

    const std::string idValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.id, dataContext, depth + 1)));
    int dialogId = arizonaCefDialogs_->LastDialogId();
    if (!idValue.empty()) {
        const std::optional<int> parsedId = ParseInteger(idValue);
        if (!parsedId.has_value() || *parsedId < 0) {
            NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondInvalidId), 3000.0);
            return std::string();
        }
        dialogId = *parsedId;
    }
    if (dialogId < 0) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondInvalidId), 3000.0);
        return std::string();
    }

    const std::string buttonValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.button, dataContext, depth + 1)));
    const std::optional<int> button = ParseInteger(buttonValue);
    if (!button.has_value() || (*button != 0 && *button != 1)) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondInvalidButton), 3000.0);
        return std::string();
    }

    int listItem = 0;
    if (parsed.hasListPart) {
        const std::string listValue = Unquote(TrimAscii(ExpandTextRecursive(parsed.listItem, dataContext, depth + 1)));
        if (!listValue.empty()) {
            const std::optional<int> parsedList = ParseInteger(listValue);
            if (!parsedList.has_value() || *parsedList < -1) {
                NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondInvalidList), 3000.0);
                return std::string();
            }
            listItem = *parsedList;
        }
    }

    std::string inputText;
    if (parsed.hasInputPart) {
        inputText = Unquote(TrimAscii(ExpandTextRecursive(parsed.input, dataContext, depth + 1)));
    }

    if (!arizonaCefDialogs_->SendRespond(dialogId, *button, listItem, inputText)) {
        NotifyDialogError(UiSettings::Instance().Text(UiText::ToastArzDialogSendRespondFailed), 3000.0);
    }
    return std::string();
}
