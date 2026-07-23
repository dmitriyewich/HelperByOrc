#include "binder_module_impl.h"

void BinderModule::Impl::SaveConfig() {
    EnsureCategories();
    ActiveCategory().lastOpenFolderPath = CurrentFolderPath();
    NormalizeExplorerOrders();
    LogExplorerOrderValidation("save");
    JsonObject root;
    root["quick_menu_hotkey"] = SerializeUintArray(quickMenuHotkey);
    root["quick_menu_activation_mode"] = QuickMenuActivationModeId(quickMenuActivationMode);
    root["quick_menu_style"] = QuickMenuStyleId(quickMenuStyle);
    root["quick_menu_width_mode"] = QuickMenuWidthModeId(quickMenuWidthMode);
    root["quick_menu_category_layout"] = QuickMenuCategoryLayoutId(quickMenuCategoryLayout);
    root["quick_menu_show_scrollbar"] = quickMenuShowScrollbar;
    root["bind_list_style"] = BindListStyleId(bindListStyle);
    root["text_confirmation_wait_timeout_ms"] = textConfirmationWaitTimeoutMs;
    root["active_category_id"] = activeCategoryId;

    JsonArray categoryArray;
    for (const BinderCategory& category : categories) {
        categoryArray.push_back(SerializeCategory(category));
    }
    root["categories"] = JsonValue(std::move(categoryArray));

    JsonArray hotkeyArray;
    for (const HotkeyEntry& hotkey : hotkeys) {
        hotkeyArray.push_back(SerializeHotkey(hotkey));
    }
    root["hotkeys"] = JsonValue(std::move(hotkeyArray));

    AppConfig::Instance().QueueSectionReplace(std::string(kBinderConfigSectionName), JsonValue(std::move(root)));
}

bool BinderModule::Impl::MigrateDeprecatedHelperCondition(std::vector<bool>& conditions) {
    const bool changed = ClearDeprecatedHelperCondition(conditions);
    NormalizeConditionFlags(conditions);
    if (changed) {
        deprecatedHelperConditionMigrated_ = true;
    }
    return changed;
}

void BinderModule::Impl::ApplyDeprecatedHelperBindMigration(HotkeyEntry& hotkey, bool helperOnly) {
    if (!helperOnly || !hotkey.enabled) {
        return;
    }

    hotkey.enabled = false;
    ++deprecatedHelperBindDisabledCount_;
}

void BinderModule::Impl::LogDeprecatedHelperConditionMigration() const {
    if (!deprecatedHelperConditionMigrated_) {
        return;
    }

    debuglog::WriteInfo(
        "[binder] migrated deprecated HelperActive condition disabledBinds=%d disabledQuickCategories=%d disabledQuickFolders=%d",
        deprecatedHelperBindDisabledCount_,
        deprecatedHelperCategoryQuickDisabledCount_,
        deprecatedHelperFolderQuickDisabledCount_);
}

void BinderModule::Impl::LoadConfig() {
    ++categoryTabOrderRevision;
    categories.clear();
    activeCategoryId.clear();
    categoryTabSelectionTargetId.clear();
    hotkeys.clear();
    currentFolder = nullptr;
    selectedFolder = nullptr;
    explorerSelection = {};
    explorerSelectionScrollPending = false;
    nextFolderId = 1;
    nextCategoryId = 1;
    nextHotkeyRuntimeId = 1;
    nextHotkeyOrderId = 1;
    deprecatedHelperConditionMigrated_ = false;
    deprecatedHelperBindDisabledCount_ = 0;
    deprecatedHelperCategoryQuickDisabledCount_ = 0;
    deprecatedHelperFolderQuickDisabledCount_ = 0;
    quickMenuHotkey.clear();
    quickMenuActivationMode = QuickMenuActivationMode::Hold;
    quickMenuStyle = QuickMenuStyle::Cascade;
    quickMenuWidthMode = QuickMenuWidthMode::Fixed;
    quickMenuCategoryLayout = QuickMenuCategoryLayout::TitleSelector;
    quickMenuShowScrollbar = true;
    bindListStyle = BindListStyle::Explorer;
    textConfirmationWaitTimeoutMs = kDefaultTextConfirmationWaitTimeoutMs;
    quickMenuActiveCategoryId.clear();
    const jsonutil::JsonValue sharedSection = AppConfig::Instance().ReadSection(kBinderConfigSectionName);
    const JsonObject* root = sharedSection.TryObject();
    if (!root) {
        EnsureCategories();
        return;
    }

    quickMenuHotkey = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(jsonutil::JsonArrayOrNull(root, "quick_menu_hotkey")), HotkeyMode::ModifierTrigger);
    quickMenuActivationMode =
        NormalizeQuickMenuActivationMode(jsonutil::JsonStringOr(root, "quick_menu_activation_mode", "hold"));
    quickMenuStyle =
        NormalizeQuickMenuStyle(jsonutil::JsonStringOr(root, "quick_menu_style", "cascade"));
    quickMenuWidthMode =
        NormalizeQuickMenuWidthMode(jsonutil::JsonStringOr(root, "quick_menu_width_mode", "fixed"));
    quickMenuCategoryLayout = NormalizeQuickMenuCategoryLayout(
        jsonutil::JsonStringOr(root, "quick_menu_category_layout", "title_selector"));
    quickMenuShowScrollbar = jsonutil::JsonBoolOr(root, "quick_menu_show_scrollbar", true);
    bindListStyle =
        NormalizeBindListStyle(jsonutil::JsonStringOr(root, "bind_list_style", "explorer"));
    textConfirmationWaitTimeoutMs = std::clamp(
        jsonutil::JsonNumberOr<int>(
            root,
            "text_confirmation_wait_timeout_ms",
            kDefaultTextConfirmationWaitTimeoutMs),
        kMinTextConfirmationWaitTimeoutMs,
        kMaxTextConfirmationWaitTimeoutMs);
    activeCategoryId = jsonutil::JsonStringOr(root, "active_category_id", "");

    if (const JsonArray* categoryArray = jsonutil::JsonArrayOrNull(root, "categories")) {
        for (const JsonValue& item : *categoryArray) {
            if (const JsonObject* object = item.TryObject()) {
                categories.push_back(DeserializeCategory(*object));
            }
        }
    }

    const bool legacyFormat = categories.empty();
    if (legacyFormat) {
        BinderCategory category = MakeDefaultCategory();
        category.rootItems = DeserializeExplorerItems(jsonutil::JsonArrayOrNull(root, "root_order"));
        category.lastOpenFolderPath =
            DeserializeStringArray(jsonutil::JsonArrayOrNull(root, "last_open_folder_path"));

        if (const JsonArray* folderArray = jsonutil::JsonArrayOrNull(root, "folders")) {
            for (const JsonValue& item : *folderArray) {
                if (const JsonObject* object = item.TryObject()) {
                    auto folder = DeserializeFolder(*object, nullptr);
                    if (folder) {
                        category.folders.push_back(std::move(folder));
                    }
                }
            }
        }
        activeCategoryId = category.id;
        categories.push_back(std::move(category));
    }

    if (const JsonArray* hotkeyArray = jsonutil::JsonArrayOrNull(root, "hotkeys")) {
        for (const JsonValue& item : *hotkeyArray) {
            if (const JsonObject* object = item.TryObject()) {
                hotkeys.push_back(DeserializeHotkey(*object));
            }
        }
    }

    EnsureCategories();
    if (!FindCategoryById(activeCategoryId)) {
        activeCategoryId = categories.front().id;
    }
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!FindCategoryById(hotkey.categoryId)) {
            hotkey.categoryId = categories.front().id;
        }
    }
    const bool migratedLegacyRoot = NormalizeLegacyRootFolderName();
    EnsureHotkeyOrderIds();
    NormalizeExplorerOrders();
    BinderCategory& active = ActiveCategory();
    currentFolder = active.lastOpenFolderPath.empty() ? nullptr : FindFolderByPath(active.folders, active.lastOpenFolderPath);
    if (!currentFolder) {
        active.lastOpenFolderPath.clear();
    }
    categoryTabSelectionTargetId = activeCategoryId;
    ClearExplorerSelection();
    RefreshNumbers();

    LogDeprecatedHelperConditionMigration();
    if (legacyFormat || migratedLegacyRoot || deprecatedHelperConditionMigrated_) {
        SaveConfig();
    }
}

JsonValue BinderModule::Impl::SerializeCategory(const BinderCategory& category) const {
    JsonObject object;
    object["id"] = category.id;
    object["name"] = category.name;
    object["quick_menu"] = category.quickMenu;
    object["conditions"] = SerializeBoolArray(category.conditions);
    object["conditions_combine"] = ConditionCombineModeId(category.conditionsCombine);
    object["root_order"] = SerializeExplorerItems(category.rootItems);
    object["last_open_folder_path"] = SerializeStringArray(category.lastOpenFolderPath);

    JsonArray folderArray;
    for (const auto& folder : category.folders) {
        if (folder) {
            folderArray.push_back(SerializeFolder(*folder));
        }
    }
    object["folders"] = JsonValue(std::move(folderArray));
    return JsonValue(std::move(object));
}

BinderCategory BinderModule::Impl::DeserializeCategory(const JsonObject& object) {
    BinderCategory category;
    category.id = jsonutil::JsonStringOr(&object, "id", "");
    if (category.id.empty()) {
        category.id = AllocateCategoryId();
    } else if (category.id.rfind("category-", 0) == 0) {
        const std::string suffix = category.id.substr(9);
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](const unsigned char ch) {
                return std::isdigit(ch) != 0;
            })) {
            try {
                const auto parsed = static_cast<std::uint64_t>(std::stoull(suffix));
                if (parsed < std::numeric_limits<std::uint64_t>::max()) {
                    nextCategoryId = std::max(nextCategoryId, parsed + 1);
                }
            } catch (...) {
                // Keep loading configs with malformed user-edited category ids.
            }
        }
    }
    category.name = SanitizeFolderName(
        jsonutil::JsonStringOr(&object, "name", UiSettings::Instance().Text(UiText::BinderDefaultRootFolder)));
    if (category.name.empty()) {
        category.name = UiSettings::Instance().Text(UiText::BinderDefaultRootFolder);
    }
    category.quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", true);
    category.conditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "conditions"));
    if (category.conditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
        category.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    }
    const bool categoryHadDeprecatedHelper = MigrateDeprecatedHelperCondition(category.conditions);
    if (categoryHadDeprecatedHelper && !HasSelectedCondition(category.conditions) && category.quickMenu) {
        category.quickMenu = false;
        ++deprecatedHelperCategoryQuickDisabledCount_;
    }
    category.conditionsCombine =
        NormalizeConditionCombineMode(jsonutil::JsonStringOr(&object, "conditions_combine", "require_any"));
    category.rootItems = DeserializeExplorerItems(jsonutil::JsonArrayOrNull(&object, "root_order"));
    category.lastOpenFolderPath =
        DeserializeStringArray(jsonutil::JsonArrayOrNull(&object, "last_open_folder_path"));

    if (const JsonArray* folderArray = jsonutil::JsonArrayOrNull(&object, "folders")) {
        for (const JsonValue& item : *folderArray) {
            if (const JsonObject* folderObject = item.TryObject()) {
                auto folder = DeserializeFolder(*folderObject, nullptr);
                if (folder) {
                    category.folders.push_back(std::move(folder));
                }
            }
        }
    }
    return category;
}

JsonValue BinderModule::Impl::SerializeFolder(const FolderNode& folder) const {
    JsonObject object;
    object["name"] = folder.name;
    object["icon_id"] = folder.iconId;
    object["enabled"] = folder.enabled;
    object["quick_menu"] = folder.quickMenu;
    object["conditions"] = SerializeBoolArray(folder.conditions);
    object["conditions_combine"] = ConditionCombineModeId(folder.conditionsCombine);
    object["items"] = SerializeExplorerItems(folder.items);

    JsonArray childrenArray;
    for (const auto& child : folder.children) {
        if (child) {
            childrenArray.push_back(SerializeFolder(*child));
        }
    }
    object["children"] = JsonValue(std::move(childrenArray));
    return JsonValue(std::move(object));
}

std::unique_ptr<FolderNode> BinderModule::Impl::DeserializeFolder(const JsonObject& object, FolderNode* parent) {
    auto folder = std::make_unique<FolderNode>();
    folder->id = nextFolderId++;
    folder->parent = parent;
    folder->name = SanitizeFolderName(
        jsonutil::JsonStringOr(&object, "name", UiSettings::Instance().Text(UiText::BinderDefaultFolder)));
    if (folder->name.empty()) {
        folder->name = UiSettings::Instance().Text(UiText::BinderDefaultFolder);
    }
    if (binder_tags::IsReservedFolderName(folder->name)) {
        debuglog::WriteError(
            "[binder][selector] legacy reserved folder name kept id=%d name=%s; use @bind-N selectors until renamed",
            folder->id,
            folder->name.c_str());
    }
    folder->iconId = icon_registry::NormalizeIconId(jsonutil::JsonStringOr(&object, "icon_id", ""));
    folder->enabled = jsonutil::JsonBoolOr(&object, "enabled", true);
    folder->quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", true);
    const JsonArray* conditionsArray = jsonutil::JsonArrayOrNull(&object, "conditions");
    if (!conditionsArray) {
        conditionsArray = jsonutil::JsonArrayOrNull(&object, "quick_conditions");
    }
    folder->conditions = DeserializeBoolArray(conditionsArray);
    if (folder->conditions.size() < static_cast<std::size_t>(ConditionId::Count)) {
        folder->conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    }
    const bool folderHadDeprecatedHelper = MigrateDeprecatedHelperCondition(folder->conditions);
    if (folderHadDeprecatedHelper && !HasSelectedCondition(folder->conditions) && folder->quickMenu) {
        folder->quickMenu = false;
        ++deprecatedHelperFolderQuickDisabledCount_;
    }
    folder->conditionsCombine = NormalizeConditionCombineMode(jsonutil::JsonStringOr(
        &object,
        "conditions_combine",
        jsonutil::JsonStringOr(&object, "quick_conditions_combine", "require_all")));
    folder->items = DeserializeExplorerItems(jsonutil::JsonArrayOrNull(&object, "items"));

    if (const JsonArray* children = jsonutil::JsonArrayOrNull(&object, "children")) {
        for (const JsonValue& childValue : *children) {
            if (const JsonObject* childObject = childValue.TryObject()) {
                auto child = DeserializeFolder(*childObject, folder.get());
                if (child) {
                    folder->children.push_back(std::move(child));
                }
            }
        }
    }
    return folder;
}

JsonValue BinderModule::Impl::SerializeExplorerItems(const std::vector<ExplorerItem>& items) const {
    JsonArray array;
    for (const ExplorerItem& item : items) {
        if (item.key.empty()) {
            continue;
        }
        JsonObject object;
        object["type"] = item.kind == ExplorerItemKind::Folder ? "folder" : "bind";
        object["key"] = item.key;
        array.emplace_back(std::move(object));
    }
    return JsonValue(std::move(array));
}

std::vector<ExplorerItem> BinderModule::Impl::DeserializeExplorerItems(const JsonArray* array) const {
    std::vector<ExplorerItem> items;
    if (!array) {
        return items;
    }

    for (const JsonValue& value : *array) {
        const JsonObject* object = value.TryObject();
        if (!object) {
            continue;
        }
        const std::string type = ToLower(jsonutil::JsonStringOr(object, "type", ""));
        const std::string key = jsonutil::JsonStringOr(object, "key", "");
        if (key.empty()) {
            continue;
        }
        if (type == "folder") {
            items.push_back(ExplorerItem{ ExplorerItemKind::Folder, key });
        } else if (type == "bind") {
            items.push_back(ExplorerItem{ ExplorerItemKind::Bind, key });
        }
    }
    return items;
}

JsonValue BinderModule::Impl::SerializeHotkey(const HotkeyEntry& hotkey) const {
    JsonObject object;
    object["label"] = hotkey.label;
    object["icon_id"] = hotkey.iconId;
    object["keys"] = SerializeUintArray(hotkey.keys);
    object["hotkey_mode"] = HotkeyModeId(hotkey.hotkeyMode);
    object["conditions"] = SerializeBoolArray(hotkey.conditions);
    object["conditions_combine"] = ConditionCombineModeId(hotkey.conditionsCombine);
    object["repeat_mode"] = hotkey.repeatMode;
    object["repeat_interval_ms"] = hotkey.repeatIntervalMs;
    object["enabled"] = hotkey.enabled;
    object["quick_menu"] = hotkey.quickMenu;
    object["command"] = hotkey.command;
    object["command_enabled"] = hotkey.commandEnabled;
    object["category_id"] = hotkey.categoryId;
    object["folder_path"] = SerializeStringArray(hotkey.folderPath);
    object["order_id"] = hotkey.orderId;

    JsonObject trigger;
    trigger["text"] = hotkey.textTrigger.text;
    trigger["enabled"] = hotkey.textTrigger.enabled;
    trigger["pattern"] = hotkey.textTrigger.pattern;
    object["text_trigger"] = JsonValue(std::move(trigger));

    JsonObject confirmation;
    confirmation["enabled"] = hotkey.textConfirmation.enabled;
    confirmation["key"] = static_cast<double>(hotkey.textConfirmation.key);
    confirmation["cancel_key"] = static_cast<double>(hotkey.textConfirmation.cancelKey);
    confirmation["wait_for_resolution"] = hotkey.textConfirmation.waitForResolution;
    object["text_confirmation"] = JsonValue(std::move(confirmation));

    JsonObject commandConfirmation;
    commandConfirmation["enabled"] = hotkey.commandConfirmation.enabled;
    commandConfirmation["wait_for_resolution"] = hotkey.commandConfirmation.waitForResolution;
    object["command_confirmation"] = JsonValue(std::move(commandConfirmation));

    JsonArray messages;
    for (const HotkeyMessage& message : hotkey.messages) {
        JsonObject item;
        item["text"] = message.text;
        item["interval_ms"] = message.intervalMs;
        item["method"] = message.method;
        messages.emplace_back(std::move(item));
    }
    object["messages"] = JsonValue(std::move(messages));

    JsonArray inputs;
    for (const HotkeyInput& input : hotkey.inputs) {
        JsonObject item;
        item["key"] = input.key;
        item["label"] = input.label;
        item["hint"] = input.hint;
        item["mode"] = InputModeId(input.mode);
        item["multi_select"] = input.multiSelect;
        item["multi_separator"] = input.multiSeparator;
        item["cascade_parent_key"] = input.cascadeParentKey;

        JsonArray buttons;
        for (const InputButton& button : input.buttons) {
            JsonObject buttonObject;
            buttonObject["label"] = button.label;
            buttonObject["text"] = button.text;
            buttonObject["hint"] = button.hint;
            buttonObject["when"] = button.when;
            buttons.emplace_back(std::move(buttonObject));
        }
        item["buttons"] = JsonValue(std::move(buttons));
        inputs.emplace_back(std::move(item));
    }
    object["inputs"] = JsonValue(std::move(inputs));
    return JsonValue(std::move(object));
}

HotkeyEntry BinderModule::Impl::DeserializeHotkey(const JsonObject& object) {
    HotkeyEntry hotkey = MakeDefaultHotkey();
    hotkey.label = jsonutil::JsonStringOr(&object, "label", hotkey.label);
    hotkey.iconId = icon_registry::NormalizeIconId(jsonutil::JsonStringOr(&object, "icon_id", ""));
    hotkey.hotkeyMode = NormalizeHotkeyMode(jsonutil::JsonStringOr(&object, "hotkey_mode", "modifier_trigger"));
    hotkey.keys = ::hotkeys::NormalizeCombo(
        DeserializeUintArray(jsonutil::JsonArrayOrNull(&object, "keys")), hotkey.hotkeyMode);
    hotkey.conditions = DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "conditions"));
    hotkey.conditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    const bool hadDeprecatedHelperCondition = MigrateDeprecatedHelperCondition(hotkey.conditions);
    hotkey.conditionsCombine =
        NormalizeConditionCombineMode(jsonutil::JsonStringOr(&object, "conditions_combine", "require_all"));
    std::vector<bool> legacyQuickConditions =
        DeserializeBoolArray(jsonutil::JsonArrayOrNull(&object, "quick_conditions"));
    legacyQuickConditions.resize(static_cast<std::size_t>(ConditionId::Count), false);
    const bool hadDeprecatedHelperQuickCondition = MigrateDeprecatedHelperCondition(legacyQuickConditions);
    if (!HasSelectedCondition(hotkey.conditions) && HasSelectedCondition(legacyQuickConditions)) {
        hotkey.conditions = std::move(legacyQuickConditions);
        hotkey.conditionsCombine =
            NormalizeConditionCombineMode(jsonutil::JsonStringOr(&object, "quick_conditions_combine", "require_all"));
    }
    const bool helperOnlyAfterMigration =
        (hadDeprecatedHelperCondition || hadDeprecatedHelperQuickCondition) && !HasSelectedCondition(hotkey.conditions);
    hotkey.repeatMode = jsonutil::JsonBoolOr(&object, "repeat_mode", false);
    hotkey.repeatIntervalMs = jsonutil::JsonNumberOr<int>(&object, "repeat_interval_ms", kDefaultRepeatIntervalMs);
    hotkey.enabled = jsonutil::JsonBoolOr(&object, "enabled", true);
    ApplyDeprecatedHelperBindMigration(hotkey, helperOnlyAfterMigration);
    hotkey.quickMenu = jsonutil::JsonBoolOr(&object, "quick_menu", false);
    hotkey.command = jsonutil::JsonStringOr(&object, "command", "");
    hotkey.commandEnabled = jsonutil::JsonBoolOr(&object, "command_enabled", false);
    hotkey.categoryId = jsonutil::JsonStringOr(&object, "category_id", "");
    hotkey.folderPath = DeserializeStringArray(jsonutil::JsonArrayOrNull(&object, "folder_path"));
    hotkey.orderId = jsonutil::JsonStringOr(&object, "order_id", "");

    if (const JsonObject* trigger = jsonutil::JsonObjectOrNull(&object, "text_trigger")) {
        hotkey.textTrigger.text = jsonutil::JsonStringOr(trigger, "text", "");
        hotkey.textTrigger.enabled = jsonutil::JsonBoolOr(trigger, "enabled", false);
        hotkey.textTrigger.pattern = jsonutil::JsonBoolOr(trigger, "pattern", false);
        hotkey.textTrigger.InvalidateRuntimeCache();
    }

    if (const JsonObject* confirmation = jsonutil::JsonObjectOrNull(&object, "text_confirmation")) {
        hotkey.textConfirmation.enabled = jsonutil::JsonBoolOr(confirmation, "enabled", false);
        hotkey.textConfirmation.key =
            static_cast<UINT>(jsonutil::JsonNumberOr<double>(confirmation, "key", kDefaultConfirmKey));
        hotkey.textConfirmation.cancelKey =
            static_cast<UINT>(jsonutil::JsonNumberOr<double>(confirmation, "cancel_key", kDefaultCancelKey));
        hotkey.textConfirmation.waitForResolution =
            jsonutil::JsonBoolOr(confirmation, "wait_for_resolution", true);
    }

    if (const JsonObject* commandConfirmation = jsonutil::JsonObjectOrNull(&object, "command_confirmation")) {
        hotkey.commandConfirmation.enabled = jsonutil::JsonBoolOr(commandConfirmation, "enabled", false);
        hotkey.commandConfirmation.waitForResolution =
            jsonutil::JsonBoolOr(commandConfirmation, "wait_for_resolution", true);
    }

    hotkey.messages.clear();
    if (const JsonArray* messages = jsonutil::JsonArrayOrNull(&object, "messages")) {
        for (const JsonValue& messageValue : *messages) {
            const JsonObject* message = messageValue.TryObject();
            if (!message) {
                continue;
            }
            hotkey.messages.push_back(HotkeyMessage{
                jsonutil::JsonStringOr(message, "text", ""),
                jsonutil::JsonNumberOr<int>(message, "interval_ms", 0),
                jsonutil::JsonNumberOr<int>(message, "method", 0),
            });
        }
    }
    if (hotkey.messages.empty()) {
        hotkey.messages.push_back(HotkeyMessage{ "", 0, 0 });
    }

    hotkey.inputs.clear();
    if (const JsonArray* inputs = jsonutil::JsonArrayOrNull(&object, "inputs")) {
        for (const JsonValue& inputValue : *inputs) {
            const JsonObject* inputObject = inputValue.TryObject();
            if (!inputObject) {
                continue;
            }

            HotkeyInput input;
            input.key = NormalizeInputKey(jsonutil::JsonStringOr(inputObject, "key", ""));
            input.label = jsonutil::JsonStringOr(inputObject, "label", "");
            input.hint = jsonutil::JsonStringOr(inputObject, "hint", "");
            input.mode = NormalizeInputMode(jsonutil::JsonStringOr(inputObject, "mode", "text"));
            input.multiSelect = jsonutil::JsonBoolOr(inputObject, "multi_select", false);
            input.multiSeparator = jsonutil::JsonStringOr(inputObject, "multi_separator", ", ");
            input.cascadeParentKey = NormalizeInputKey(jsonutil::JsonStringOr(inputObject, "cascade_parent_key", ""));

            if (const JsonArray* buttons = jsonutil::JsonArrayOrNull(inputObject, "buttons")) {
                for (const JsonValue& buttonValue : *buttons) {
                    const JsonObject* buttonObject = buttonValue.TryObject();
                    if (!buttonObject) {
                        continue;
                    }
                    input.buttons.push_back(InputButton{
                        jsonutil::JsonStringOr(buttonObject, "label", ""),
                        jsonutil::JsonStringOr(buttonObject, "text", ""),
                        jsonutil::JsonStringOr(buttonObject, "hint", ""),
                        jsonutil::JsonStringOr(buttonObject, "when", ""),
                    });
                }
            }

            if (InputModeUsesButtons(input.mode) && input.buttons.empty()) {
                input.mode = InputMode::Text;
            }
            hotkey.inputs.push_back(std::move(input));
        }
    }

    return hotkey;
}

void BinderModule::Impl::EnsureHotkeyOrderIds() {
    for (const HotkeyEntry& hotkey : hotkeys) {
        constexpr std::string_view prefix = "bind-";
        if (!StartsWith(hotkey.orderId, prefix)) {
            continue;
        }
        std::uint64_t value = 0;
        bool valid = false;
        for (const char ch : hotkey.orderId.substr(prefix.size())) {
            if (ch < '0' || ch > '9') {
                valid = false;
                break;
            }
            valid = true;
            value = value * 10 + static_cast<std::uint64_t>(ch - '0');
        }
        if (valid && value >= nextHotkeyOrderId) {
            nextHotkeyOrderId = value + 1;
        }
    }

    std::set<std::string> used;
    for (HotkeyEntry& hotkey : hotkeys) {
        if (!hotkey.orderId.empty() && used.insert(hotkey.orderId).second) {
            continue;
        }

        do {
            hotkey.orderId = AllocateHotkeyOrderId();
        } while (!used.insert(hotkey.orderId).second);
    }
}
