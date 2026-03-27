std::uintptr_t SampApi::pDialog_func() {
    std::uint32_t dialog = 0;
    if (!ResolveDialog(dialog)) {
        return 0;
    }
    return dialog;
}

bool SampApi::sampSetCurrentDialogEditboxTextFix(std::string_view newString, bool alreadyDecoded) {
    const std::uintptr_t editBox = pDialogInput_pEditBox_func();
    const auto address = GetAddress(main_offsets.CDXUTEditBox_SetText);
    if (editBox == 0 || address == 0) {
        SetError("Dialog edit box is not available");
        return false;
    }

    std::string gameText = PrepareOutgoingText(newString, alreadyDecoded, false);
    const auto setText = reinterpret_cast<SetEditboxTextFn>(address);

    if (!CallSetEditboxText(setText, reinterpret_cast<void*>(editBox), gameText.data())) {
        SetError("Failed to set current dialog edit box text");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::isDialogActive() {
    const std::uintptr_t dialog = pDialog_func();
    if (dialog == 0) {
        return false;
    }

    std::int32_t active = 0;
    return SafeRead(dialog + main_offsets.SAMP_DIALOG_ACTIVE_OFFSET.Get(currentVersion_), active) && active == 1;
}

SampApi::DialogPosition SampApi::getCurrentDialogPosition() {
    DialogPosition result;
    if (!isDialogActive()) {
        return result;
    }

    const std::uintptr_t dialog = pDialog_func();
    std::uint32_t dxutDialog = 0;
    int baseX = 0;
    int baseY = 0;
    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;

    if (!SafeRead(dialog + main_offsets.CDXUTDialog.Get(currentVersion_), dxutDialog) || dxutDialog == 0
        || !SafeRead(dialog + 0x04, baseX) || !SafeRead(dialog + 0x08, baseY) || !SafeRead(dialog + 0x0C, width)
        || !SafeRead(dialog + 0x10, height) || !SafeRead(dxutDialog + 0x116, offsetX)
        || !SafeRead(dxutDialog + 0x11A, offsetY)) {
        return result;
    }

    result.x = baseX + offsetX;
    result.y = baseY + offsetY;
    result.width = width;
    result.height = height;
    result.valid = true;
    return result;
}

bool SampApi::Set_CursorMode(int mode, bool enabled) {
    std::uint32_t game = 0;
    const auto address = GetAddress(main_offsets.SetCursorMode);
    if (address == 0 || !SafeRead(ModuleBase() + main_offsets.RefGame.Get(currentVersion_), game) || game == 0) {
        SetError("CGame cursor interface is not available");
        return false;
    }

    const auto setCursorMode = reinterpret_cast<SetCursorModeFn>(address);
    if (!CallSetCursorMode(setCursorMode, reinterpret_cast<void*>(game), mode, enabled)) {
        SetError("Failed to set SAMP cursor mode");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::hideDialog() {
    const std::uintptr_t dialog = pDialog_func();
    if (dialog == 0) {
        SetError("Dialog pointer is not available");
        return false;
    }

    if (!SafeWrite<std::uint64_t>(dialog + 40, 0)) {
        SetError("Failed to hide dialog state");
        return false;
    }

    return Set_CursorMode(0, false);
}

bool SampApi::CDialog_Close_func(int button) {
    if (!isDialogActive()) {
        return false;
    }

    const auto address = GetAddress(main_offsets.CDialog_Close);
    const std::uintptr_t dialog = pDialog_func();
    if (address == 0 || dialog == 0) {
        SetError("CDialog::Close is not available");
        return false;
    }

    const auto closeDialog = reinterpret_cast<DialogCloseFn>(address);
    ++dialogHookBypassDepth_;

    const bool success = CallDialogClose(closeDialog, reinterpret_cast<void*>(dialog), button);

    dialogHookBypassDepth_ = std::max(dialogHookBypassDepth_ - 1, 0);
    if (!success) {
        SetError("Failed to close dialog");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::isDialogHookBypassActive() const {
    return dialogHookBypassDepth_ > 0;
}

std::uintptr_t SampApi::pDialogInput_pEditBox_func() {
    const std::uintptr_t dialog = pDialog_func();
    if (dialog == 0) {
        return 0;
    }

    std::uint32_t editBox = 0;
    if (!SafeRead(dialog + main_offsets.pDialogInput_pEditBox.Get(currentVersion_), editBox)) {
        return 0;
    }
    return editBox;
}

bool SampApi::pDialogInput_pEditBox_active_func() {
    const std::uintptr_t editBox = pDialogInput_pEditBox_func();
    std::uint8_t active = 0;
    return editBox != 0 && SafeRead(editBox + 0x4, active) && active == 1;
}

int SampApi::SAMP_DIALOG_ID() {
    if (!isDialogActive()) {
        return -1;
    }

    std::int32_t dialogId = -1;
    const auto dialog = pDialog_func();
    if (!SafeRead(dialog + main_offsets.SAMP_DIALOG_ID_OFFSET.Get(currentVersion_), dialogId)) {
        return -1;
    }

    return dialogId;
}

bool SampApi::sampSetDialogInputCursor(int cursor) {
    const std::uintptr_t editBox = pDialogInput_pEditBox_func();
    if (!isDialogActive() || editBox == 0) {
        SetError("Dialog input edit box is not available");
        return false;
    }

    const auto value = static_cast<std::int8_t>(cursor);
    if (!SafeWrite(editBox + 0x11E, value) || !SafeWrite(editBox + 0x119, value)) {
        SetError("Failed to set dialog input cursor");
        return false;
    }

    ClearError();
    return true;
}

SampApi::CursorRange SampApi::sampGetDialogInputCursor() {
    CursorRange result;
    if (!isDialogActive() || !pDialogInput_pEditBox_active_func()) {
        return result;
    }

    const std::uintptr_t editBox = pDialogInput_pEditBox_func();
    std::int8_t start = -1;
    std::int8_t end = -1;
    if (!SafeRead(editBox + 0x11E, start) || !SafeRead(editBox + 0x119, end)) {
        return result;
    }

    result.start = start;
    result.end = end;
    result.valid = true;
    return result;
}

std::uintptr_t SampApi::SAMP_CHAT_INPUT_INFO_OFFSET_func() {
    std::uint32_t input = 0;
    if (!ResolveChatInput(input)) {
        return 0;
    }
    return input;
}

std::uintptr_t SampApi::SAMP_CHAT_INPUT_INFO_OFFSET_func_test() {
    const std::uintptr_t input = SAMP_CHAT_INPUT_INFO_OFFSET_func();
    if (input == 0) {
        return 0;
    }

    std::uint32_t editBox = 0;
    if (!SafeRead(input + main_offsets.pChatInput_pEditBox.Get(currentVersion_), editBox)) {
        return 0;
    }
    return editBox;
}

std::string SampApi::sampGetChatEditboxText() {
    const auto address = GetAddress(main_offsets.CDXUTEditBox_GetText);
    const std::uintptr_t editBox = SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
    if (address == 0 || editBox == 0) {
        return {};
    }

    const auto getText = reinterpret_cast<GetEditboxTextFn>(address);
    char* rawText = nullptr;

    if (!CallGetEditboxText(getText, reinterpret_cast<void*>(editBox), rawText) || !rawText) {
        return {};
    }

    return PrepareIncomingText(SafeReadCString(reinterpret_cast<std::uintptr_t>(rawText), kDefaultTextLimit));
}

bool SampApi::pCInput_Open_Close(bool open) {
    const std::uintptr_t input = SAMP_CHAT_INPUT_INFO_OFFSET_func();
    const auto address = GetAddress(open ? main_offsets.CInput_Open : main_offsets.CInput_Close);
    if (input == 0 || address == 0) {
        SetError("CInput open/close routine is not available");
        return false;
    }

    const auto toggleInput = reinterpret_cast<InputOpenCloseFn>(address);
    if (!CallInputOpenClose(toggleInput, reinterpret_cast<void*>(input))) {
        SetError("Failed to toggle chat input state");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::sampSetChatInputCursor(int cursor) {
    const std::uintptr_t editBox = SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
    if (editBox == 0) {
        SetError("Chat input edit box is not available");
        return false;
    }

    const auto value = static_cast<std::int8_t>(cursor);
    if (!SafeWrite(editBox + 0x11E, value) || !SafeWrite(editBox + 0x119, value)) {
        SetError("Failed to set chat input cursor");
        return false;
    }

    ClearError();
    return true;
}

std::string SampApi::sampGetDialogText() {
    if (!isDialogActive()) {
        return {};
    }

    std::uint32_t textPtr = 0;
    const std::uintptr_t dialog = pDialog_func();
    if (!SafeRead(dialog + main_offsets.SAMP_DIALOG_TEXT_OFFSET.Get(currentVersion_), textPtr) || textPtr == 0) {
        return {};
    }

    return PrepareIncomingText(SafeReadCString(textPtr, kDefaultTextLimit));
}

int SampApi::getListItemNumberByText(std::string_view text) {
    const std::string dialogText = sampGetDialogText();
    if (dialogText.empty() || text.empty()) {
        return -1;
    }

    std::size_t start = 0;
    int index = 0;
    while (start <= dialogText.size()) {
        const auto end = dialogText.find('\n', start);
        const auto line = dialogText.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (line.find(text) != std::string::npos) {
            return index;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
        ++index;
    }

    return -1;
}

std::string SampApi::get_dialog_caption() {
    if (!isDialogActive()) {
        return {};
    }

    return PrepareIncomingText(
        SafeReadCString(pDialog_func() + main_offsets.SAMP_DIALOG_CAPTION_OFFSET.Get(currentVersion_), kDefaultSmallStringLimit));
}

std::string SampApi::sampGetDialogEditboxText() {
    const int style = GetCurrentDialogStyle();
    if (!isDialogActive() || !isDialogInputStyle(style) || !pDialogInput_pEditBox_active_func()) {
        return {};
    }

    const auto address = GetAddress(main_offsets.CDXUTEditBox_GetText);
    const std::uintptr_t editBox = pDialogInput_pEditBox_func();
    if (address == 0 || editBox == 0) {
        return {};
    }

    const auto getText = reinterpret_cast<GetEditboxTextFn>(address);
    char* rawText = nullptr;

    if (!CallGetEditboxText(getText, reinterpret_cast<void*>(editBox), rawText) || !rawText) {
        return {};
    }

    return PrepareIncomingText(SafeReadCString(reinterpret_cast<std::uintptr_t>(rawText), kDefaultTextLimit));
}

bool SampApi::sampSetDialogEditboxText(std::string_view text, bool alreadyDecoded) {
    const int style = GetCurrentDialogStyle();
    if (!isDialogActive() || !isDialogInputStyle(style) || !pDialogInput_pEditBox_active_func()) {
        SetError("Dialog input edit box is not active");
        return false;
    }

    const auto address = GetAddress(main_offsets.CDXUTEditBox_SetText);
    const std::uintptr_t editBox = pDialogInput_pEditBox_func();
    if (address == 0 || editBox == 0) {
        SetError("CDXUTEditBox::SetText is not available");
        return false;
    }

    std::string gameText = PrepareOutgoingText(text, alreadyDecoded, false);
    const auto setText = reinterpret_cast<SetEditboxTextFn>(address);

    if (!CallSetEditboxText(setText, reinterpret_cast<void*>(editBox), gameText.data())) {
        SetError("Failed to set dialog edit box text");
        return false;
    }

    ClearError();
    return true;
}

bool SampApi::SetCurrentDialogListItem(int index) {
    const std::uintptr_t listBox = GetCurrentDialogListBoxPointer();
    const auto address = GetAddress(main_offsets.SAMP_SET_DIALOG_LIST_ITEM_OFFSET);
    if (listBox == 0 || address == 0) {
        SetError("Dialog list box is not available");
        return false;
    }

    const auto setItem = reinterpret_cast<SetDialogListItemFn>(address);
    if (!CallSetDialogListItem(setItem, reinterpret_cast<void*>(listBox), index)) {
        SetError("Failed to set current dialog list item");
        return false;
    }

    ClearError();
    return true;
}

int SampApi::GetCurrentDialogListItem() {
    return GetCurrentDialogSelectedIndex();
}

SampApi::DialogSelectionText SampApi::getDialogSelectedItemText() {
    DialogSelectionText result;
    const int selectedIndex = GetCurrentDialogSelectedIndex();
    const std::uintptr_t listBox = GetCurrentDialogListBoxPointer();
    const auto address = GetAddress(main_offsets.CDXUTListBox__GetItem);
    if (selectedIndex < 0 || listBox == 0 || address == 0) {
        return result;
    }

    const auto getItem = reinterpret_cast<ListBoxGetItemFn>(address);
    const char* rawItem = nullptr;

    if (!CallListBoxGetItem(getItem, reinterpret_cast<void*>(listBox), selectedIndex, rawItem) || !rawItem) {
        return result;
    }

    result.found = true;
    result.text = PrepareIncomingText(SafeReadCString(reinterpret_cast<std::uintptr_t>(rawItem), kDefaultTextLimit));
    return result;
}

int SampApi::GetCurrentDialogStyle() {
    if (!isDialogActive()) {
        return -1;
    }

    std::int32_t style = -1;
    if (!SafeRead(pDialog_func() + 0x2C, style)) {
        return -1;
    }

    return style;
}

bool SampApi::isDialogInputStyle(int style) const {
    return style == DIALOG_STYLE_INPUT || style == DIALOG_STYLE_PASSWORD;
}

bool SampApi::isDialogListStyle(int style) const {
    return style == DIALOG_STYLE_LIST || style == DIALOG_STYLE_TABLIST || style == DIALOG_STYLE_TABLIST_HEADERS;
}

SampApi::DialogSubmitResult SampApi::submitCurrentDialog(
    int button,
    std::optional<int> listItem,
    std::optional<std::string> inputText,
    bool alreadyDecoded) {
    DialogSubmitResult result;

    if (!isDialogActive()) {
        result.error = "no_dialog";
        return result;
    }

    if (button != 0 && button != 1) {
        result.error = "invalid_button";
        return result;
    }

    result.style = GetCurrentDialogStyle();
    if (result.style < 0) {
        result.error = "no_style";
        return result;
    }

    if (button == 1) {
        if (isDialogInputStyle(result.style) && inputText.has_value()) {
            if (!sampSetDialogEditboxText(*inputText, alreadyDecoded)) {
                result.error = "set_input_failed";
                return result;
            }
        } else if (isDialogListStyle(result.style) && listItem.has_value()) {
            if (!SetCurrentDialogListItem(*listItem)) {
                result.error = "set_listitem_failed";
                return result;
            }
        }
    }

    if (!CDialog_Close_func(button)) {
        result.error = "close_failed";
        return result;
    }

    result.ok = true;
    return result;
}

int SampApi::GetCurrentDialogListboxItemsCount() {
    const std::uintptr_t listBox = GetCurrentDialogListBoxPointer();
    if (listBox == 0) {
        return 0;
    }

    std::int32_t count = 0;
    if (!SafeRead(listBox + 0x150, count)) {
        return 0;
    }

    return count;
}
