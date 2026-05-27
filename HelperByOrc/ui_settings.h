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
    X(TabHud, "HUD", "HUD") \
    X(TabMisc, "Прочее", "Misc") \
    X(TabNotepad, "Блокнот", "Notepad") \
    X(TabSettings, "Настройки", "Settings") \
    X(TabHomeCompact, "ГЛ", "HM") \
    X(TabBinderCompact, "БН", "BD") \
    X(TabHudCompact, "HUD", "HUD") \
    X(TabMiscCompact, "ПР", "MS") \
    X(TabNotepadCompact, "БЛ", "NP") \
    X(TabSettingsCompact, "НС", "ST") \
    X(HomeIntro, "Оставлена чистая оболочка интерфейса: окно, логотип, боковое меню и вкладки без тестовых инструментов и служебных логов.", "The base interface shell is in place: window, logo, sidebar, and tabs without test tools or debug clutter.") \
    X(HomeInterfaceTitle, "Основа интерфейса", "Interface Foundation") \
    X(HomeInterfaceDesc, "Главное окно, кастомный title bar, логотип из ресурса и анимированное боковое меню уже готовы под дальнейшую разработку.", "The main window, custom title bar, resource logo, and animated sidebar are ready for the next feature work.") \
    X(HomeTabsTitle, "Порядок вкладок", "Tab Layout") \
    X(HomeTabsDesc, "Главная, Биндер, HUD, Прочее, Блокнот, Настройки. Порядок приведён к новому варианту без тестовых экранов.", "Home, Binder, HUD, Misc, Notepad, Settings. The tab order was cleaned up and no longer includes test screens.") \
    X(HudIntro, "Конструктор экранных виджетов с текстом, картинками, иконками и переменными.", "Screen widget builder with text, images, icons, and variables.") \
    X(HudSearchHint, "Поиск виджетов", "Search widgets") \
    X(HudNoWidgets, "Виджетов пока нет. Добавьте пустой виджет или пресет.", "No widgets yet. Add an empty widget or a preset.") \
    X(HudNoSelection, "Выберите виджет слева или создайте новый.", "Select a widget on the left or create a new one.") \
    X(HudAddWidget, "Виджет", "Widget") \
    X(HudDuplicateWidget, "Дублировать", "Duplicate") \
    X(HudPresets, "Пресеты", "Presets") \
    X(HudPresetWeapon, "Оружие", "Weapon") \
    X(HudPresetFreeText, "Свободный текст", "Free text") \
    X(HudDefaultWidgetName, "Новый виджет", "New widget") \
    X(HudSource, "Источник", "Source") \
    X(HudSourceInline, "Свой текст", "Inline text") \
    X(HudSourceNotepad, "Заметка из Блокнота", "Notepad note") \
    X(HudLinkedNoteMissing, "Связанная заметка не найдена.", "Linked note was not found.") \
    X(HudActionTagsDisabled, "Action-теги в HUD не выполняются: команды, скриншоты, фото, управление биндами и диалогами будут пропущены.", "Action tags are disabled in HUD: commands, screenshots, photos, bind control, and dialog actions will be skipped.") \
    X(HudText, "Текст виджета", "Widget text") \
    X(HudPosition, "Позиция", "Position") \
    X(HudAnchor, "Якорь", "Anchor") \
    X(HudAnchorTopLeft, "Слева сверху", "Top left") \
    X(HudAnchorTopCenter, "Сверху по центру", "Top center") \
    X(HudAnchorTopRight, "Справа сверху", "Top right") \
    X(HudAnchorCenterLeft, "Слева по центру", "Center left") \
    X(HudAnchorCenter, "Центр", "Center") \
    X(HudAnchorCenterRight, "Справа по центру", "Center right") \
    X(HudAnchorBottomLeft, "Слева снизу", "Bottom left") \
    X(HudAnchorBottomCenter, "Снизу по центру", "Bottom center") \
    X(HudAnchorBottomRight, "Справа снизу", "Bottom right") \
    X(HudOffsetX, "Смещение X", "Offset X") \
    X(HudOffsetY, "Смещение Y", "Offset Y") \
    X(HudPlaceOnScreen, "Разместить на экране", "Place on screen") \
    X(HudPlacementActive, "Перетащите виджет мышью. Esc отменит режим.", "Drag the widget with the mouse. Esc cancels placement.") \
    X(HudSize, "Размер", "Size") \
    X(HudAutoSize, "Авторазмер", "Auto size") \
    X(HudWidth, "Ширина", "Width") \
    X(HudHeight, "Высота", "Height") \
    X(HudScale, "Масштаб", "Scale") \
    X(HudStyle, "Стиль", "Style") \
    X(HudBackground, "Фон", "Background") \
    X(HudBackgroundAlpha, "Прозрачность фона", "Background opacity") \
    X(HudPaddingX, "Отступ X", "Padding X") \
    X(HudPaddingY, "Отступ Y", "Padding Y") \
    X(HudRounding, "Закругление", "Rounding") \
    X(HudBorder, "Рамка", "Border") \
    X(HudShadow, "Тень", "Shadow") \
    X(HudStyleWindow, "Окно", "Window") \
    X(HudStyleTextRows, "Текст и строки", "Text and rows") \
    X(HudStyleBorderShadow, "Рамка и тень", "Border and shadow") \
    X(HudStyleSeparators, "Разделители", "Separators") \
    X(HudTextColor, "Цвет текста", "Text color") \
    X(HudTextAlpha, "Прозрачность текста", "Text opacity") \
    X(HudItemSpacingX, "Интервал X", "Spacing X") \
    X(HudItemSpacingY, "Интервал между строками", "Line spacing") \
    X(HudItemInnerSpacingX, "Внутренний интервал X", "Inner spacing X") \
    X(HudItemInnerSpacingY, "Внутренний интервал Y", "Inner spacing Y") \
    X(HudBorderColor, "Цвет рамки", "Border color") \
    X(HudBorderAlpha, "Прозрачность рамки", "Border opacity") \
    X(HudBorderSize, "Толщина рамки", "Border size") \
    X(HudSeparatorColor, "Цвет разделителя", "Separator color") \
    X(HudSeparatorAlpha, "Прозрачность разделителя", "Separator opacity") \
    X(HudSeparatorSize, "Толщина разделителя", "Separator size") \
    X(HudShadowColor, "Цвет тени", "Shadow color") \
    X(HudShadowAlpha, "Прозрачность тени", "Shadow opacity") \
    X(HudShadowOffsetX, "Смещение тени X", "Shadow offset X") \
    X(HudShadowOffsetY, "Смещение тени Y", "Shadow offset Y") \
    X(HudVisibility, "Видимость", "Visibility") \
    X(HudVisibilityConditions, "Условия", "Conditions") \
    X(HudRefreshMs, "Обновление (мс)", "Refresh (ms)") \
    X(HudRefreshZeroWarning, "0 мс обновляет виджет каждый кадр. Это может снизить FPS, особенно при тяжёлых тегах или множестве виджетов.", "0 ms refreshes the widget every frame. This can reduce FPS, especially with expensive tags or many widgets.") \
    X(HudPreview, "Предпросмотр", "Preview") \
    X(HudImagesFolder, "Папка картинок", "Images folder") \
    X(MiscIntro, "Служебные тесты, отладочные кнопки и логи убраны. Оставлена только пустая оболочка раздела.", "Service tests, debug buttons, and logs were removed. Only the empty shell of this section remains.") \
    X(MiscShellTitle, "Техническая вкладка", "Technical Tab") \
    X(MiscShellDesc, "Раздел готов для будущих утилит, но сейчас не содержит ни тестовых действий, ни диагностических панелей.", "This section is ready for future utilities, but currently contains no test actions or diagnostic panels.") \
    X(MiscHomeIntro, "Во вкладке собраны отдельные служебные разделы. Нажмите на карточку нужного модуля, чтобы открыть его экран.", "This tab groups standalone utility sections. Click a module card to open its screen.") \
    X(MiscVariablesEntryDesc, "Открывает каталог встроенных переменных, описание тегов и sandbox-предпросмотр для быстрой проверки.", "Opens the built-in variables catalog, tag descriptions, and a sandbox preview for quick checks.") \
    X(MiscOpenSectionAction, "Открыть", "Open") \
    X(UnwantedTitle, "Игнорирование сообщений", "Message Ignoring") \
    X(UnwantedEntryDesc, "Глушит входящие и локальные сообщения через CChat и RakNet до попадания в чат и текстовые триггеры.", "Blocks incoming and local messages through CChat and RakNet before chat display and text triggers.") \
    X(UnwantedIntro, "Правила проверяются синхронно в единой точке. Тип regex использует C++ std::regex ECMAScript по UTF-8 строке; Lua-pattern здесь намеренно не поддерживается.", "Rules are checked synchronously in one place. Regex uses C++ std::regex ECMAScript on UTF-8 strings; Lua patterns are intentionally not supported here.") \
    X(UnwantedEnabled, "Включено", "Enabled") \
    X(UnwantedReload, "Перезагрузить", "Reload") \
    X(UnwantedEnableAll, "Все вкл.", "All on") \
    X(UnwantedDisableAll, "Все выкл.", "All off") \
    X(UnwantedStats, "Правил: %s | невалидных: %s | заблокировано: %s", "Rules: %s | invalid: %s | blocked: %s") \
    X(UnwantedLastBlocked, "Последнее: %s, правило %s, текст: %s", "Last: %s, rule %s, text: %s") \
    X(UnwantedLastBlockedEmpty, "Пока нет заблокированных сообщений.", "No blocked messages yet.") \
    X(UnwantedNormalizer, "Нормализация", "Normalizer") \
    X(UnwantedStripColors, "Убрать {RRGGBB}", "Strip {RRGGBB}") \
    X(UnwantedCollapseWhitespace, "Сжать пробелы", "Collapse whitespace") \
    X(UnwantedTrim, "Обрезать края", "Trim") \
    X(UnwantedMaxPatternLength, "Лимит regex", "Regex limit") \
    X(UnwantedRules, "Правила", "Rules") \
    X(UnwantedSelectAll, "Выделить все", "Select all") \
    X(UnwantedClearSelection, "Снять выделение", "Clear selection") \
    X(UnwantedDeleteSelected, "Удалить выбранные", "Delete selected") \
    X(UnwantedEnableSelected, "Выбранные вкл.", "Selected on") \
    X(UnwantedDisableSelected, "Выбранные выкл.", "Selected off") \
    X(UnwantedRemoveDuplicates, "Дубли", "Duplicates") \
    X(UnwantedSortByType, "По типу", "By type") \
    X(UnwantedSortByText, "По тексту", "By text") \
    X(UnwantedNoRules, "Правил пока нет.", "No rules yet.") \
    X(UnwantedTypeLiteral, "literal", "literal") \
    X(UnwantedTypeRegex, "regex", "regex") \
    X(UnwantedNoCase, "nocase", "nocase") \
    X(UnwantedWholeWord, "слово", "word") \
    X(UnwantedInvalidRule, "Ошибка", "Error") \
    X(UnwantedRuleOk, "OK", "OK") \
    X(UnwantedAddRule, "Добавление", "Add Rule") \
    X(UnwantedRuleTextHint, "Текст literal или C++ regex", "Literal text or C++ regex") \
    X(UnwantedAddRuleAction, "Добавить", "Add") \
    X(UnwantedTester, "Тестер", "Tester") \
    X(UnwantedTesterHint, "Сообщение для проверки", "Message to test") \
    X(UnwantedTestAction, "Проверить", "Test") \
    X(UnwantedTesterMatched, "Совпало: правило %s, кандидат: %s", "Matched: rule %s, candidate: %s") \
    X(UnwantedTesterNoMatch, "Совпадений нет.", "No match.") \
    X(UnwantedRegexHelper, "Regex-helper", "Regex Helper") \
    X(UnwantedHelperInputHint, "Вставьте пример сообщения", "Paste a sample message") \
    X(UnwantedHelperAnchors, "^...$", "^...$") \
    X(UnwantedHelperColors, "цвета", "colors") \
    X(UnwantedHelperNumbers, "числа", "numbers") \
    X(UnwantedHelperMoney, "деньги", "money") \
    X(UnwantedHelperTime, "время", "time") \
    X(UnwantedHelperNick, "ник", "nick") \
    X(UnwantedHelperBracketTag, "[тег]", "[tag]") \
    X(UnwantedHelperGenerate, "Сгенерировать", "Generate") \
    X(UnwantedHelperExact, "Точный escaped regex", "Exact escaped regex") \
    X(UnwantedHelperGeneralized, "Обобщённый regex", "Generalized regex") \
    X(UnwantedAddExact, "Добавить точный", "Add exact") \
    X(UnwantedCopyExact, "Копировать точный", "Copy exact") \
    X(UnwantedAddGeneralized, "Добавить общий", "Add generalized") \
    X(UnwantedCopyGeneralized, "Копировать общий", "Copy generalized") \
    X(UnwantedErrorTooLong, "Длина больше лимита %s.", "Length exceeds limit %s.") \
    X(UnwantedErrorEmpty, "Пустое правило не выполняется.", "Empty rule is skipped.") \
    X(TagsKindSimple, "Простая", "Simple") \
    X(TagsKindFunction, "Функциональная", "Function") \
    X(TagsBuiltinIdDescription, "Возвращает ваш локальный ID игрока через SampApi::Local_ID().", "Returns your local player ID via SampApi::Local_ID().") \
    X(TagsBuiltinNickDescription, "Возвращает ваш текущий ник через GetNameID(Local_ID()).", "Returns your current nickname via GetNameID(Local_ID()).") \
    X(TagsBuiltinThisbindDescription, "Возвращает безопасный selector текущего запущенного bind для других bind-тегов.\n\nФормат результата:\n- bind в root категории: \"Имя\" \"\"\n- bind в папке: \"Имя\" \"Папка\" или \"Имя\" \"Папка/Вложенная\"\n\nИспользуйте именно этот тег, когда действие должно ссылаться на текущий bind: [bindstop({thisbind})], [bindpause({thisbind})], [bindended({thisbind})].\n\nВажно:\n- Работает только во время выполнения bind.\n- Это selector, а не просто имя: пустая папка сохраняется как \"\", поэтому root-bind не перепутается с bind в папке.\n- В HUD, preview и sandbox без runtime-контекста возвращает пустую строку.", "Returns a safe selector for the currently running bind, intended for other bind tags.\n\nResult format:\n- bind in category root: \"Name\" \"\"\n- bind in a folder: \"Name\" \"Folder\" or \"Name\" \"Folder/Nested\"\n\nUse this tag when an action must target the current bind: [bindstop({thisbind})], [bindpause({thisbind})], [bindended({thisbind})].\n\nNotes:\n- Works only while a bind is running.\n- This is a selector, not just a name: the empty folder is preserved as \"\", so a root bind is not mixed with a folder bind.\n- In HUD, preview, and sandbox without runtime context it returns an empty string.") \
    X(TagsBuiltinThisbindSelectorDescription, "Явное имя для того же значения, что и {thisbind}. Возвращает selector текущего bind в формате \"Имя\" \"Папка\".\n\nИспользуйте, если в тексте нужно подчеркнуть, что значение предназначено именно для selector-аргумента: [bindenable({thisbindselector})].", "Explicit name for the same value as {thisbind}. Returns the current bind selector in \"Name\" \"Folder\" format.\n\nUse it when the text should make it clear that the value is intended for a selector argument: [bindenable({thisbindselector})].") \
    X(TagsBuiltinThisbindNameDescription, "Возвращает только display name текущего запущенного bind без кавычек и без папки.\n\nПодходит для текста сообщений, уведомлений и логов: Текущий bind: {thisbindname}.\n\nНе рекомендуется использовать как единственный selector в bind-действиях, если имена могут повторяться. Для действий безопаснее {thisbind} или {thisbindselector}.", "Returns only the display name of the currently running bind, without quotes and without folder.\n\nUseful in messages, notifications, and logs: Current bind: {thisbindname}.\n\nDo not use it as the only selector for bind actions if names can repeat. {thisbind} or {thisbindselector} is safer for actions.") \
    X(TagsBuiltinThisbindFolderDescription, "Возвращает папку текущего запущенного bind.\n\nФормат:\n- root категории: пустая строка\n- обычная папка: Команды\n- вложенная папка: Команды/Фрапс\n\nИспользуйте для текста, условий и ручной сборки selector: [bindrandom(\"{thisbindfolder}\")]. Если нужен текущий bind целиком, используйте {thisbind}.", "Returns the folder of the currently running bind.\n\nFormat:\n- category root: empty string\n- regular folder: Commands\n- nested folder: Commands/Fraps\n\nUse it in text, conditions, or when manually building selectors: [bindrandom(\"{thisbindfolder}\")]. If you need the full current bind selector, use {thisbind}.") \
    X(TagsBuiltinThiscategoryDescription, "Возвращает категорию, из которой был запущен текущий bind.\n\nЭто runtime-категория запуска, а не выбранная вкладка Binder UI. Если bind стартовал из категории \"Основные\", тег вернёт \"Основные\" даже после переключения вкладки в интерфейсе.\n\nПолезно для явного 3-го аргумента selector: [bindstart(\"fb\" \"\" \"{thiscategory}\")]. Вне запущенного bind возвращает пустую строку.", "Returns the category from which the current bind was launched.\n\nThis is the launch runtime category, not the selected Binder UI tab. If the bind started from category \"Main\", this tag still returns \"Main\" even after the UI tab changes.\n\nUseful as the explicit third selector argument: [bindstart(\"fb\" \"\" \"{thiscategory}\")]. Outside a running bind it returns an empty string.") \
    X(TagsBuiltinBindStopAllDescription, "Останавливает все запущенные bind после текущей строки и ничего не вставляет в текст.\n\nСинтаксис: {bindstopall}\n\nГде работает:\n- только внутри выполняющегося bind;\n- действие ставится в очередь и выполняется после текущей строки;\n- в HUD, preview, sandbox и обычной подстановке без runtime-контекста игнорируется.\n\nЕсли запущенных bind нет, это не считается ошибкой.", "Stops all running binds after the current line and inserts no text.\n\nSyntax: {bindstopall}\n\nWhere it works:\n- only inside a running bind;\n- the action is queued and runs after the current line;\n- ignored in HUD, preview, sandbox, and regular expansion without runtime context.\n\nIf no binds are running, this is not an error.") \
    X(TagsBuiltinTargetIdDescription, "Возвращает ID игрока, в которого вы целились последним.", "Returns the ID of the player you aimed at most recently.") \
    X(TagsBuiltinTargetNickDescription, "Возвращает ник игрока, в которого вы целились последним.", "Returns the nickname of the player you aimed at most recently.") \
    X(TagsBuiltinTargetRpNickDescription, "Возвращает RP-ник последней цели: Walcher_Flett станет Walcher Flett.", "Returns the RP nickname of the last target: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinTargetNameDescription, "Возвращает имя из ника последней цели до символа подчёркивания.", "Returns the first name from the last target nickname before the underscore.") \
    X(TagsBuiltinTargetSurnameDescription, "Возвращает фамилию из ника последней цели после символа подчёркивания.", "Returns the surname from the last target nickname after the underscore.") \
    X(TagsBuiltinTargetHealthDescription, "Возвращает здоровье игрока, в которого вы целились последним. Если цель недоступна или значение не удалось получить, возвращает пустую строку.", "Returns the health of the player you aimed at most recently. If the target is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinTargetArmourDescription, "Возвращает броню игрока, в которого вы целились последним. Если цель недоступна или значение не удалось получить, возвращает пустую строку.", "Returns the armour of the player you aimed at most recently. If the target is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinClosestIdDescription, "Возвращает ID ближайшего к вам стримнутого игрока. Считается прямо в момент раскрытия переменной.", "Returns the ID of the streamed player closest to you. It is evaluated on demand when the variable is expanded.") \
    X(TagsBuiltinClosestIdToCenterDescription, "Возвращает ID стримнутого игрока, который находится ближе всего к центру экрана среди видимых на экране.", "Returns the ID of the streamed player closest to the screen center among those currently visible on screen.") \
    X(TagsBuiltinClosestNameDescription, "Возвращает имя из ника ближайшего к вам стримнутого игрока.", "Returns the first name from the nickname of the streamed player closest to you.") \
    X(TagsBuiltinClosestSurnameDescription, "Возвращает фамилию из ника ближайшего к вам стримнутого игрока.", "Returns the surname from the nickname of the streamed player closest to you.") \
    X(TagsBuiltinArmourDescription, "Возвращает вашу текущую броню. Если брони нет или значение недоступно, вернёт 0.", "Returns your current armour. If there is no armour or the value is unavailable, it returns 0.") \
    X(TagsBuiltinHealthDescription, "Возвращает ваше текущее здоровье локального игрока.", "Returns the current health of the local player.") \
    X(TagsBuiltinDateDescription, "Возвращает текущую локальную дату в формате ДД.ММ.ГГГГ.", "Returns the current local date formatted as DD.MM.YYYY.") \
    X(TagsBuiltinMySkinDescription, "Возвращает ID текущего скина локального игрока через model index педа.", "Returns the current local player skin ID via the ped model index.") \
    X(TagsBuiltinMyWeaponDescription, "Возвращает название текущего оружия локального игрока.", "Returns the local player's current weapon name.") \
    X(TagsBuiltinMyWeaponIdDescription, "Возвращает ID текущего оружия локального игрока.", "Returns the local player's current weapon ID.") \
    X(TagsBuiltinMyWeaponClipDescription, "Возвращает количество патронов в текущей обойме локального игрока.", "Returns the current ammo-in-clip value for the local player's weapon.") \
    X(TagsBuiltinMyMoneyDescription, "Возвращает количество денег на руках у локального игрока.", "Returns the amount of money currently carried by the local player.") \
    X(TagsBuiltinFpsDescription, "Возвращает текущий FPS клиента как целое число.", "Returns the current client FPS as an integer.") \
    X(TagsBuiltinGetVehTypeDescription, "Возвращает тип транспорта, в котором сейчас находится локальный игрок. Если вы не в транспорте, возвращает пустую строку.", "Returns the type of vehicle the local player is currently in. If you are not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinScreenDescription, "Делает скриншот игры и сохраняет его в папку данных GTA SA\\\\HelperByOrc\\\\screens.", "Takes a game screenshot and saves it to the GTA SA userfiles folder\\\\HelperByOrc\\\\screens.") \
    X(TagsBuiltinScreenFunctionDescription, "Делает скриншот игры и сохраняет его в указанную подпапку внутри папки данных GTA SA\\\\HelperByOrc\\\\screens.", "Takes a game screenshot and saves it to the specified subfolder inside the GTA SA userfiles folder\\\\HelperByOrc\\\\screens.") \
    X(TagsBuiltinTPhotoDescription, "Делает игровое фото через механику фотоаппарата GTA SA и сохраняет его в галерею игры.", "Takes an in-game camera photo through GTA SA's photo mechanic and saves it to the game's gallery.") \
    X(TagsBuiltinNickRpDescription, "Возвращает ваш ник в RP-виде: Walcher_Flett станет Walcher Flett.", "Returns your nickname in RP form: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinNameDescription, "Возвращает имя из ника до символа подчёркивания: Walcher_Flett станет Walcher.", "Returns the first name before the underscore: Walcher_Flett becomes Walcher.") \
    X(TagsBuiltinSurnameDescription, "Возвращает фамилию из ника после символа подчёркивания: Walcher_Flett станет Flett.", "Returns the surname after the underscore: Walcher_Flett becomes Flett.") \
    X(TagsBuiltinTimeDescription, "Возвращает текущее локальное время в формате %H:%M:%S.", "Returns the current local time formatted as %H:%M:%S.") \
    X(TagsBuiltinTimeNoSecDescription, "Возвращает текущее локальное время без секунд в формате %H:%M.", "Returns the current local time without seconds in %H:%M format.") \
    X(TagsBuiltinDialogActiveDescription, "Возвращает true, если сейчас открыт активный диалог SA:MP. Иначе возвращает false.", "Returns true if a SA:MP dialog is currently open. Otherwise returns false.") \
    X(TagsBuiltinDialogCaptionDescription, "Возвращает заголовок активного диалога SA:MP. Если диалог не открыт, возвращает пустую строку.", "Returns the caption of the active SA:MP dialog. If no dialog is open, returns an empty string.") \
    X(TagsBuiltinDialogGetSelectedItemDescription, "Возвращает текст выбранного пункта активного диалога со списком. Если список недоступен или ничего не выбрано, возвращает пустую строку.", "Returns the text of the currently selected item in the active list dialog. If the list is unavailable or nothing is selected, it returns an empty string.") \
    X(TagsBuiltinDialogEditboxTextDescription, "Возвращает текущее содержимое editbox активного диалога SA:MP. Работает для input/password диалогов. Если editbox нет, возвращает пустую строку.", "Returns the current contents of the active SA:MP dialog edit box. Works for input/password dialogs. If there is no edit box, it returns an empty string.") \
    X(TagsBuiltinDialogSelectedIndexDescription, "Возвращает текущий выбранный индекс активного list/tablist диалога. Индекс 0-based. Если список недоступен или ничего не выбрано, возвращает пустую строку.", "Returns the currently selected index of the active list/tablist dialog. The index is 0-based. If the list is unavailable or nothing is selected, it returns an empty string.") \
    X(TagsBuiltinDialogGetIdDescription, "Возвращает ID активного диалога SA:MP. Если диалог не открыт, возвращает пустую строку.", "Returns the ID of the active SA:MP dialog. If no dialog is open, returns an empty string.") \
    X(TagsBuiltinTimefDescription, "Пишет текущее локальное время в указанном формате strftime. В конце формата обязательно нужна точка с запятой ;\n%a — сокращённое название дня недели\n%A — полное название дня недели\n%b — сокращённое название месяца\n%B — полное название месяца\n%c — дата и время целиком\n%d — день месяца [01-31]\n%H — час в 24-часовом формате [00-23]\n%I — час в 12-часовом формате [01-12]\n%M — минуты [00-59]\n%m — месяц [01-12]\n%p — am или pm\n%S — секунды [00-61]\n%w — день недели [0-6, где 0 — воскресенье]\n%x — дата\n%X — время\n%Y — полный год\n%y — двухзначный год [00-99]\n%% — символ %", "Prints the current local time using the specified strftime format. The format must end with a semicolon ;\n%a — abbreviated weekday name\n%A — full weekday name\n%b — abbreviated month name\n%B — full month name\n%c — full date and time\n%d — day of month [01-31]\n%H — hour in 24-hour format [00-23]\n%I — hour in 12-hour format [01-12]\n%M — minute [00-59]\n%m — month [01-12]\n%p — am or pm\n%S — second [00-61]\n%w — weekday [0-6, Sunday = 0]\n%x — date\n%X — time\n%Y — full year\n%y — two-digit year [00-99]\n%% — percent sign") \
    X(TagsBuiltinNickFunctionDescription, "Возвращает ник игрока по указанному ID. Работает и в обычном чате, и в биндах.", "Returns a player's nickname for the specified ID. Works in regular chat and in binds.") \
    X(TagsBuiltinRpNickFunctionDescription, "Возвращает RP-ник игрока по указанному ID: Walcher_Flett станет Walcher Flett.", "Returns a player's RP nickname for the specified ID: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinNameFunctionDescription, "Возвращает имя из ника игрока по указанному ID.", "Returns the first name from the nickname of the specified player ID.") \
    X(TagsBuiltinSurnameFunctionDescription, "Возвращает фамилию из ника игрока по указанному ID.", "Returns the surname from the nickname of the specified player ID.") \
    X(TagsBuiltinParamcmdDescription, "Достаёт параметры из команды, которой был запущен бинд. Поддерживает селекторы 1, 1+, 3-, 2-4.", "Extracts arguments from the command that launched the bind. Supports selectors like 1, 1+, 3-, and 2-4.") \
    X(TagsBuiltinKeyEmulateDescription, "Эмулирует одно нажатие указанной виртуальной клавиши Windows и ничего не вставляет в текст. В sandbox-предпросмотре не срабатывает.", "Emulates a single press of the specified Windows virtual key and inserts no text. It stays inactive in sandbox previews.") \
    X(TagsBuiltinMathDescription, "Вычисляет арифметическое выражение. Поддерживает +, -, *, /, %, скобки и унарные знаки.", "Evaluates an arithmetic expression. Supports +, -, *, /, %, parentheses, and unary signs.") \
    X(TagsBuiltinNumberWithDotsDescription, "Форматирует число, разделяя целую часть точками по тысячам: 1000 станет 1.000, а -12345.67 станет -12.345.67.", "Formats a number by inserting dots every three digits in the integer part: 1000 becomes 1.000 and -12345.67 becomes -12.345.67.") \
    X(TagsBuiltinArmourFunctionDescription, "Возвращает броню игрока по ID. Если игрок недоступен или значение не удалось получить, возвращает пустую строку.", "Returns a player's armour by ID. If the player is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinHealthFunctionDescription, "Возвращает здоровье игрока по ID. Если игрок недоступен или значение не удалось получить, возвращает пустую строку.", "Returns a player's health by ID. If the player is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinSkinFunctionDescription, "Возвращает model index педа игрока по указанному ID. Если пед игрока не найден или не застримлен, возвращает пустую строку.", "Returns the ped model index for the specified player ID. If the player's ped is not found or not streamed in, it returns an empty string.") \
    X(TagsBuiltinNickColorFunctionDescription, "Возвращает цвет ника игрока в HUD-разметке {RRGGBB}. Если цвет недоступен, возвращает белый.", "Returns the player's nick color as HUD markup {RRGGBB}. If the color is unavailable, it returns white.") \
    X(TagsBuiltinCarFunctionDescription, "Возвращает название транспорта игрока по ID. Если игрок не в транспорте или не застримлен, возвращает пустую строку.", "Returns the vehicle name for a player by ID. If the player is not in a vehicle or is not streamed in, it returns an empty string.") \
    X(TagsBuiltinCarHealthFunctionDescription, "Возвращает здоровье транспорта игрока по ID. Если транспорт недоступен, возвращает пустую строку.", "Returns the vehicle health for a player by ID. If the vehicle is unavailable, it returns an empty string.") \
    X(TagsBuiltinKeyDownDescription, "Зажимает указанную виртуальную клавишу Windows на заданное количество миллисекунд. Формат: [keydown(КодКлавиши;Мс)]. В уже запущенном бинде тег ставит выполнение на паузу до конца удержания.", "Holds the specified Windows virtual key for the requested number of milliseconds. Format: [keydown(KeyCode;Ms)]. In a running bind, the tag pauses execution until the hold finishes.") \
    X(TagsBuiltinStrLowDescription, "Возвращает текст в нижнем регистре. Работает и для кириллицы.", "Returns the text in lowercase. Cyrillic text is supported as well.") \
    X(TagsBuiltinAddTimeDescription, "Возвращает текущее локальное время плюс указанное смещение. Поддерживает форматы MM:SS и HH:MM:SS. Результат всегда выводится как %H:%M:%S.", "Returns the current local time plus the specified offset. Supports MM:SS and HH:MM:SS formats. The result is always printed as %H:%M:%S.") \
    X(TagsBuiltinRandomDescription, "Генерирует случайное значение. [random()] даёт число от -2147483647 до 2147483647, [random(10)] — от 1 до 10, [random(20-30)] — от 20 до 30, [random(Да;Нет;Не знаю)] — случайный вариант из списка.", "Generates a random value. [random()] returns a number from -2147483647 to 2147483647, [random(10)] returns 1 to 10, [random(20-30)] returns 20 to 30, and [random(Yes;No;Maybe)] picks a random option from the list.") \
    X(TagsBuiltinIfAndOrDescription, "Возвращает один из двух вариантов по условию.\n\nСинтаксис:\n[ifandor(Условие?Верно:Неверно)]\n\nКак это работает:\n1. Сначала вычисляется только Условие.\n2. Если условие истинно, раскрывается и выполняется только часть Верно.\n3. Если условие ложно, раскрывается и выполняется только часть Неверно.\n4. Невыбранная ветка вообще не раскрывается и не выполняется.\n\nЭто важно:\n- Внутри Верно и Неверно можно безопасно использовать функциональные переменные с действиями, например [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Выполнится только выбранная ветка.\n- Внутри самого условия side effects запрещены: условие нужно только для проверки.\n\nЧто можно писать в условии:\n- операторы сравнения: ==, !=, >, >=, <, <=\n- логические операторы: and, or, not\n- круглые скобки: ( )\n- числа: 1, 74, 12.5\n- строки в кавычках: \"text\" или 'text'\n- булевы значения: true, false\n- обычные и безопасные функциональные переменные после подстановки, например {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nКак сравниваются значения:\n- Если обе стороны похожи на числа, сравнение будет числовым.\n- Иначе сравнение будет строковым.\n\nПримеры:\n- [ifandor({id}==74?Мой id 74:Мой id не 74)]\n- [ifandor(1>0?[bindstart(30)]:[bindstart(\"fb\" \"\")])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?День:Не день)]\n- [ifandor([bindended({thisbind})]==1?Бинд завершён:Бинд ещё работает)]\n\nВажно по синтаксису:\n- Формат строго один: Условие?Верно:Неверно\n- Если в строках есть текст, лучше брать его в кавычки при сравнении.\n- Старый формат вида @...@ не используется. Используйте текущий синтаксис [ ... ].", "Returns one of two branches by condition.\n\nSyntax:\n[ifandor(Condition?True:False)]\n\nHow it works:\n1. Only the Condition is evaluated first.\n2. If the condition is true, only the True branch is expanded and executed.\n3. If the condition is false, only the False branch is expanded and executed.\n4. The branch that was not selected is never expanded or executed.\n\nThis matters:\n- You can safely use action-oriented functional variables inside True and False, such as [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Only the selected branch runs.\n- Side effects are forbidden inside the condition itself; the condition is for checking only.\n\nSupported inside the condition:\n- comparison operators: ==, !=, >, >=, <, <=\n- logical operators: and, or, not\n- parentheses: ( )\n- numbers: 1, 74, 12.5\n- quoted strings: \"text\" or 'text'\n- boolean values: true, false\n- regular and safe functional variables after expansion, such as {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nHow values are compared:\n- If both sides look like numbers, comparison is numeric.\n- Otherwise comparison is string-based.\n\nExamples:\n- [ifandor({id}==74?My id is 74:My id is not 74)]\n- [ifandor(1>0?[bindstart(30)]:[bindstart(\"fb\" \"\")])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?Day:Not day)]\n- [ifandor([bindended({thisbind})]==1?Bind finished:Bind still running)]\n\nSyntax notes:\n- The format is strictly Condition?True:False\n- If you compare text, quoting it is recommended.\n- The old @...@ form is not used. Use the current [ ... ] syntax.") \
    X(TagsBuiltinGetVehTypeFunctionDescription, "Возвращает тип транспорта игрока по указанному ID. Если игрок не найден или не находится в транспорте, возвращает пустую строку.", "Returns the type of vehicle used by the specified player ID. If the player is not found or is not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinWaitDescription, "Переопределяет паузу до следующей строки у уже запущенного бинда. Работает как обычная задержка между строками и ничего не вставляет в текст.", "Overrides the delay before the next line of an already running bind. Works like the regular delay between lines and inserts no text.") \
    X(TagsBuiltinDialogCloseDescription, "Закрывает активный диалог SA:MP. Параметр 1 отправляет положительный ответ (Enter/OK), параметр 0 отправляет отрицательный ответ (Esc/Cancel). Ничего не вставляет в текст.", "Closes the active SA:MP dialog. Parameter 1 sends the positive response (Enter/OK), parameter 0 sends the negative response (Esc/Cancel). Inserts no text.") \
    X(TagsBuiltinDialogSetTextDescription, "Устанавливает текст в editbox активного диалога SA:MP. Работает только для input/password диалогов и ничего не вставляет в текст.", "Sets the text of the active SA:MP dialog editbox. Works only for input/password dialogs and inserts no text.") \
    X(TagsBuiltinDialogWaitOpenDescription, "Ничего не вставляет в текст и дожидается появления активного диалога SA:MP до 3 секунд. Если диалог не открылся, текущий бинд будет остановлен.", "Inserts no text and waits up to 3 seconds for an active SA:MP dialog to appear. If no dialog opens, the current bind is stopped.") \
    X(TagsBuiltinDialogWaitCloseDescription, "Ничего не вставляет в текст и ставит текущий бинд на паузу, пока открыт диалог SA:MP. После закрытия диалога выполнение продолжается.", "Inserts no text and pauses the current bind while a SA:MP dialog remains open. Execution resumes after the dialog closes.") \
    X(TagsBuiltinDialogItemDescription, "Открывает активный диалоговый пункт по номеру или по части текста. Для чисел поддерживается привычная 1-based форма: [dialogitem(1)] нажмёт первый пункт. В list/tablist диалогах можно открыть picker по плюсику рядом с тегом и сразу скопировать готовый пример.", "Opens the active dialog item by number or by partial text. Numeric arguments support the familiar 1-based form: [dialogitem(1)] presses the first item. In list/tablist dialogs you can open the picker via the plus button next to the tag and copy a ready-made example.") \
    X(TagsBuiltinDialogSelectDescription, "Выбирает пункт активного list/tablist диалога без нажатия Enter. Поддерживает номер или часть текста пункта. Для чисел используется привычная 1-based форма: [dialogselect(1)] выберет первый пункт, но не отправит диалог.", "Selects an item in the active list/tablist dialog without pressing Enter. Supports an item number or a partial text match. Numeric arguments use the familiar 1-based form: [dialogselect(1)] selects the first item without submitting the dialog.") \
    X(TagsBuiltinDialogWaitIdDescription, "Ничего не вставляет в текст и ждёт появления конкретного dialog id до 3 секунд. Если нужный диалог уже открыт, выполнение продолжается сразу. Если за 3 секунды нужный id так и не появился, текущий бинд будет остановлен.", "Inserts no text and waits up to 3 seconds for a specific dialog id to appear. If the requested dialog is already open, execution continues immediately. If that id does not appear within 3 seconds, the current bind is stopped.") \
    X(TagsBuiltinDialogResponseDescription, "Универсально отвечает на активный диалог одной переменной.\n\nСинтаксис:\n[dialogresponse(button;item;text)]\n\nПараметры:\n- button — обязательный. 1 = положительный ответ (Enter/OK), 0 = отрицательный ответ (Esc/Cancel).\n- item — необязательный. Используется только для list/tablist диалогов. Можно указать номер пункта или часть его текста.\n- text — необязательный. Используется только для input/password диалогов и задаёт текст для editbox.\n\nКак это работает:\n- Для msgbox достаточно button: [dialogresponse(1)]\n- Для list/tablist можно выбрать пункт и сразу подтвердить: [dialogresponse(1;3;)] или [dialogresponse(1;Инвентарь;)]\n- Для input/password можно передать текст и сразу подтвердить: [dialogresponse(1;;Пример)]\n- Для отрицательного ответа достаточно [dialogresponse(0)] — item и text будут проигнорированы.\n\nВажно:\n- Формат всегда один и тот же: button;item;text\n- Если какой-то параметр не нужен, оставьте его пустым, но сохраните разделители. Например: [dialogresponse(1;;Пример)]\n- item для чисел работает в привычной 1-based форме, как [dialogitem(1)]\n- item по тексту ищется по части текста без учёта регистра\n- text и item перед отправкой безопасно раскрывают обычные переменные и pure-функции, но не выполняют action-теги\n- Сам тег ничего не вставляет в текст и только отправляет ответ в активный диалог\n\nПримеры:\n- [dialogresponse(1)]\n- [dialogresponse(0)]\n- [dialogresponse(1;1;)]\n- [dialogresponse(1;Навыки персонажа;)]\n- [dialogresponse(1;;{nick})]\n- [dialogresponse(1;;[timef(%H:%M:%S;)])]", "Sends a universal response to the active dialog with a single variable.\n\nSyntax:\n[dialogresponse(button;item;text)]\n\nParameters:\n- button — required. 1 = positive response (Enter/OK), 0 = negative response (Esc/Cancel).\n- item — optional. Used only for list/tablist dialogs. You can pass an item number or a partial item text.\n- text — optional. Used only for input/password dialogs and sets the edit box text.\n\nHow it works:\n- For a msgbox only button is needed: [dialogresponse(1)]\n- For list/tablist dialogs you can select an item and confirm it immediately: [dialogresponse(1;3;)] or [dialogresponse(1;Inventory;)]\n- For input/password dialogs you can pass text and confirm immediately: [dialogresponse(1;;Example)]\n- For a negative response, [dialogresponse(0)] is enough — item and text are ignored.\n\nImportant:\n- The format is always button;item;text\n- If a parameter is not needed, leave it empty but keep the separators. Example: [dialogresponse(1;;Example)]\n- Numeric item values use the familiar 1-based form, like [dialogitem(1)]\n- Text item lookup is case-insensitive and matches by partial text\n- item and text safely expand regular variables and pure functions before submit, but do not execute action tags\n- The tag inserts no text and only sends a response to the active dialog\n\nExamples:\n- [dialogresponse(1)]\n- [dialogresponse(0)]\n- [dialogresponse(1;1;)]\n- [dialogresponse(1;Skills;)]\n- [dialogresponse(1;;{nick})]\n- [dialogresponse(1;;[timef(%H:%M:%S;)])]") \
    X(TagsBuiltinDialogTextDescription, "Возвращает токен текста активного диалога по 0-based индексу. Текст разбивается примерно как в Lua-версии: пробелы и табы разделяют слова, а символы [](){} считаются отдельными токенами. По плюсику рядом с тегом можно открыть список доступных индексов.", "Returns a token from the active dialog text by 0-based index. The text is split similarly to the Lua version: spaces and tabs separate words, while [](){} are treated as standalone tokens. Use the plus button next to the tag to open the list of available indexes.") \
    X(TagsBuiltinSaveDialogDescription, "Сохраняет активный диалог в .txt файл в папку данных GTA SA\\\\HelperByOrc\\\\saved\\\\dialogs. Если аргумент не указан, имя файла берётся из заголовка диалога. Ничего не вставляет в текст.", "Saves the active dialog to a .txt file in the GTA SA userfiles folder\\\\HelperByOrc\\\\saved\\\\dialogs. If no argument is provided, the file name is derived from the dialog caption. Inserts no text.") \
    X(TagsBuiltinBindDisableDescription, "Выключает выбранный bind и сохраняет конфиг, если состояние реально изменилось.\n\nСинтаксис selector:\n- [binddisable(30)] — bind с UI-номером №30\n- [binddisable(\"№30 fb\")] — тот же bind, но с проверкой display name\n- [binddisable(\"fb\")] — имя в текущей категории; если найдено несколько, будет ошибка неоднозначности\n- [binddisable(\"fb\" \"\")] — bind в root текущей категории\n- [binddisable(\"fb\" \"Команды/Фрапс\")] — bind в папке текущей категории\n- [binddisable(\"fb\" \"\" \"Основные\")] — bind в root указанной категории\n- [binddisable({thisbind})] — текущий bind\n\nГде работает: только внутри выполняющегося bind; действие выполняется после текущей строки. Если bind уже выключен, это успешный no-op без ошибки.", "Disables the selected bind and saves the config only when the state actually changes.\n\nSelector syntax:\n- [binddisable(30)] — bind with UI number №30\n- [binddisable(\"№30 fb\")] — the same bind with display-name verification\n- [binddisable(\"fb\")] — name in the current category; multiple matches produce an ambiguity error\n- [binddisable(\"fb\" \"\")] — bind in current category root\n- [binddisable(\"fb\" \"Commands/Fraps\")] — bind in a folder of the current category\n- [binddisable(\"fb\" \"\" \"Main\")] — bind in root of the selected category\n- [binddisable({thisbind})] — current bind\n\nWhere it works: only inside a running bind; the action runs after the current line. If the bind is already disabled, this is a successful no-op.") \
    X(TagsBuiltinBindEnableDescription, "Включает выбранный bind и сохраняет конфиг, если состояние реально изменилось.\n\nSelector принимает те же формы, что и [bindstart(...)]: номер, \"№N имя\", имя, имя+папка, имя+папка+категория или {thisbind}.\n\nПримеры:\n- [bindenable(30)]\n- [bindenable(\"fb\" \"\")]\n- [bindenable(\"fb\" \"Команды/Фрапс\" \"Основные\")]\n\nРаботает только внутри выполняющегося bind и выполняется после текущей строки. Если bind уже включён, это успешный no-op без ошибки.", "Enables the selected bind and saves the config only when the state actually changes.\n\nSelector accepts the same forms as [bindstart(...)]: number, \"№N name\", name, name+folder, name+folder+category, or {thisbind}.\n\nExamples:\n- [bindenable(30)]\n- [bindenable(\"fb\" \"\")]\n- [bindenable(\"fb\" \"Commands/Fraps\" \"Main\")]\n\nWorks only inside a running bind and runs after the current line. If the bind is already enabled, this is a successful no-op.") \
    X(TagsBuiltinBindStartDescription, "Запускает выбранный bind после текущей строки текущего bind. Ничего не вставляет в текст.\n\nSelector:\n- [bindstart(30)] — запуск по UI-номеру №30\n- [bindstart(\"№30 fb\")] — alias: номер №30 + display name fb\n- [bindstart(\"fb\")] — имя в текущей категории, root и все папки; при дублях будет ошибка\n- [bindstart(\"fb\" \"\")] — только root текущей категории\n- [bindstart(\"fb\" \"Команды/Фрапс\")] — конкретная папка текущей категории\n- [bindstart(\"fb\" \"\" \"Основные\")] — root указанной категории\n- [bindstart(\"fb\" \"Команды/Фрапс\" \"Основные\")] — папка указанной категории\n- [bindstart({thisbind})] — перезапустить текущий bind после его штатного завершения\n\nПравила:\n- Кавычки дают точное совпадение имени/папки.\n- Без кавычек допускается partial match, но только если найден один bind.\n- Для имён с пробелами используйте кавычки.\n- Если selector указывает на текущий выполняющийся bind, старт откладывается до завершения текущего запуска; параллельный экземпляр не создаётся.\n- В HUD, preview и sandbox action-тег не выполняется.", "Starts the selected bind after the current line of the current bind. Inserts no text.\n\nSelector:\n- [bindstart(30)] — start by UI number №30\n- [bindstart(\"№30 fb\")] — alias: number №30 + display name fb\n- [bindstart(\"fb\")] — name in the current category, root and all folders; duplicate matches are an error\n- [bindstart(\"fb\" \"\")] — only current category root\n- [bindstart(\"fb\" \"Commands/Fraps\")] — specific folder in current category\n- [bindstart(\"fb\" \"\" \"Main\")] — root of the selected category\n- [bindstart(\"fb\" \"Commands/Fraps\" \"Main\")] — folder of the selected category\n- [bindstart({thisbind})] — restart the current bind after it finishes normally\n\nRules:\n- Quoted tokens require exact name/folder matches.\n- Unquoted tokens allow partial match only when exactly one bind matches.\n- Use quotes for names with spaces.\n- If the selector points to the currently running bind, the start is deferred until the current run finishes; no parallel instance is created.\n- HUD, preview, and sandbox do not execute this action tag.") \
    X(TagsBuiltinBindStopDescription, "Останавливает выбранный запущенный bind после текущей строки.\n\nСинтаксис:\n- [bindstop] — остановить текущий bind\n- [bindstop({thisbind})] — явно остановить текущий bind\n- [bindstop(30)] — остановить bind по UI-номеру\n- [bindstop(\"fb\" \"Команды/Фрапс\" \"Основные\")] — остановить bind по имени, папке и категории\n\nЕсли выбранный bind сейчас не выполняется, будет ошибка runtime-состояния. Для проверки перед остановкой используйте [bindended(...)].", "Stops the selected running bind after the current line.\n\nSyntax:\n- [bindstop] — stop the current bind\n- [bindstop({thisbind})] — explicitly stop the current bind\n- [bindstop(30)] — stop by UI number\n- [bindstop(\"fb\" \"Commands/Fraps\" \"Main\")] — stop by name, folder, and category\n\nIf the selected bind is not running, this is a runtime-state error. Use [bindended(...)] before stopping if needed.") \
    X(TagsBuiltinBindPauseDescription, "Ставит выбранный запущенный bind на паузу после текущей строки.\n\nСинтаксис:\n- [bindpause] — поставить на паузу текущий bind\n- [bindpause({thisbind})]\n- [bindpause(30)]\n- [bindpause(\"fb\" \"\" \"Основные\")]\n\nПауза применяется только к уже выполняющемуся bind. Если bind не запущен, будет ошибка.", "Pauses the selected running bind after the current line.\n\nSyntax:\n- [bindpause] — pause the current bind\n- [bindpause({thisbind})]\n- [bindpause(30)]\n- [bindpause(\"fb\" \"\" \"Main\")]\n\nPause applies only to an already running bind. If the bind is not running, this is an error.") \
    X(TagsBuiltinBindUnpauseDescription, "Снимает паузу с выбранного bind после текущей строки.\n\nСинтаксис:\n- [bindunpause(30)]\n- [bindunpause(\"№30 fb\")]\n- [bindunpause(\"fb\" \"Команды/Фрапс\")]\n- [bindunpause(\"fb\" \"\" \"Основные\")]\n\nЕсли bind не стоит на паузе, будет ошибка runtime-состояния. Selector ищется по тем же правилам, что у [bindstart(...)].", "Unpauses the selected bind after the current line.\n\nSyntax:\n- [bindunpause(30)]\n- [bindunpause(\"№30 fb\")]\n- [bindunpause(\"fb\" \"Commands/Fraps\")]\n- [bindunpause(\"fb\" \"\" \"Main\")]\n\nIf the bind is not paused, this is a runtime-state error. Selector lookup follows the same rules as [bindstart(...)].") \
    X(TagsBuiltinBindFastMenuDescription, "Добавляет выбранный bind в быстрое меню и сохраняет конфиг при изменении.\n\nПримеры:\n- [bindfastmenu(30)]\n- [bindfastmenu(\"fb\" \"\")]\n- [bindfastmenu(\"fb\" \"Команды/Фрапс\" \"Основные\")]\n\nРаботает только внутри выполняющегося bind и выполняется после текущей строки. Если bind уже в быстром меню, это успешный no-op без ошибки.", "Adds the selected bind to the quick menu and saves the config when changed.\n\nExamples:\n- [bindfastmenu(30)]\n- [bindfastmenu(\"fb\" \"\")]\n- [bindfastmenu(\"fb\" \"Commands/Fraps\" \"Main\")]\n\nWorks only inside a running bind and runs after the current line. If the bind is already in the quick menu, this is a successful no-op.") \
    X(TagsBuiltinBindUnfastMenuDescription, "Убирает выбранный bind из быстрого меню и сохраняет конфиг при изменении.\n\nПримеры:\n- [bindunfastmenu(30)]\n- [bindunfastmenu(\"№30 fb\")]\n- [bindunfastmenu(\"fb\" \"\" \"Основные\")]\n\nРаботает только внутри выполняющегося bind и выполняется после текущей строки. Если bind уже не в быстром меню, это успешный no-op без ошибки.", "Removes the selected bind from the quick menu and saves the config when changed.\n\nExamples:\n- [bindunfastmenu(30)]\n- [bindunfastmenu(\"№30 fb\")]\n- [bindunfastmenu(\"fb\" \"\" \"Main\")]\n\nWorks only inside a running bind and runs after the current line. If the bind is already absent from the quick menu, this is a successful no-op.") \
    X(TagsBuiltinBindRandomDescription, "Запускает случайный bind после текущей строки. Выбираются только enabled bind, которые сейчас не выполняются, не ждут input/confirmation и не заблокированы условиями.\n\nФорматы:\n- [bindrandom] — текущая папка текущего bind; если текущий bind в root, выбирает из root\n- [bindrandom(*)] или [bindrandom(\"*\")] — вся текущая категория\n- [bindrandom(\"\")] — root текущей категории\n- [bindrandom(\"Команды/Фрапс\")] — папка текущей категории\n- [bindrandom(\"Команды/Фрапс\" \"Основные\")] — папка указанной категории\n\nЕсли подходящих bind нет, запуск считается невозможным и будет ошибка.", "Starts a random bind after the current line. It only chooses enabled binds that are not running, not waiting for input/confirmation, and not blocked by conditions.\n\nForms:\n- [bindrandom] — current bind folder; if the current bind is in root, chooses from root\n- [bindrandom(*)] or [bindrandom(\"*\")] — whole current category\n- [bindrandom(\"\")] — current category root\n- [bindrandom(\"Commands/Fraps\")] — folder in current category\n- [bindrandom(\"Commands/Fraps\" \"Main\")] — folder in selected category\n\nIf no suitable bind exists, start is impossible and an error is shown.") \
    X(TagsBuiltinBindEndedDescription, "Проверяет состояние выбранного bind и возвращает строку:\n- 1 — bind завершён или не активен\n- 0 — bind выполняется, ждёт input/confirmation или стоит на паузе\n\nЭто pure-check: его можно использовать в [ifandor(...)] и условиях без запуска действий.\n\nПримеры:\n- [bindended({thisbind})]\n- [bindended(30)]\n- [bindended(\"fb\" \"\" \"Основные\")]\n- [ifandor([bindended(30)]==1?[bindstart(30)]:Уже выполняется)]\n\nВне runtime-контекста bind возвращает 0.", "Checks the selected bind state and returns a string:\n- 1 — bind has ended or is inactive\n- 0 — bind is running, waiting for input/confirmation, or paused\n\nThis is a pure check: it can be used in [ifandor(...)] and conditions without executing actions.\n\nExamples:\n- [bindended({thisbind})]\n- [bindended(30)]\n- [bindended(\"fb\" \"\" \"Main\")]\n- [ifandor([bindended(30)]==1?[bindstart(30)]:Already running)]\n\nOutside bind runtime context it returns 0.") \
    X(TagsBuiltinBindPopupDescription, "Открывает popup строк выбранного bind для быстрой ручной отправки. Сам bind не запускает и текст не вставляет.\n\nПримеры:\n- [bindpopup(30)]\n- [bindpopup(\"№30 fb\")]\n- [bindpopup(\"fb\" \"Команды/Фрапс\")]\n- [bindpopup(\"fb\" \"\" \"Основные\")]\n\nРаботает только внутри выполняющегося bind и открывает popup после текущей строки. Если bind не найден или selector неоднозначен, будет ошибка.", "Opens the selected bind's lines popup for quick manual sending. It does not start the bind and inserts no text.\n\nExamples:\n- [bindpopup(30)]\n- [bindpopup(\"№30 fb\")]\n- [bindpopup(\"fb\" \"Commands/Fraps\")]\n- [bindpopup(\"fb\" \"\" \"Main\")]\n\nWorks only inside a running bind and opens the popup after the current line. Missing or ambiguous selectors produce an error.") \
    X(MiscVariablesTitle, "Переменные", "Variables") \
    X(MiscVariablesIntro, "Отдельный модуль переменных живёт в общем HelperByOrc.json и даёт основу для будущего тегового движка. Здесь собраны простые данные игрока, время, lookup-теги и первые функциональные действия.", "The variables module lives in the shared HelperByOrc.json and lays the groundwork for the future tag engine. It already groups simple player data, time tags, lookup tags, and the first functional actions.") \
    X(MiscVariablesCardTitle, "Каталог переменных", "Variables Catalog") \
    X(MiscVariablesCardDesc, "Экран показывает встроенные и пользовательские переменные, их тип, описание и быстрый sandbox-предпросмотр без запуска бинда.", "This screen shows built-in and custom variables, their type, description, and a quick sandbox preview without launching a bind.") \
    X(MiscVariablesBuiltinsLabel, "Встроенных", "Built-ins") \
    X(MiscVariablesSimpleLabel, "Простых", "Simple") \
    X(MiscVariablesFunctionLabel, "Функциональных", "Function") \
    X(MiscVariablesCustomLabel, "Пользовательских", "Custom") \
    X(MiscVariablesConfigLabel, "Секция конфига", "Config section") \
    X(MiscVariablesCatalogTitle, "Список тегов", "Tag Catalog") \
    X(MiscVariablesSearchHint, "Поиск по токену или описанию", "Search by token or description") \
    X(MiscVariablesCatalogEmpty, "По запросу ничего не найдено.", "No tags matched the query.") \
    X(MiscVariablesInspectorTitle, "Карточка тега", "Tag Card") \
    X(MiscVariablesInspectorEmpty, "Выберите тег слева, чтобы увидеть описание и пример.", "Select a tag on the left to view its description and example.") \
    X(MiscVariablesDescriptionLabel, "Описание", "Description") \
    X(MiscVariablesExampleLabel, "Пример", "Example") \
    X(MiscVariablesCopyToken, "Скопировать токен", "Copy token") \
    X(MiscVariablesCopyExample, "Скопировать пример", "Copy example") \
    X(MiscVariablesParamcmdNote, "[paramcmd(...)] работает только если бинд был запущен именно командой. Аргументы делятся по пробелам, как в Lua-версии.", "[paramcmd(...)] only works when the bind was launched by a command. Arguments are split by spaces, matching the Lua version.") \
    X(MiscVariablesKeyEmulateNote, "[keyemulate(...)] выполняет одно нажатие клавиши и возвращает пустую строку. В sandbox и preview он только скрывается из результата без реального нажатия.", "[keyemulate(...)] performs a single key press and returns an empty string. In sandboxes and previews it only disappears from the result without pressing a real key.") \
    X(MiscVariablesKeyPickerOpenHint, "Открыть список виртуальных клавиш и скопировать готовый [keyemulate(...)].", "Open the virtual-key list and copy a ready-made [keyemulate(...)].") \
    X(MiscVariablesKeyPickerTitle, "Подбор клавиши для [keyemulate(...)]", "Pick a key for [keyemulate(...)]") \
    X(MiscVariablesKeyPickerIntro, "Наведите или нажмите на плюсик, затем выберите виртуальную клавишу. Готовая переменная сразу скопируется в буфер обмена.", "Hover or click the plus button, then choose a virtual key. The ready-made variable is copied to the clipboard immediately.") \
    X(MiscVariablesKeyPickerSearchHint, "Поиск по коду или названию клавиши", "Search by key code or key name") \
    X(MiscVariablesKeyPickerEmpty, "По этому фильтру клавиши не найдены.", "No keys matched this filter.") \
    X(MiscVariablesKeyPickerCopyHint, "Щелчок по строке копирует [keyemulate(code)] с уже подставленным кодом.", "Clicking a row copies [keyemulate(code)] with the selected code inserted.") \
    X(MiscVariablesDialogItemPickerOpenHint, "Открыть список доступных пунктов активного диалога и скопировать [dialogitem(...)].", "Open the active dialog item list and copy [dialogitem(...)].") \
    X(MiscVariablesDialogItemPickerTitle, "Подбор пункта для [dialogitem(...)]", "Pick an item for [dialogitem(...)]") \
    X(MiscVariablesDialogItemPickerIntro, "Показаны текущие пункты активного list/tablist диалога. Щелчок по строке копирует [dialogitem(N)] с нужным номером.", "Shows the current items of the active list/tablist dialog. Clicking a row copies [dialogitem(N)] with the selected number.") \
    X(MiscVariablesDialogItemPickerSearchHint, "Поиск по номеру или тексту пункта", "Search by item number or text") \
    X(MiscVariablesDialogItemPickerEmpty, "По этому фильтру пункты не найдены.", "No dialog items matched this filter.") \
    X(MiscVariablesDialogItemPickerNoDialog, "Нужен активный диалог SA:MP со списком.", "An active SA:MP list dialog is required.") \
    X(MiscVariablesDialogItemPickerNotList, "Активный диалог не является list/tablist.", "The active dialog is not a list/tablist dialog.") \
    X(MiscVariablesDialogItemPickerHeaderLabel, "Заголовок колонок: %s", "Header row: %s") \
    X(MiscVariablesDialogItemPickerCaptionLabel, "Заголовок диалога: %s", "Dialog caption: %s") \
    X(MiscVariablesDialogItemPickerCopyHint, "Щелчок по строке копирует [dialogitem(N)] для выбранного пункта.", "Clicking a row copies [dialogitem(N)] for the selected item.") \
    X(MiscVariablesDialogTextPickerOpenHint, "Открыть список токенов активного диалога и скопировать [dialogtext(...)].", "Open the active dialog token list and copy [dialogtext(...)].") \
    X(MiscVariablesDialogTextPickerTitle, "Подбор индекса для [dialogtext(...)]", "Pick an index for [dialogtext(...)]") \
    X(MiscVariablesDialogTextPickerIntro, "Показаны токены текста активного диалога. Щелчок по строке копирует [dialogtext(index)] с нужным индексом.", "Shows tokens from the active dialog text. Clicking a row copies [dialogtext(index)] with the selected index.") \
    X(MiscVariablesDialogTextPickerSearchHint, "Поиск по индексу или тексту токена", "Search by token index or token text") \
    X(MiscVariablesDialogTextPickerEmpty, "По этому фильтру токены не найдены.", "No dialog tokens matched this filter.") \
    X(MiscVariablesDialogTextPickerNoDialog, "Нужен активный диалог SA:MP.", "An active SA:MP dialog is required.") \
    X(MiscVariablesDialogTextPickerCaptionLabel, "Заголовок диалога: %s", "Dialog caption: %s") \
    X(MiscVariablesDialogTextPickerCountLabel, "Всего токенов: %s", "Total tokens: %s") \
    X(MiscVariablesDialogTextPickerCopyHint, "Щелчок по строке копирует [dialogtext(index)] с выбранным индексом.", "Clicking a row copies [dialogtext(index)] with the selected index.") \
    X(MiscVariablesPreviewTitle, "Sandbox", "Sandbox") \
    X(MiscVariablesPreviewHint, "Здесь можно быстро проверить, как строка раскроется через текущий движок переменных.", "Use this sandbox to quickly test how a string expands through the current variables engine.") \
    X(MiscVariablesTemplateLabel, "Шаблон", "Template") \
    X(MiscVariablesPreviewLaunchManual, "Обычный запуск", "Regular launch") \
    X(MiscVariablesPreviewLaunchCommand, "Запуск командой", "Command launch") \
    X(MiscVariablesPreviewSourceLabel, "Источник запуска", "Launch source") \
    X(MiscVariablesPreviewBindCommandLabel, "Команда бинда", "Bind command") \
    X(MiscVariablesPreviewCommandTextLabel, "Введённая команда", "Entered command") \
    X(MiscVariablesPreviewResultLabel, "Результат", "Result") \
    X(MiscVariablesPreviewEmpty, "Результат пустой.", "The result is empty.") \
    X(NotepadIntro, "Профильный блокнот с папками, разметкой, локальными картинками и live preview.", "Profile-aware notepad with folders, markup, local images, and live preview.") \
    X(NotepadRootName, "Блокнот", "Notepad") \
    X(NotepadNewNote, "Заметка", "Note") \
    X(NotepadNewFolder, "Папка", "Folder") \
    X(NotepadDefaultFolder, "Новая папка", "New folder") \
    X(NotepadUntitled, "Без названия", "Untitled") \
    X(NotepadCopySuffix, " копия", " copy") \
    X(NotepadSearchHint, "Поиск заметок", "Search notes") \
    X(NotepadSearchResults, "Найдено:", "Found:") \
    X(NotepadFavorites, "Избранное", "Favorites") \
    X(NotepadEmptyFolder, "В этой папке пока пусто.", "This folder is empty.") \
    X(NotepadEmptySearch, "По запросу ничего не найдено.", "No notes matched the query.") \
    X(NotepadNoSelection, "Выберите заметку слева или создайте новую.", "Select a note on the left or create a new one.") \
    X(NotepadEditor, "Редактор", "Editor") \
    X(NotepadLivePreview, "Предпросмотр", "Preview") \
    X(NotepadFavorite, "В избранное", "Add to favorites") \
    X(NotepadUnfavorite, "Убрать из избранного", "Remove from favorites") \
    X(NotepadOpenFolder, "Открыть папку", "Open folder") \
    X(NotepadImportTxt, "Импорт .txt", "Import .txt") \
    X(NotepadExportTxt, "Экспорт .txt", "Export .txt") \
    X(NotepadCopyRaw, "Копировать raw", "Copy raw") \
    X(NotepadCopyRendered, "Копировать текст", "Copy text") \
    X(NotepadCopyLine, "Копировать строки", "Copy lines") \
    X(NotepadPreviewMode, "Предпросмотр", "Preview") \
    X(NotepadInsertImage, "Картинка", "Image") \
    X(NotepadApplyTags, "Подставлять теги", "Expand tags") \
    X(NotepadMarkupHelp, "Разметка", "Markup") \
    X(NotepadMarkupHelpTitle, "Поддерживаемая разметка", "Supported markup") \
    X(NotepadMarkupHelpHint, "Картинки ищутся только в папке профиля notepad/images. Абсолютные пути и .. запрещены.", "Images are resolved only from the profile notepad/images folder. Absolute paths and .. are blocked.") \
    X(NotepadModalTitle, "Блокнот", "Notepad") \
    X(NotepadCreateFolderTitle, "Новая папка", "New folder") \
    X(NotepadCreateNoteTitle, "Новая заметка", "New note") \
    X(NotepadRenameTitle, "Переименование", "Rename") \
    X(NotepadDeleteNoteTitle, "Удаление заметки", "Delete note") \
    X(NotepadDeleteFolderTitle, "Удаление папки", "Delete folder") \
    X(NotepadFolderName, "Имя папки", "Folder name") \
    X(NotepadNoteTitle, "Название заметки", "Note title") \
    X(NotepadDeleteNoteQuestionFormat, "Удалить заметку \"%s\"?", "Delete note \"%s\"?") \
    X(NotepadDeleteFolderQuestionFormat, "Удалить папку \"%s\"? Внутри папок: %d, заметок: %d.", "Delete folder \"%s\"? Nested folders: %d, notes: %d.") \
    X(NotepadConfirmDelete, "Удалить", "Delete") \
    X(NotepadFolderExists, "Папка с таким именем уже есть.", "A folder with this name already exists.") \
    X(NotepadInvalidName, "Некорректное имя.", "Invalid name.") \
    X(NotepadImportFailed, "Не удалось импортировать txt.", "Failed to import txt.") \
    X(NotepadExportFailed, "Не удалось экспортировать заметку.", "Failed to export note.") \
    X(NotepadExportSuccessFormat, "Экспортировано: %s", "Exported: %s") \
    X(NotepadImageCopied, "Картинка скопирована в профиль и вставлена в заметку.", "Image was copied into the profile and inserted into the note.") \
    X(NotepadImageInsertFailed, "Не удалось вставить картинку.", "Failed to insert image.") \
    X(NotepadInvalidImagePath, "Некорректный путь к картинке.", "Invalid image path.") \
    X(NotepadMissingImageFormat, "Картинка не найдена: %s", "Image not found: %s") \
    X(NotepadTxtFilesFilter, "Текстовые файлы (*.txt)", "Text files (*.txt)") \
    X(NotepadImageFilesFilter, "Картинки (*.png;*.jpg;*.jpeg;*.bmp;*.gif)", "Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)") \
    X(NotepadAllFilesFilter, "Все файлы (*.*)", "All files (*.*)") \
    X(SettingsGtaVersion, "Версия GTA", "GTA version") \
    X(SettingsSectionGeneral, "Общее", "General") \
    X(SettingsSectionBinder, "Биндер", "Binder") \
    X(SettingsSectionHotkeys, "Горячие клавиши", "Hotkeys") \
    X(SettingsSectionProfiles, "Профили", "Profiles") \
    X(SettingsSectionQuickMenu, "Быстрое меню", "Quick menu") \
    X(SettingsSectionNotifications, "Уведомления", "Notifications") \
    X(SettingsSectionDiagnostics, "Диагностика", "Diagnostics") \
    X(SettingsGeneralIntro, "Язык и масштаб интерфейса.", "Language and interface scaling.") \
    X(SettingsBinderIntro, "Общие параметры выполнения биндов.", "General bind runtime settings.") \
    X(SettingsBinderTextConfirmationTimeoutSec, "Таймер подтверждения триггера, сек", "Trigger confirmation timeout, sec") \
    X(SettingsBinderTextConfirmationTimeoutHint, "Если бинд ждёт подтверждение по текстовому триггеру дольше этого времени, ожидание сбрасывается и бинд не запускается. Диапазон: 5-600 секунд.", "If a bind waits for text-trigger confirmation longer than this value, the pending confirmation is cleared and the bind is not started. Range: 5-600 seconds.") \
    X(SettingsHotkeysIntro, "Комбинации для открытия основных окон HelperByOrc.", "Shortcuts for opening HelperByOrc windows.") \
    X(SettingsQuickMenuIntro, "Как открывается быстрое меню биндов.", "How the binder quick menu opens.") \
    X(SettingsDiagnosticsIntro, "Пути, журнал и runtime-состояние для поиска проблем.", "Paths, log, and runtime state for troubleshooting.") \
    X(SettingsNotificationsEnabled, "Включить системные уведомления", "Enable system notifications") \
    X(SettingsNotificationsChannel, "Канал", "Channel") \
    X(SettingsNotificationsChannelPopup, "Окно", "Popup") \
    X(SettingsNotificationsChannelLog, "Лог", "Log") \
    X(SettingsNotificationsGroups, "События", "Events") \
    X(SettingsNotificationsGroupBinderErrors, "Ошибки биндов и отправки", "Bind and send errors") \
    X(SettingsNotificationsGroupTagErrors, "Ошибки тегов", "Tag errors") \
    X(SettingsNotificationsGroupSampDialogErrors, "Ошибки диалогов SA:MP", "SA:MP dialog errors") \
    X(SettingsNotificationsGroupSuccess, "Успешные действия", "Successful actions") \
    X(SettingsNotificationsGroupConfirmation, "Подтверждения и отмены", "Confirmations and cancels") \
    X(SettingsNotificationsPosition, "Позиция", "Position") \
    X(SettingsNotificationsPositionTopLeft, "Сверху слева", "Top left") \
    X(SettingsNotificationsPositionTopCenter, "Сверху по центру", "Top center") \
    X(SettingsNotificationsPositionTopRight, "Сверху справа", "Top right") \
    X(SettingsNotificationsPositionMiddleLeft, "По центру слева", "Middle left") \
    X(SettingsNotificationsPositionMiddleCenter, "По центру", "Middle center") \
    X(SettingsNotificationsPositionMiddleRight, "По центру справа", "Middle right") \
    X(SettingsNotificationsPositionBottomLeft, "Снизу слева", "Bottom left") \
    X(SettingsNotificationsPositionBottomCenter, "Снизу по центру", "Bottom center") \
    X(SettingsNotificationsPositionBottomRight, "Снизу справа", "Bottom right") \
    X(SettingsNotificationsOffsetX, "Отступ X", "Offset X") \
    X(SettingsNotificationsOffsetY, "Отступ Y", "Offset Y") \
    X(SettingsNotificationsDisplay, "Отображение", "Display") \
    X(SettingsNotificationsDurationMs, "Время показа, мс", "Duration, ms") \
    X(SettingsNotificationsWidth, "Ширина окна", "Popup width") \
    X(SettingsNotificationsOpacity, "Прозрачность", "Opacity") \
    X(SettingsNotificationsAntiFlood, "Антифлуд", "Anti-flood") \
    X(SettingsNotificationsDedupeMs, "Схлопывать повторы, мс", "Collapse repeats, ms") \
    X(SettingsNotificationsMaxVisible, "Максимум на экране", "Max visible") \
    X(SettingsNotificationsMaxQueue, "Максимум в очереди", "Max queued") \
    X(SettingsNotificationsTest, "Тестовое уведомление", "Test notification") \
    X(SettingsNotificationsTestText, "Тестовое уведомление HelperByOrc.", "HelperByOrc test notification.") \
    X(SettingsSummaryProfile, "Профиль", "Profile") \
    X(SettingsSummaryLanguage, "Язык", "Language") \
    X(SettingsSummaryMainWindow, "Главное окно", "Main window") \
    X(SettingsSummaryQuickMenu, "Быстрое меню", "Quick menu") \
    X(SettingsLanguage, "Язык", "Language") \
    X(SettingsUiScale, "Масштаб интерфейса", "Interface Scale") \
    X(SettingsAutoScale, "Автомасштаб под разрешение", "Auto scale for resolution") \
    X(SettingsScaleMultiplier, "Пользовательский множитель", "Custom multiplier") \
    X(SettingsEffectiveScale, "Итоговый масштаб", "Effective scale") \
    X(SettingsResetDefaults, "Сбросить интерфейс", "Reset interface") \
    X(SettingsConfigPath, "Файл конфига", "Config file") \
    X(SettingsLogPath, "Файл журнала", "Log file") \
    X(SettingsLogLevel, "Уровень логирования", "Log level") \
    X(SettingsLogLevelOff, "Off", "Off") \
    X(SettingsLogLevelError, "Error", "Error") \
    X(SettingsLogLevelInfo, "Info", "Info") \
    X(SettingsApplyDamageProtection, "Защита деталей транспорта", "Vehicle parts protection") \
    X(SettingsApplyDamageProtectionDesc, "Не даёт деталям транспорта отваливаться после урона.", "Prevents vehicle parts from falling off after damage.") \
    X(SettingsScaleHint, "Автомасштаб берёт за основу 1920x1080 и подстраивает UI под текущее разрешение. Его можно отключить или скорректировать множителем.", "Auto scale uses 1920x1080 as the reference and adapts the UI to the current resolution. You can disable it or fine-tune it with the multiplier.") \
    X(SettingsMainWindowHotkey, "Хоткей открытия главного окна", "Main window hotkey") \
    X(SettingsMainWindowHotkeyHelp, "Комбинация открывает и закрывает главное окно HelperByOrc.", "This shortcut opens and closes the main HelperByOrc window.") \
    X(SettingsResetHotkey, "Сбросить", "Reset") \
    X(SettingsProfilesSection, "Профили", "Profiles") \
    X(SettingsProfilesIntro, "Каждый профиль хранит отдельный HelperByOrc.json. Переключение сразу перезагружает настройки UI, биндер и переменные.", "Each profile stores a separate HelperByOrc.json. Switching reloads UI settings, binder data, and variables immediately.") \
    X(SettingsActiveProfile, "Активный профиль", "Active profile") \
    X(SettingsProfileName, "Название профиля", "Profile name") \
    X(SettingsProfileCreateEmpty, "Создать пустой", "Create empty") \
    X(SettingsProfileDuplicate, "Дублировать текущий", "Duplicate current") \
    X(SettingsProfileRename, "Переименовать", "Rename") \
    X(SettingsProfileDelete, "Удалить профиль", "Delete profile") \
    X(SettingsProfileDeleteTitle, "Удаление профиля", "Delete profile") \
    X(SettingsProfileDeleteQuestionFormat, "Удалить профиль \"%s\" вместе с его HelperByOrc.json?", "Delete profile \"%s\" together with its HelperByOrc.json?") \
    X(SettingsProfileCannotDeleteLast, "Нельзя удалить последний профиль.", "The last profile cannot be deleted.") \
    X(SettingsProfileNameRequired, "Введите название профиля.", "Enter a profile name.") \
    X(SettingsProfileOperationFailed, "Не удалось выполнить действие с профилем. Подробности записаны в лог.", "Failed to complete the profile action. Details were written to the log.") \
    X(SettingsProfilesPath, "Папка профилей", "Profiles folder") \
    X(SettingsProfilesRegistryPath, "Реестр профилей", "Profiles registry") \
    X(SettingsOpenProfilesFolder, "Открыть папку профилей", "Open profiles folder") \
    X(SettingsOpenConfigFile, "Открыть конфиг", "Open config") \
    X(SettingsOpenRegistryFile, "Открыть реестр", "Open registry") \
    X(SettingsOpenLogFile, "Открыть журнал", "Open log") \
    X(SettingsCopyPath, "Копировать путь", "Copy path") \
    X(SettingsSampStatus, "SA:MP", "SA:MP") \
    X(SettingsSampVersion, "Версия SA:MP", "SA:MP version") \
    X(SettingsSampWaiting, "ожидание samp.dll", "waiting for samp.dll") \
    X(SettingsSampLoaded, "загружен", "loaded") \
    X(SettingsSampReady, "готов", "ready") \
    X(SettingsSampUnsupported, "неподдерживаемая версия", "unsupported version") \
    X(SettingsChatAsiStatus, "_chat.asi", "_chat.asi") \
    X(SettingsChatAsiLoaded, "загружен", "loaded") \
    X(SettingsChatAsiNotLoaded, "не загружен", "not loaded") \
    X(SettingsFallbackStatus, "SA:MP fallback", "SA:MP fallback") \
    X(SettingsFallbackAvailable, "доступен при готовом SA:MP", "available when SA:MP is ready") \
    X(SettingsHooksStatus, "SA:MP hooks", "SA:MP hooks") \
    X(SettingsRakHooksStatus, "RakNet hooks", "RakNet hooks") \
    X(MiscGameFixesTitle, "Игровые исправления", "Game fixes") \
    X(MiscGameFixesDesc, "Небольшие исправления поведения игры, которые работают без отдельной настройки биндов.", "Small game behavior fixes that do not require binder setup.") \
    X(HotkeyConflictFormat, "Комбинация конфликтует с %s.", "This combination conflicts with %s.") \
    X(HotkeyConflictMainWindowFormat, "хоткеем открытия главного окна (%s)", "the main window hotkey (%s)") \
    X(HotkeyConflictQuickMenuFormat, "хоткеем быстрого меню (%s)", "the quick menu hotkey (%s)") \
    X(HotkeyConflictBindFormat, "биндом \"%s\" (%s)", "bind \"%s\" (%s)") \
    X(BinderDefaultRootFolder, "Основные", "Main") \
    X(BinderDefaultHotkey, "Новый бинд", "New bind") \
    X(BinderDefaultFolder, "Папка", "Folder") \
    X(BinderNewCategory, "Новая категория", "New category") \
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
    X(ConditionScoreboardOpen, "Таб открыт", "Scoreboard open") \
    X(ConditionScoreboardClosed, "Таб закрыт", "Scoreboard closed") \
    X(ConditionChatVisible, "Чат виден", "Chat visible") \
    X(ConditionChatHidden, "Чат скрыт", "Chat hidden") \
    X(ConditionSampCursorActive, "Активный курсор SA:MP", "SA:MP cursor active") \
    X(ConditionWindowsCursorActive, "Активный курсор Windows", "Windows cursor active") \
    X(ConditionHelperActive, "Активный Helper", "Helper active") \
    X(ConditionGameHudVisible, "HUD игры виден", "Game HUD visible") \
    X(ConditionGameHudHidden, "HUD игры не виден", "Game HUD hidden") \
    X(ConditionCameraAttached, "Камера прикреплена", "Camera attached") \
    X(ConditionCameraDetached, "Камера не прикреплена", "Camera detached") \
    X(ConditionServerConnected, "Подключён к серверу", "Connected to server") \
    X(ConditionServerDisconnected, "Не подключён к серверу", "Disconnected from server") \
    X(ConditionDriver, "За рулём", "Driver") \
    X(ConditionPassenger, "Пассажир", "Passenger") \
    X(ConditionVehicleSirenOn, "Сирена включена", "Siren on") \
    X(ConditionVehicleSirenOff, "Сирена выключена", "Siren off") \
    X(ConditionVehicleEngineOn, "Двигатель заведён", "Engine on") \
    X(ConditionVehicleEngineOff, "Двигатель заглушён", "Engine off") \
    X(ConditionGtaMenuOpen, "GTA меню открыто", "GTA menu open") \
    X(ConditionGtaMenuClosed, "GTA меню закрыто", "GTA menu closed") \
    X(ConditionSampCursorInactive, "Неактивный курсор SA:MP", "SA:MP cursor inactive") \
    X(ConditionWindowsCursorInactive, "Неактивный курсор Windows", "Windows cursor inactive") \
    X(ConditionChatClosed, "Закрыт чат", "Chat closed") \
    X(ConditionDialogClosed, "Закрыт диалог", "Dialog closed") \
    X(ConditionVehicleBoat, "Лодка", "Boat") \
    X(ConditionVehicleCar, "Машина", "Car") \
    X(ConditionVehicleTrain, "Поезд", "Train") \
    X(ConditionVehicleHeli, "Вертолёт", "Helicopter") \
    X(ConditionVehiclePlane, "Самолёт", "Plane") \
    X(ConditionVehicleBike, "Мотоцикл", "Motorcycle") \
    X(ConditionVehicleFakePlane, "Псевдосамолёт", "Fake plane") \
    X(ConditionVehicleMonsterTruck, "Монстр-трак", "Monster truck") \
    X(ConditionVehicleQuadBike, "Квадроцикл", "Quad bike") \
    X(ConditionVehicleBicycle, "Велосипед", "Bicycle") \
    X(ConditionNotVehicleBoat, "Не лодка", "Not boat") \
    X(ConditionNotVehicleTrain, "Не поезд", "Not train") \
    X(ConditionNotVehiclePlane, "Не самолёт", "Not plane") \
    X(ConditionNotVehicleFakePlane, "Не псевдосамолёт", "Not fake plane") \
    X(ConditionNotVehicleCar, "Не машина", "Not car") \
    X(ConditionNotVehicleHeli, "Не вертолёт", "Not helicopter") \
    X(ConditionNotVehicleBike, "Не мотоцикл", "Not motorcycle") \
    X(ConditionNotVehicleMonsterTruck, "Не монстр-трак", "Not monster truck") \
    X(ConditionNotVehicleBicycle, "Не велосипед", "Not bicycle") \
    X(ConditionNotVehicleQuadBike, "Не квадроцикл", "Not quad bike") \
    X(ConditionNotInWater, "Не в воде", "Not in water") \
    X(ConditionNotInAir, "Не в воздухе", "Not in air") \
    X(ConditionDeprecated, "Удалённое условие", "Removed condition") \
    X(ConditionCategoryPlayer, "Игрок", "Player") \
    X(ConditionCategoryInterface, "Интерфейс", "Interface") \
    X(ConditionCategoryGame, "Игра", "Game") \
    X(ConditionCategoryVehicle, "Транспорт", "Vehicle") \
    X(ConditionSearchHint, "Поиск условий", "Search conditions") \
    X(ConditionSelectedCount, "Выбрано: %d", "Selected: %d") \
    X(ConditionSelectedNone, "Условия не выбраны.", "No conditions selected.") \
    X(ConditionReset, "Сбросить", "Reset") \
    X(ConditionNoMatches, "Нет условий по этому фильтру.", "No conditions match this filter.") \
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
    X(ToastBindConfirmExpired, "%s: подтверждение бинда истекло: %s", "%s: bind confirmation expired: %s") \
    X(ToastBindCanceled, "%s: бинд отменён: %s", "%s: bind canceled: %s") \
    X(ToastConditionBlocked, "Бинд заблокирован: %s", "Bind blocked: %s") \
    X(ToastFinishActiveInput, "Сначала завершите активный ввод.", "Finish the active input first.") \
    X(ToastSendLocalFailed, "Не удалось добавить сообщение в чат SA:MP.", "Failed to add a message to the SA:MP chat.") \
    X(ToastSendSampFailed, "Не удалось отправить текст в SA:MP.", "Failed to send text to SA:MP.") \
    X(ToastInsertChatFailed, "Не удалось вставить текст в чат.", "Failed to insert text into chat.") \
    X(ToastOpenChatFailed, "Не удалось открыть чат с текстом.", "Failed to open chat with text.") \
    X(ToastInsertDialogFailed, "Не удалось вставить текст в диалог.", "Failed to insert text into dialog.") \
    X(ToastClipboardFailed, "Не удалось записать текст в буфер обмена.", "Failed to copy text to the clipboard.") \
    X(ToastClipboardCopied, "Скопировано в буфер обмена.", "Copied to the clipboard.") \
    X(ToastUnknownSendMethod, "Неизвестный способ отправки: %d", "Unknown send method: %d") \
    X(ToastBindTagTargetRequired, "Для %s нужно указать бинд или вызвать тег внутри запущенного бинда.", "%s requires a bind target or must be called from a running bind.") \
    X(ToastBindTagNoFolders, "Папки биндов ещё не созданы.", "No bind folders have been created yet.") \
    X(ToastBindTagFolderNotFound, "Папка для %s не найдена.", "The folder for %s was not found.") \
    X(ToastBindTagCategoryNotFound, "Категория для %s не найдена.", "The category for %s was not found.") \
    X(ToastBindTagAmbiguous, "Selector для %s неоднозначен: найдено несколько биндов или папок.", "The selector for %s is ambiguous: multiple binds or folders matched.") \
    X(ToastBindTagBindNotFound, "Бинд для %s не найден.", "The bind for %s was not found.") \
    X(ToastBindTagNotStarted, "Не удалось запустить бинд для %s.", "Failed to start the bind for %s.") \
    X(ToastBindTagNotRunning, "Бинд для %s не запущен.", "The bind for %s is not running.") \
    X(ToastBindTagNotPaused, "Бинд для %s не стоит на паузе.", "The bind for %s is not paused.") \
    X(ToastBindTagNoChanges, "%s не изменил состояние бинда.", "%s did not change the bind state.") \
    X(ToastBindTagPopupUnavailable, "Не удалось открыть попап строк бинда для %s.", "Failed to open the bind lines popup for %s.") \
    X(ToastBindTagStopAllEmpty, "Нет запущенных биндов для остановки.", "There are no running binds to stop.") \
    X(ToastBindTagUnknownAction, "Неизвестное действие bind-тега: %s", "Unknown bind-tag action: %s") \
    X(ToastScreenDocumentsUnavailable, "Не удалось определить папку данных GTA SA для сохранения скриншота.", "Failed to resolve the GTA SA userfiles folder for saving the screenshot.") \
    X(ToastScreenInvalidFolder, "Недопустимая подпапка для [screen(...)]: %s", "Invalid subfolder for [screen(...)]: %s") \
    X(ToastScreenCaptureFailed, "Не удалось сделать скриншот игры.", "Failed to capture the game screenshot.") \
    X(ToastTPhotoFailed, "Не удалось сделать фото через игровой фотоаппарат.", "Failed to take a photo through the in-game camera.") \
    X(ToastKeyDownInvalidFormat, "Для [keydown(...)] нужен формат key;milliseconds.", "The [keydown(...)] tag expects the key;milliseconds format.") \
    X(ToastKeyDownInvalidKey, "Для [keydown(...)] код клавиши должен быть целым числом от 1 до 255.", "The [keydown(...)] key code must be an integer from 1 to 255.") \
    X(ToastKeyDownInvalidDuration, "Для [keydown(...)] длительность должна быть целым числом больше 0.", "The [keydown(...)] duration must be an integer greater than 0.") \
    X(ToastTimefMissingSemicolon, "Для [timef(...)] формат должен заканчиваться точкой с запятой ;", "The format for [timef(...)] must end with a semicolon ;") \
    X(ToastTimefEmptyFormat, "Для [timef(...)] нужно указать формат времени перед ;", "You must provide a time format before ; in [timef(...)]") \
    X(ToastTimefInvalidSpecifier, "Неподдерживаемый параметр [timef(...)] : %s", "Unsupported [timef(...)] specifier: %s") \
    X(ToastTimefFormatFailed, "Не удалось отформатировать время для [timef(...)].", "Failed to format time for [timef(...)]") \
    X(ToastDialogCloseNoActive, "Для [dialogclose(...)] нужен активный диалог SA:MP.", "The [dialogclose(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogCloseInvalidButton, "Для [dialogclose(...)] допустимы только 1 или 0.", "Only 1 or 0 are allowed in [dialogclose(...)]") \
    X(ToastDialogCloseFailed, "Не удалось закрыть активный диалог через [dialogclose(...)].", "Failed to close the active dialog via [dialogclose(...)]") \
    X(ToastDialogSetTextNoActive, "Для [dialogsettext(...)] нужен активный диалог SA:MP.", "The [dialogsettext(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogSetTextNoEditbox, "В активном диалоге нет editbox для [dialogsettext(...)].", "The active dialog has no editbox for [dialogsettext(...)]") \
    X(ToastDialogSetTextFailed, "Не удалось установить текст в editbox через [dialogsettext(...)].", "Failed to set dialog editbox text via [dialogsettext(...)]") \
    X(ToastDialogWaitOpenTimedOut, "Диалог не открылся за 3 секунды. Текущий бинд остановлен.", "No dialog opened within 3 seconds. The current bind was stopped.") \
    X(ToastDialogWaitIdInvalidId, "Для [dialogwaitid(...)] нужно указать dialog id >= 0.", "You must provide a dialog id >= 0 for [dialogwaitid(...)]") \
    X(ToastDialogWaitIdTimedOut, "Диалог с id %s не открылся за 3 секунды. Текущий бинд остановлен.", "Dialog id %s did not open within 3 seconds. The current bind was stopped.") \
    X(ToastDialogItemNoActive, "Для [dialogitem(...)] нужен активный list/tablist диалог SA:MP.", "The [dialogitem(...)] tag requires an active SA:MP list/tablist dialog.") \
    X(ToastDialogItemEmptyParam, "Для [dialogitem(...)] нужно указать номер или текст пункта.", "You must provide an item number or item text for [dialogitem(...)]") \
    X(ToastDialogItemNotList, "Активный диалог не поддерживает выбор пункта через [dialogitem(...)].", "The active dialog does not support [dialogitem(...)] item selection.") \
    X(ToastDialogItemNotFound, "Пункт для [dialogitem(...)] не найден в активном диалоге.", "The requested [dialogitem(...)] target was not found in the active dialog.") \
    X(ToastDialogItemOutOfRange, "Для [dialogitem(...)] пункт %s вне диапазона активного списка.", "The [dialogitem(...)] target %s is out of range for the active dialog list.") \
    X(ToastDialogItemReadFailed, "Не удалось прочитать пункты активного диалога для [dialogitem(...)].", "Failed to read the active dialog items for [dialogitem(...)]") \
    X(ToastDialogItemFailed, "Не удалось открыть выбранный пункт через [dialogitem(...)].", "Failed to activate the selected item via [dialogitem(...)]") \
    X(ToastDialogSelectNoActive, "Для [dialogselect(...)] нужен активный list/tablist диалог SA:MP.", "The [dialogselect(...)] tag requires an active SA:MP list/tablist dialog.") \
    X(ToastDialogSelectEmptyParam, "Для [dialogselect(...)] нужно указать номер или текст пункта.", "You must provide an item number or item text for [dialogselect(...)]") \
    X(ToastDialogSelectNotList, "Активный диалог не поддерживает выбор пункта через [dialogselect(...)].", "The active dialog does not support [dialogselect(...)] item selection.") \
    X(ToastDialogSelectNotFound, "Пункт для [dialogselect(...)] не найден в активном диалоге.", "The requested [dialogselect(...)] target was not found in the active dialog.") \
    X(ToastDialogSelectOutOfRange, "Для [dialogselect(...)] пункт %s вне диапазона активного списка.", "The [dialogselect(...)] target %s is out of range for the active dialog list.") \
    X(ToastDialogSelectReadFailed, "Не удалось прочитать пункты активного диалога для [dialogselect(...)].", "Failed to read the active dialog items for [dialogselect(...)]") \
    X(ToastDialogSelectFailed, "Не удалось выбрать пункт через [dialogselect(...)].", "Failed to select the requested item via [dialogselect(...)]") \
    X(ToastDialogResponseNoActive, "Для [dialogresponse(...)] нужен активный диалог SA:MP.", "The [dialogresponse(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogResponseInvalidFormat, "Для [dialogresponse(...)] нужен формат button;item;text", "The [dialogresponse(...)] tag expects the format button;item;text") \
    X(ToastDialogResponseInvalidButton, "Для [dialogresponse(...)] button должен быть 1 или 0.", "The [dialogresponse(...)] button must be 1 or 0.") \
    X(ToastDialogResponseReadFailed, "Не удалось прочитать активный диалог для [dialogresponse(...)].", "Failed to read the active dialog for [dialogresponse(...)]") \
    X(ToastDialogResponseItemNotList, "Активный диалог не поддерживает выбор пункта через [dialogresponse(...)].", "The active dialog does not support item selection via [dialogresponse(...)]") \
    X(ToastDialogResponseItemNotFound, "Пункт для [dialogresponse(...)] не найден в активном диалоге.", "The requested [dialogresponse(...)] item was not found in the active dialog.") \
    X(ToastDialogResponseItemOutOfRange, "Для [dialogresponse(...)] пункт %s вне диапазона активного списка.", "The [dialogresponse(...)] item %s is out of range for the active dialog list.") \
    X(ToastDialogResponseFailed, "Не удалось отправить ответ через [dialogresponse(...)].", "Failed to send the response via [dialogresponse(...)]") \
    X(ToastDialogTextNoActive, "Для [dialogtext(...)] нужен активный диалог SA:MP.", "The [dialogtext(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogTextEmptyParam, "Для [dialogtext(...)] нужно указать индекс.", "You must provide an index for [dialogtext(...)]") \
    X(ToastDialogTextInvalidIndex, "Для [dialogtext(...)] индекс должен быть целым числом >= 0.", "The [dialogtext(...)] index must be an integer >= 0.") \
    X(ToastDialogTextOutOfRange, "Для [dialogtext(...)] индекс %s вне диапазона 0..%s.", "The [dialogtext(...)] index %s is out of range 0..%s.") \
    X(ToastDialogTextReadFailed, "Не удалось прочитать текст активного диалога для [dialogtext(...)].", "Failed to read the active dialog text for [dialogtext(...)]") \
    X(ToastSaveDialogNoActive, "Для [save_dialog(...)] нужен активный диалог SA:MP.", "The [save_dialog(...)] tag requires an active SA:MP dialog.") \
    X(ToastSaveDialogDocumentsUnavailable, "Не удалось определить папку данных GTA SA для [save_dialog(...)].", "Failed to resolve the GTA SA userfiles folder for [save_dialog(...)]") \
    X(ToastSaveDialogCreateDirFailed, "Не удалось создать папку сохранения для [save_dialog(...)].", "Failed to create the save directory for [save_dialog(...)]") \
    X(ToastSaveDialogWriteFailed, "Не удалось сохранить диалог через [save_dialog(...)].", "Failed to save the dialog via [save_dialog(...)]") \
    X(ToastSaveDialogSuccess, "Диалог сохранён: %s", "Dialog saved: %s") \
    X(ToastIfAndOrInvalidSyntax, "Для [ifandor(...)] нужен формат Условие?Верно:Неверно", "The [ifandor(...)] tag requires the Condition?True:False format") \
    X(ToastIfAndOrEmptyCondition, "Для [ifandor(...)] нужно указать условие перед символом ?", "The [ifandor(...)] tag requires a condition before the ? symbol") \
    X(ToastIfAndOrConditionFailed, "Ошибка условия [ifandor(...)] : %s", "Failed to evaluate [ifandor(...)] condition: %s") \
    X(ToastBindSaved, "Бинд сохранён.", "Bind saved.") \
    X(ToastConfirmPrompt, "%s: подтвердить бинд \"%s\": [%s] принять, [%s] отменить", "%s: confirm bind \"%s\": [%s] accept, [%s] cancel") \
    X(ValidationFirstMessageRequired, "Заполните первую строку бинда.", "Fill in the first bind line.") \
    X(ValidationExistingFolderRequired, "Укажите существующую папку.", "Select an existing folder.") \
    X(ValidationFolderNameRequired, "Укажите название папки.", "Enter a folder name.") \
    X(ValidationFolderNameUnique, "Папка с таким названием уже есть здесь.", "A folder with this name already exists here.") \
    X(ValidationCategoryNameRequired, "Укажите название категории.", "Enter a category name.") \
    X(ValidationCategoryNameUnique, "Категория с таким названием уже есть.", "A category with this name already exists.") \
    X(ValidationRepeatInterval, "Интервал повтора не может быть отрицательным.", "Repeat interval cannot be negative.") \
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
    X(UndoFolderMove, "Отменить перемещение", "Undo move") \
    X(ToastFolderMoveInvalid, "Нельзя перенести папку сюда.", "Cannot move the folder here.") \
    X(FolderDropInto, "Сделать подпапкой", "Nest into folder") \
    X(BinderDropMoveHere, "Перенести сюда", "Move here") \
    X(BinderDropCurrentFolder, "Это уже текущая папка.", "This is already the current folder.") \
    X(BinderDropCrossCategoryDisabled, "Drag-and-drop между категориями отключён. Используйте \"Переместить в...\".", "Cross-category drag-and-drop is disabled. Use \"Move to...\".") \
    X(Name, "Название", "Name") \
    X(NameOptional, "Название (необяз.)", "Name (optional)") \
    X(Save, "Сохранить", "Save") \
    X(Done, "Готово", "Done") \
    X(Cancel, "Отмена", "Cancel") \
    X(DeleteFolderMoveBindsQuestion, "Удалить папку вместе с подпапками и биндами?", "Delete the folder together with its subfolders and binds?") \
    X(DeleteFolderAll, "Удалить всё", "Delete all") \
    X(DeleteFolderMoveContentsHere, "Перенести содержимое сюда", "Move contents here") \
    X(BinderRootName, "Биндер", "Binder") \
    X(CategoryAdd, "Добавить категорию", "Add category") \
    X(CategoryRenameTitle, "Переименование категории", "Rename category") \
    X(CategoryConditions, "Скрыть категорию, если активно", "Hide category when active") \
    X(CategoryQuickMenuConditions, "Блокировки быстрого меню", "Quick menu blocks") \
    X(CategoryCannotDeleteLast, "Нельзя удалить последнюю категорию.", "The last category cannot be deleted.") \
    X(DeleteCategoryQuestion, "Удалить категорию?", "Delete category?") \
    X(DeleteCategoryMoveContents, "Перенести и удалить", "Move and delete") \
    X(DeleteCategoryAll, "Удалить всё", "Delete all") \
    X(CategoryMoveContentsTarget, "Куда перенести", "Move contents to") \
    X(CategoryMoveLeft, "Переместить влево", "Move left") \
    X(CategoryMoveRight, "Переместить вправо", "Move right") \
    X(BinderEmptyFolder, "Папка пуста", "Folder is empty") \
    X(BinderSearchGlobal, "Поиск", "Search") \
    X(BinderGoUp, "Вверх", "Up") \
    X(BinderOpenFolder, "Открыть", "Open") \
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
    X(EditorPrimaryLaunch, "Основное", "Main") \
    X(EditorAdvancedLaunch, "Доп. запуск", "Extra launch") \
    X(EditorScenarioTab, "Сценарий", "Scenario") \
    X(EditorMultiInputTitle, "Мульти-ввод", "Multi-input") \
    X(EditorOpenMultiInput, "Мульти-ввод", "Multi-input") \
    X(EditorInputFieldsTab, "Параметры", "Parameters") \
    X(EditorOpenConditions, "Блокировки", "Blocks") \
    X(EditorConditionsNone, "Блокировок: нет", "Blocks: none") \
    X(EditorConditionsCount, "Блокировок: %d", "Blocks: %d") \
    X(EditorBack, "Назад", "Back") \
    X(EditorPreviousBind, "Предыдущий бинд", "Previous bind") \
    X(EditorNextBind, "Следующий бинд", "Next bind") \
    X(EditorUnsaved, "Несохранённые изменения", "Unsaved changes") \
    X(EditorTriggerHint, "Срабатывает, когда вы отправляете указанную фразу в чат или в виде команды.", "Triggers when you send the specified phrase to chat or as a command.") \
    X(EditorTriggerToggleHint, "Включить или выключить триггер по тексту.", "Enable or disable the text trigger.") \
    X(EditorTriggerPatternMode, "Режим шаблона", "Pattern mode") \
    X(EditorTriggerPatternTooltip, "Regex ECMAScript для текстового триггера.\n\nЧастое:\n. — любой символ\n.* — любой текст\n\\d+ — одно или больше чисел\n\\w+ — слово/идентификатор\n^ и $ — начало и конец строки\n(a|b) — один из вариантов\n[abc] — один символ из набора\n\\. — обычная точка\n\nПримеры:\n^[Гг]олова$\n^дом\\d+$\n^(куплю|продам)\\s+\\d+$", "ECMAScript regex for the text trigger.\n\nCommon:\n. — any character\n.* — any text\n\\d+ — one or more digits\n\\w+ — word/identifier\n^ and $ — start and end of line\n(a|b) — one of the alternatives\n[abc] — one character from a set\n\\. — literal dot\n\nExamples:\n^[Hh]ead$\n^house\\d+$\n^(buy|sell)\\s+\\d+$") \
    X(EditorTriggerExample, "Например: Голова, [Гг]олова, ^дом\\d+$", "For example: Head, [Hh]ead, ^house\\d+$") \
    X(EditorScenarioHint, "Перетащите ручку слева, чтобы изменить порядок шагов.", "Drag the handle on the left to reorder steps.") \
    X(EditorAddStep, "Шаг", "Step") \
    X(EditorDuplicateStep, "Дублировать шаг", "Duplicate step") \
    X(EditorMoveStep, "Переместить шаг", "Move step") \
    X(EditorVariables, "Переменные", "Variables") \
    X(EditorVariablesTitle, "Переменные бинда", "Bind variables") \
    X(EditorVariablesHint, "Используйте {{KEY}} или {{1}} в сообщениях, чтобы подставить значения параметров.", "Use {{KEY}} or {{1}} in messages to insert parameter values.") \
    X(EditorVariablesEmpty, "Параметры ещё не настроены.", "No parameters are configured yet.") \
    X(EditorVariablesParametersTab, "Параметры бинда", "Bind parameters") \
    X(EditorVariablesSimpleTab, "Обычные", "Simple") \
    X(EditorVariablesFunctionTab, "Функциональные", "Functional") \
    X(EditorVariablesInspectorEmpty, "Выберите строку слева, чтобы увидеть варианты копирования.", "Select an item on the left to view copy options.") \
    X(EditorVariablesParameterClickHint, "Щелчок по строке слева копирует основной плейсхолдер текущего параметра.", "Clicking a row on the left copies the primary placeholder for that parameter.") \
    X(EditorVariablesSimpleClickHint, "Щелчок по строке слева копирует основу тега.", "Clicking a row on the left copies the tag token.") \
    X(EditorVariablesFunctionClickHint, "Щелчок по строке слева копирует готовый пример.", "Clicking a row on the left copies the ready-made example.") \
    X(EditorDiscardTitle, "Несохранённые изменения", "Unsaved changes") \
    X(EditorDiscardMessage, "Изменения не сохранены. Продолжить и потерять правки?", "Changes are not saved. Continue and discard them?") \
    X(EditorDiscardAction, "Продолжить", "Continue") \
    X(EditorStay, "Остаться", "Stay") \
    X(EditorColumnMessage, "Сообщение", "Message") \
    X(EditorColumnPauseMs, "Пауза (мс)", "Pause (ms)") \
    X(EditorColumnDestination, "Куда", "Destination") \
    X(EditorConfirmationHint, "После активации по триггеру или команде бинд ждёт отдельные клавиши подтверждения и отклонения.", "After trigger or command activation, the bind waits for separate confirm and cancel keys.") \
    X(EditorMultiInputHint, "Каждая непустая строка станет отдельным шагом. Пустые строки игнорируются.", "Each non-empty line becomes a separate step. Empty lines are ignored.") \
    X(Enabled, "Включён", "Enabled") \
    X(Folder, "Папка", "Folder") \
    X(HotkeyMode, "Режим хоткея", "Hotkey mode") \
    X(HotkeyNotSet, "Не задано", "Not set") \
    X(HotkeyFormat, "Хоткей: %s", "Hotkey: %s") \
    X(ChangeHotkey, "Изменить хоткей", "Change hotkey") \
    X(ShowInQuickMenu, "Показывать в быстром меню", "Show in quick menu") \
    X(EditorToggleQuickMenu, "Быстрое меню", "Quick menu") \
    X(EditorToggleTrigger, "Триггер", "Trigger") \
    X(EditorTogglePattern, "Шаблон", "Pattern") \
    X(EditorToggleTextConfirm, "Подтв. триггер", "Confirm trigger") \
    X(EditorToggleCommandConfirm, "Подтв. команда", "Confirm command") \
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
    X(CommandConfirmation, "Требовать подтверждение по команде", "Require confirmation on command") \
    X(WaitWithoutTimeout, "Дожидаться подтверждения или отклонения", "Wait for confirmation or rejection") \
    X(TriggerWaitWithoutTimeout, "Ждать подтверждения по триггеру до таймера", "Wait for trigger confirmation until timeout") \
    X(TriggerWaitTimeoutHint, "Таймер настраивается во вкладке «Настройки» -> «Биндер». По умолчанию: 60 секунд.", "The timeout is configured in Settings -> Binder. Default: 60 seconds.") \
    X(CommandWaitWithoutTimeout, "Дожидаться подтверждения или отклонения по команде", "Wait for confirmation or rejection on command") \
    X(ConfirmKeyFormat, "Клавиша подтверждения: %s", "Confirm key: %s") \
    X(CancelKeyFormat, "Клавиша отклонения: %s", "Cancel key: %s") \
    X(Change, "Изменить", "Change") \
    X(BlockingConditions, "Не запускать, если активно", "Do not run when active") \
    X(FolderConditions, "Скрыть папку, если активно", "Hide folder when active") \
    X(ConditionBlockHint, "Сработает блокировка, если активно хотя бы одно отмеченное условие.", "Blocks when any selected condition is active.") \
    X(InputDialogSearchHint, "Поиск по названию, тексту или подсказке", "Search by label, text, or hint") \
    X(InputDialogNoOptions, "Нет доступных вариантов.", "No available options.") \
    X(InputDialogPreviewTitle, "Предпросмотр отправки", "Send preview") \
    X(InputDialogPreviewEmpty, "В сценарии нет сообщений для отправки.", "There are no messages to send in this scenario.") \
    X(AddBind, "+ Бинд", "+ Bind") \
    X(Edit, "Изменить", "Edit") \
    X(Run, "Запустить", "Run") \
    X(Resume, "Продолжить", "Resume") \
    X(Pause, "Пауза", "Pause") \
    X(Stop, "Стоп", "Stop") \
    X(SearchBinds, "Поиск биндов", "Search binds") \
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
    X(QuickMenuStyle, "Стиль быстрого меню", "Quick menu style") \
    X(QuickMenuStyleTree, "Стиль 1: дерево", "Style 1: tree") \
    X(QuickMenuStyleCascade, "Стиль 2: каскадное меню", "Style 2: cascaded menu") \
    X(ColumnFolders, "Папки", "Folders") \
    X(ColumnBinds, "Бинды", "Binds")

enum class UiLanguage {
    Russian = 0,
    English,
};

enum class UiLogLevel : int {
    Off = 0,
    Error = 1,
    Info = 2,
};

enum class UiSettingsSection {
    General = 0,
    Binder,
    QuickMenu,
    Notifications,
    Profiles,
    Hotkeys,
    Diagnostics,
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

    UiLogLevel LogLevel() const;
    void SetLogLevel(UiLogLevel level);
    bool ApplyDamageProtectionEnabled() const;
    void SetApplyDamageProtectionEnabled(bool enabled);

    const std::vector<unsigned int>& MenuToggleHotkey() const;
    void SetMenuToggleHotkey(const std::vector<unsigned int>& hotkey);
    void ResetMenuToggleHotkey();

    UiSettingsSection SettingsActiveSection() const;
    void SetSettingsActiveSection(UiSettingsSection section);

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
    UiLogLevel logLevel_ = UiLogLevel::Info;
    bool applyDamageProtectionEnabled_ = true;
    std::vector<unsigned int> menuToggleHotkey_{};
    UiSettingsSection settingsActiveSection_ = UiSettingsSection::General;
    float currentScale_ = 1.0f;
};
