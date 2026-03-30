#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#define APP_UI_TEXTS(X) \
    X(AppBrand, "HelperByOrc", "HelperByOrc") \
    X(AppBrandCompact, "H", "H") \
    X(LanguageRussian, "Русский", "Russian") \
    X(LanguageEnglish, "English", "English") \
    X(TabHome, "Главная", "Home") \
    X(TabBinder, "Биндер", "Binder") \
    X(TabSmiHelper, "СМИ Хелпер", "SMI Helper") \
    X(TabMisc, "Прочее", "Misc") \
    X(TabNotepad, "Блокнот", "Notepad") \
    X(TabSettings, "Настройки", "Settings") \
    X(TabHomeCompact, "ГЛ", "HM") \
    X(TabBinderCompact, "БН", "BD") \
    X(TabSmiHelperCompact, "СМ", "SM") \
    X(TabMiscCompact, "ПР", "MS") \
    X(TabNotepadCompact, "БЛ", "NP") \
    X(TabSettingsCompact, "НС", "ST") \
    X(HomeIntro, "Оставлена чистая оболочка интерфейса: окно, логотип, боковое меню и вкладки без тестовых инструментов и служебных логов.", "The base interface shell is in place: window, logo, sidebar, and tabs without test tools or debug clutter.") \
    X(HomeInterfaceTitle, "Основа интерфейса", "Interface Foundation") \
    X(HomeInterfaceDesc, "Главное окно, кастомный title bar, логотип из ресурса и анимированное боковое меню уже готовы под дальнейшую разработку.", "The main window, custom title bar, resource logo, and animated sidebar are ready for the next feature work.") \
    X(HomeTabsTitle, "Порядок вкладок", "Tab Layout") \
    X(HomeTabsDesc, "Главная, Биндер, СМИ Хелпер, Прочее, Блокнот, Настройки. Порядок приведён к новому варианту без тестовых экранов.", "Home, Binder, SMI Helper, Misc, Notepad, Settings. The tab order was cleaned up and no longer includes test screens.") \
    X(SmiHelperIntro, "Пустая заготовка вкладки. Здесь можно будет вернуть рабочие панели и инструменты уже без тестового мусора.", "This tab is an empty placeholder. The working SMI Helper panels can be restored here without test leftovers.") \
    X(SmiHelperShellTitle, "Каркас вкладки", "Tab Shell") \
    X(SmiHelperShellDesc, "Вкладка оставлена чистой. Подходит для дальнейшего переноса реального функционала СМИ Хелпера.", "The tab stays intentionally clean and is ready for the real SMI Helper functionality to be ported in.") \
    X(MiscIntro, "Служебные тесты, отладочные кнопки и логи убраны. Оставлена только пустая оболочка раздела.", "Service tests, debug buttons, and logs were removed. Only the empty shell of this section remains.") \
    X(MiscShellTitle, "Техническая вкладка", "Technical Tab") \
    X(MiscShellDesc, "Раздел готов для будущих утилит, но сейчас не содержит ни тестовых действий, ни диагностических панелей.", "This section is ready for future utilities, but currently contains no test actions or diagnostic panels.") \
    X(NotepadIntro, "Вкладка оставлена как пустая оболочка. При необходимости сюда можно вернуть заметки или встроенный редактор текста.", "The tab is kept as an empty shell. Notes or an embedded text editor can be restored here later if needed.") \
    X(SettingsIntro, "Здесь настраиваются язык интерфейса, автоматический масштаб под разрешение экрана, пользовательский множитель масштаба и параметры быстрого меню.", "Configure interface language, automatic scaling for screen resolution, a custom scale multiplier, and quick menu settings here.") \
    X(GtaVersionFormat, "Версия GTA: %s", "GTA version: %s") \
    X(SettingsLanguage, "Язык", "Language") \
    X(SettingsUiScale, "Масштаб интерфейса", "Interface Scale") \
    X(SettingsAutoScale, "Автомасштаб под разрешение", "Auto scale for resolution") \
    X(SettingsScaleMultiplier, "Пользовательский множитель", "Custom multiplier") \
    X(SettingsEffectiveScale, "Итоговый масштаб", "Effective scale") \
    X(SettingsResetDefaults, "Сбросить настройки UI", "Reset UI settings") \
    X(SettingsConfigPath, "Файл конфига", "Config file") \
    X(SettingsScaleHint, "Автомасштаб берёт за основу 1920x1080 и подстраивает UI под текущее разрешение. Его можно отключить или скорректировать множителем.", "Auto scale uses 1920x1080 as the reference and adapts the UI to the current resolution. You can disable it or fine-tune it with the multiplier.") \
    X(SettingsMainWindowHotkey, "Хоткей открытия главного окна", "Main window hotkey") \
    X(HotkeyConflictFormat, "Комбинация конфликтует с %s.", "This combination conflicts with %s.") \
    X(HotkeyConflictMainWindowFormat, "хоткеем открытия главного окна (%s)", "the main window hotkey (%s)") \
    X(HotkeyConflictQuickMenuFormat, "хоткеем быстрого меню (%s)", "the quick menu hotkey (%s)") \
    X(HotkeyConflictBindFormat, "биндом \"%s\" (%s)", "bind \"%s\" (%s)") \
    X(BinderDefaultRootFolder, "Основные", "Main") \
    X(BinderDefaultHotkey, "Новый бинд", "New bind") \
    X(BinderDefaultFolder, "Папка", "Folder") \
    X(BinderNewFolder, "Новая папка", "New folder") \
    X(ConditionInWater, "В воде", "In water") \
    X(ConditionDead, "Мёртв", "Dead") \
    X(ConditionInAir, "В воздухе", "In air") \
    X(ConditionInAnyCar, "В любом транспорте", "In any vehicle") \
    X(ConditionWithoutWeapon, "Без оружия", "Without weapon") \
    X(ConditionWithWeapon, "С оружием", "With weapon") \
    X(ConditionOnFoot, "Пешком", "On foot") \
    X(ConditionChatOpened, "Открыт чат", "Chat open") \
    X(ConditionDialogOpened, "Открыт диалог", "Dialog open") \
    X(InputModeText, "Свободный ввод", "Free text") \
    X(InputModeButtonsList, "Выбор из вариантов", "Pick from options") \
    X(InputModeButtonsListText, "Варианты + свой текст", "Options + custom text") \
    X(HotkeyModeModifierTrigger, "Модификатор + клавиша", "Modifier + key") \
    X(HotkeyModeOrderedCombo, "Последовательная комбинация", "Ordered combo") \
    X(QuickMenuModeHold, "Удержание", "Hold") \
    X(QuickMenuModeToggle, "Переключение", "Toggle") \
    X(SendLocalChat, "Локальный чат", "Local chat") \
    X(SendViaSamp, "Через SA:MP", "Send via SA:MP") \
    X(SendDirect, "Серверу", "To server") \
    X(SendNoSend, "Без отправки", "No send") \
    X(SendInsertChat, "Вставить в чат", "Insert into chat") \
    X(SendOpenChat, "Открыть чат", "Open chat") \
    X(SendDialog, "В диалог", "Into dialog") \
    X(SendClipboard, "В буфер обмена", "To clipboard") \
    X(SendLog, "В лог", "To log") \
    X(SendToast, "Уведомление", "Toast") \
    X(SendUnknown, "Неизвестно", "Unknown") \
    X(ToastBindConfirmExpired, "Подтверждение бинда истекло: %s", "Bind confirmation expired: %s") \
    X(ToastBindCanceled, "Бинд отменён: %s", "Bind canceled: %s") \
    X(ToastConditionBlocked, "Блокировка по условию: %s", "Blocked by condition: %s") \
    X(ToastFinishActiveInput, "Сначала завершите активный ввод.", "Finish the active input first.") \
    X(ToastSendLocalFailed, "Не удалось добавить сообщение в чат SA:MP.", "Failed to add a message to the SA:MP chat.") \
    X(ToastSendSampFailed, "Не удалось отправить текст в SA:MP.", "Failed to send text to SA:MP.") \
    X(ToastInsertChatFailed, "Не удалось вставить текст в чат.", "Failed to insert text into chat.") \
    X(ToastOpenChatFailed, "Не удалось открыть чат с текстом.", "Failed to open chat with text.") \
    X(ToastInsertDialogFailed, "Не удалось вставить текст в диалог.", "Failed to insert text into dialog.") \
    X(ToastClipboardFailed, "Не удалось записать текст в буфер обмена.", "Failed to copy text to the clipboard.") \
    X(ToastUnknownSendMethod, "Неизвестный способ отправки: %d", "Unknown send method: %d") \
    X(ToastBindSaved, "Бинд сохранён.", "Bind saved.") \
    X(ToastConfirmPrompt, "Подтвердить бинд \"%s\": [%s] принять, [%s] отменить", "Confirm bind \"%s\": [%s] accept, [%s] cancel") \
    X(ValidationBindNameRequired, "Укажите название бинда.", "Enter a bind name.") \
    X(ValidationExistingFolderRequired, "Укажите существующую папку.", "Select an existing folder.") \
    X(ValidationRepeatInterval, "Интервал повтора должен быть не меньше 50 мс.", "Repeat interval must be at least 50 ms.") \
    X(ValidationTriggerTextRequired, "Укажите текст триггера.", "Enter trigger text.") \
    X(ValidationCommandRequired, "Укажите команду.", "Enter a command.") \
    X(ValidationConfirmCancelKeysDifferent, "Клавиши подтверждения и отклонения должны отличаться.", "Confirm and cancel keys must be different.") \
    X(ValidationInputKeyRequired, "У каждого параметра должен быть служебный ключ.", "Each parameter must have a service key.") \
    X(ValidationInputKeyUnique, "Служебные ключи параметров должны быть уникальными.", "Parameter service keys must be unique.") \
    X(ValidationButtonsRequired, "Для параметра с вариантами нужен хотя бы один вариант.", "Parameters with options require at least one option.") \
    X(ValidationButtonsTextRequired, "Для параметра с вариантами нужен хотя бы один вариант со значением.", "Parameters with options require at least one option with a value.") \
    X(ValidationInvalidRegex, "Некорректное регулярное выражение триггера: %s", "Invalid trigger regex: %s") \
    X(FolderAdd, "+ Папка", "+ Folder") \
    X(FolderRename, "Переименовать", "Rename") \
    X(Delete, "Удалить", "Delete") \
    X(SearchFolders, "Поиск папок", "Search folders") \
    X(Name, "Название", "Name") \
    X(Save, "Сохранить", "Save") \
    X(Cancel, "Отмена", "Cancel") \
    X(DeleteFolderMoveBindsQuestion, "Удалить папку вместе с подпапками и биндами?", "Delete the folder together with its subfolders and binds?") \
    X(AddField, "+ Параметр", "+ Parameter") \
    X(AddButton, "+ Вариант", "+ Option") \
    X(FieldLabelFormat, "Параметр %d", "Parameter %d") \
    X(ButtonLabelFormat, "Вариант %d", "Option %d") \
    X(UnnamedField, "(без названия)", "(unnamed)") \
    X(FieldProperties, "Свойства параметра", "Parameter properties") \
    X(ButtonPropertiesTitle, "Свойства варианта", "Option properties") \
    X(InputFieldsListTitle, "Параметры", "Parameters") \
    X(InputFieldsEmpty, "Параметры ещё не настроены.", "No parameters are configured yet.") \
    X(ParameterQuestionSection, "Что увидит игрок", "What the player sees") \
    X(ParameterQuestionHint, "Название и подсказка этого параметра в окне перед запуском.", "Name and helper text shown before the bind starts.") \
    X(ParameterPrompt, "Заголовок вопроса", "Prompt title") \
    X(ParameterHintText, "Подсказка под вопросом", "Hint under the prompt") \
    X(ParameterResponseSection, "Какой ответ ожидается", "Expected answer") \
    X(ParameterResponseHint, "Выберите способ ответа и формат значения, которое подставится в сообщения.", "Choose how the player answers and which value will be inserted into messages.") \
    X(ParameterResponseType, "Тип ответа", "Answer type") \
    X(ParameterAllowMultiple, "Можно выбрать несколько", "Allow multiple choices") \
    X(ParameterJoinSeparator, "Разделитель при подстановке", "Separator for inserted values") \
    X(ParameterVariantsSection, "Варианты ответа", "Answer options") \
    X(ParameterVariantsHint, "Название показывается игроку, а значение подставляется в {{KEY}}.", "The label is shown to the player, while the value is inserted into {{KEY}}.") \
    X(ParameterAdvancedSection, "Расширенные настройки", "Advanced settings") \
    X(ParameterAdvancedHint, "Служебный ключ нужен для {{KEY}}, зависимостей и каскадных списков.", "The service key is used for {{KEY}}, dependencies, and cascading lists.") \
    X(ParameterSystemKey, "Служебный ключ", "Service key") \
    X(ParameterDependsOn, "Зависит от параметра", "Depends on parameter") \
    X(OptionName, "Название варианта", "Option label") \
    X(OptionValue, "Подставляемое значение", "Inserted value") \
    X(OptionHint, "Подсказка варианта", "Option hint") \
    X(Key, "Ключ", "Key") \
    X(Hint, "Подсказка", "Hint") \
    X(Mode, "Режим", "Mode") \
    X(MultiSelect, "Множественный выбор", "Multi-select") \
    X(Separator, "Разделитель", "Separator") \
    X(CascadeFromKey, "Каскад от ключа", "Cascade from key") \
    X(Buttons, "Варианты", "Options") \
    X(ButtonsStructuredTab, "Редактор", "Editor") \
    X(ButtonsBulkTab, "Списком", "Bulk") \
    X(ButtonListTitle, "Список вариантов", "Options list") \
    X(ButtonsEmpty, "Список вариантов пуст.", "The options list is empty.") \
    X(ButtonsBulkAddLine, "+ 1 строка", "+ 1 line") \
    X(ButtonsBulkAddFiveLines, "+ 5 строк", "+ 5 lines") \
    X(ButtonsBulkTemplateValueFormat, "значение_%d", "value_%d") \
    X(ButtonsBulkTemplateHintFormat, "Подсказка %d", "Hint %d") \
    X(ButtonsBulkSync, "Синхронизировать", "Sync") \
    X(ButtonsBulkCheck, "Проверить", "Check") \
    X(ButtonsBulkNormalize, "Нормализовать", "Normalize") \
    X(ButtonsBulkApply, "Применить", "Apply") \
    X(ButtonsFormatHint, "Формат строки: название | значение | подсказка | when", "Line format: label | value | hint | when") \
    X(ButtonsBulkEscapingHint, "Поддерживаются комментарии # и экранирование \\\\|, \\\\n, \\\\\\\\.", "Supports # comments and escaping \\\\|, \\\\n, \\\\\\\\.") \
    X(ButtonsBulkPreviewFormat, "Строк: %d, полезных: %d, пропущено: %d, кнопок после разбора: %d", "Lines: %d, used: %d, ignored: %d, buttons after parse: %d") \
    X(ButtonsBulkExtraPipesHint, "Лишние символы | после третьего столбца объединяются в when.", "Extra | after the third column are merged into when.") \
    X(ButtonWhen, "Правило показа", "Display rule") \
    X(ButtonWhenHint, "Показывать вариант, если у родительского параметра совпали label/text через |.", "Show this option when the parent parameter matches one of the label/text tokens separated by |.") \
    X(MoveUp, "Поднять выше", "Move up") \
    X(MoveDown, "Опустить ниже", "Move down") \
    X(CopyPlaceholder, "Скопировать {{KEY}}", "Copy {{KEY}}") \
    X(InputFieldPlaceholderFormat, "Подстановка: {{%s}}", "Insert token: {{%s}}") \
    X(NewBindTitle, "Новый бинд", "New bind") \
    X(EditBindTitle, "Редактирование бинда", "Edit bind") \
    X(EditorStartSection, "Как запускается", "How it starts") \
    X(EditorCollapseStartSection, "Скрыть блок запуска", "Hide launch block") \
    X(EditorExpandStartSection, "Показать блок запуска", "Show launch block") \
    X(EditorScenarioTab, "Сценарий", "Scenario") \
    X(EditorMultiInputTab, "Мульти-ввод", "Multi-input") \
    X(EditorInputFieldsTab, "Параметры", "Parameters") \
    X(EditorOpenConditions, "Условия", "Conditions") \
    X(EditorBack, "Назад", "Back") \
    X(EditorPreviousBind, "Предыдущий бинд", "Previous bind") \
    X(EditorNextBind, "Следующий бинд", "Next bind") \
    X(EditorUnsaved, "Несохранённые изменения", "Unsaved changes") \
    X(EditorTriggerHint, "Срабатывает, когда вы отправляете указанную фразу в чат или в виде команды.", "Triggers when you send the specified phrase to chat or as a command.") \
    X(EditorTriggerToggleHint, "Включить или выключить триггер по тексту.", "Enable or disable the text trigger.") \
    X(EditorTriggerPatternMode, "Режим шаблона", "Pattern mode") \
    X(EditorTriggerExample, "Например: Голова, [Гг]олова, ^дом\\d+$", "For example: Head, [Hh]ead, ^house\\d+$") \
    X(EditorScenarioHint, "Перетащите ручку слева, чтобы изменить порядок шагов.", "Drag the handle on the left to reorder steps.") \
    X(EditorAddStep, "+ Добавить шаг", "+ Add step") \
    X(EditorDuplicateStep, "Дублировать шаг", "Duplicate step") \
    X(EditorMoveStep, "Переместить шаг", "Move step") \
    X(EditorVariables, "Переменные", "Variables") \
    X(EditorVariablesTitle, "Переменные бинда", "Bind variables") \
    X(EditorVariablesHint, "Используйте {{KEY}} или {{1}} в сообщениях, чтобы подставить значения параметров.", "Use {{KEY}} or {{1}} in messages to insert parameter values.") \
    X(EditorVariablesEmpty, "Параметры ещё не настроены.", "No parameters are configured yet.") \
    X(EditorPreview, "Предпросмотр", "Preview") \
    X(EditorPreviewTitle, "Предпросмотр сценария", "Scenario preview") \
    X(EditorPreviewEmpty, "В сценарии пока нет шагов.", "There are no steps in the scenario yet.") \
    X(EditorDiscardTitle, "Несохранённые изменения", "Unsaved changes") \
    X(EditorDiscardMessage, "Изменения не сохранены. Продолжить и потерять правки?", "Changes are not saved. Continue and discard them?") \
    X(EditorDiscardAction, "Продолжить", "Continue") \
    X(EditorStay, "Остаться", "Stay") \
    X(EditorColumnMessage, "Сообщение", "Message") \
    X(EditorColumnPauseMs, "Пауза (мс)", "Pause (ms)") \
    X(EditorColumnDestination, "Куда", "Destination") \
    X(EditorConfirmationHint, "После триггера бинд ждёт отдельные клавиши подтверждения и отклонения.", "After a trigger, the bind waits for separate confirm and cancel keys.") \
    X(EditorMultiInputHint, "Каждая непустая строка станет отдельным шагом. Пустые строки игнорируются.", "Each non-empty line becomes a separate step. Empty lines are ignored.") \
    X(Enabled, "Включён", "Enabled") \
    X(Folder, "Папка", "Folder") \
    X(HotkeyMode, "Режим хоткея", "Hotkey mode") \
    X(HotkeyNotSet, "Не задано", "Not set") \
    X(HotkeyFormat, "Хоткей: %s", "Hotkey: %s") \
    X(ChangeHotkey, "Изменить хоткей", "Change hotkey") \
    X(ShowInQuickMenu, "Показывать в быстром меню", "Show in quick menu") \
    X(Repeat, "Повтор", "Repeat") \
    X(RepeatInterval, "Интервал повтора", "Repeat interval") \
    X(AddRow, "+ Строка", "+ Row") \
    X(ColumnSpacer, " ", " ") \
    X(ColumnEnabledShort, "Вкл", "On") \
    X(ColumnQuickShort, "БМ", "QM") \
    X(ColumnNumberShort, "№", "#") \
    X(ActionRemoveShort, "X", "X") \
    X(ColumnText, "Текст", "Text") \
    X(ColumnDelay, "Задержка", "Delay") \
    X(ColumnMethod, "Метод", "Method") \
    X(TextTrigger, "Триггер по тексту в чате", "Text trigger") \
    X(Command, "Команда", "Command") \
    X(TextConfirmation, "Требовать подтверждение по триггеру", "Require confirmation on trigger") \
    X(WaitWithoutTimeout, "Дожидаться подтверждения или отклонения", "Wait for confirmation or rejection") \
    X(ConfirmKeyFormat, "Клавиша подтверждения: %s", "Confirm key: %s") \
    X(CancelKeyFormat, "Клавиша отклонения: %s", "Cancel key: %s") \
    X(Change, "Изменить", "Change") \
    X(BlockingConditions, "Условия блокировки", "Blocking Conditions") \
    X(QuickMenuConditions, "Условия быстрого меню", "Quick Menu Conditions") \
    X(InputDialogSearchHint, "Поиск по названию, тексту или подсказке", "Search by label, text, or hint") \
    X(InputDialogNoOptions, "Нет доступных вариантов.", "No available options.") \
    X(InputDialogPreviewTitle, "Предпросмотр отправки", "Send preview") \
    X(InputDialogPreviewEmpty, "В сценарии нет сообщений для отправки.", "There are no messages to send in this scenario.") \
    X(AddBind, "+ Бинд", "+ Bind") \
    X(Edit, "Изменить", "Edit") \
    X(Run, "Запустить", "Run") \
    X(SearchBinds, "Поиск биндов", "Search binds") \
    X(ColumnLaunch, "Запуск", "Launch") \
    X(ColumnBind, "Бинд", "Bind") \
    X(ColumnName, "Название", "Name") \
    X(ColumnHotkey, "Клавиша", "Hotkey") \
    X(ColumnActions, "Действия", "Actions") \
    X(ActionMoveTo, "Переместить в...", "Move to...") \
    X(ActionDuplicate, "Дублировать", "Duplicate") \
    X(ActionBindLines, "Строки бинда...", "Bind lines...") \
    X(BindLinesTitle, "Строки бинда", "Bind lines") \
    X(BindLinesEmpty, "В бинде нет строк.", "This bind has no lines.") \
    X(Send, "Отправить", "Send") \
    X(BindListEntryFormat, "№%d %s", "#%d %s") \
    X(DeleteSelectedBindQuestion, "Удалить выбранный бинд?", "Delete the selected bind?") \
    X(CapturePrompt, "Нажмите комбинацию клавиш. Enter сохранить, Backspace очистить, Esc отменить.", "Press a key combination. Enter saves, Backspace clears, Esc cancels.") \
    X(CurrentCombinationFormat, "Текущая комбинация: %s", "Current combination: %s") \
    X(WaitingMouseButton, "Ожидание кнопки мыши...", "Waiting for a mouse button...") \
    X(Clear, "Очистить", "Clear") \
    X(Mouse, "Мышь", "Mouse") \
    X(FillBindParametersFormat, "Заполните параметры для бинда \"%s\".", "Fill in the parameters for bind \"%s\".") \
    X(Launch, "Запустить", "Launch") \
    X(BinderSectionTitle, "Биндер", "Binder") \
    X(QuickMenuWindowTitle, "Быстрое меню", "Quick menu") \
    X(QuickMenuFormat, "Быстрое меню: %s", "Quick menu: %s") \
    X(ChangeQuickMenuHotkey, "Изменить хоткей быстрого меню", "Change quick menu hotkey") \
    X(QuickMenuMode, "Режим быстрого меню", "Quick menu mode") \
    X(ColumnFolders, "Папки", "Folders") \
    X(ColumnBinds, "Бинды", "Binds")

enum class UiLanguage {
    Russian = 0,
    English,
};

enum class UiText {
#define APP_UI_TEXT_ENUM(id, ru, en) id,
    APP_UI_TEXTS(APP_UI_TEXT_ENUM)
#undef APP_UI_TEXT_ENUM
    Count,
};

class UiSettings {
public:
    static UiSettings& Instance();

    void Load();

    UiLanguage Language() const;
    void SetLanguage(UiLanguage language);

    bool AutoScaleEnabled() const;
    void SetAutoScaleEnabled(bool enabled);

    float ScaleMultiplier() const;
    void SetScaleMultiplier(float multiplier);
    const std::vector<unsigned int>& MenuToggleHotkey() const;
    void SetMenuToggleHotkey(const std::vector<unsigned int>& hotkey);

    void ResetToDefaults();

    float UpdateScale(const ImVec2& displaySize);
    float CurrentScale() const;
    float Scale(float value) const;
    ImVec2 Scale(const ImVec2& value) const;

    const char* Text(UiText id) const;
    std::string Format(UiText id, ...) const;
    const char* LanguageDisplayName(UiLanguage language) const;

private:
    void QueueSave() const;
    float ComputeAutoScale(const ImVec2& displaySize) const;

    UiLanguage language_ = UiLanguage::Russian;
    bool autoScaleEnabled_ = true;
    float scaleMultiplier_ = 1.0f;
    std::vector<unsigned int> menuToggleHotkey_{};
    float currentScale_ = 1.0f;
};
