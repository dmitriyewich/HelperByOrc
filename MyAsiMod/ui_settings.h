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
    X(MiscHomeIntro, "Во вкладке собраны отдельные служебные разделы. Нажмите на карточку нужного модуля, чтобы открыть его экран.", "This tab groups standalone utility sections. Click a module card to open its screen.") \
    X(MiscVariablesEntryDesc, "Открывает каталог встроенных переменных, описание тегов и sandbox-предпросмотр для быстрой проверки.", "Opens the built-in variables catalog, tag descriptions, and a sandbox preview for quick checks.") \
    X(MiscOpenSectionAction, "Открыть", "Open") \
    X(TagsKindSimple, "Простая", "Simple") \
    X(TagsKindFunction, "Функциональная", "Function") \
    X(TagsBuiltinIdDescription, "Возвращает ваш локальный ID игрока через SampApi::Local_ID().", "Returns your local player ID via SampApi::Local_ID().") \
    X(TagsBuiltinNickDescription, "Возвращает ваш текущий ник через GetNameID(Local_ID()).", "Returns your current nickname via GetNameID(Local_ID()).") \
    X(TagsBuiltinThisbindDescription, "Возвращает имя и папку текущего запущенного бинда в формате, пригодном для bind-тегов.", "Returns the name and folder of the currently running bind in a format suitable for bind tags.") \
    X(TagsBuiltinBindStopAllDescription, "Останавливает все запущенные бинды и возвращает пустую строку.", "Stops every running bind and returns an empty string.") \
    X(TagsBuiltinTargetIdDescription, "Возвращает ID игрока, в которого вы целились последним.", "Returns the ID of the player you aimed at most recently.") \
    X(TagsBuiltinTargetNickDescription, "Возвращает ник игрока, в которого вы целились последним.", "Returns the nickname of the player you aimed at most recently.") \
    X(TagsBuiltinTargetRpNickDescription, "Возвращает RP-ник последней цели: Walcher_Flett станет Walcher Flett.", "Returns the RP nickname of the last target: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinTargetNameDescription, "Возвращает имя из ника последней цели до символа подчёркивания.", "Returns the first name from the last target nickname before the underscore.") \
    X(TagsBuiltinTargetSurnameDescription, "Возвращает фамилию из ника последней цели после символа подчёркивания.", "Returns the surname from the last target nickname after the underscore.") \
    X(TagsBuiltinClosestIdDescription, "Возвращает ID ближайшего к вам стримнутого игрока. Считается прямо в момент раскрытия переменной.", "Returns the ID of the streamed player closest to you. It is evaluated on demand when the variable is expanded.") \
    X(TagsBuiltinClosestIdToCenterDescription, "Возвращает ID стримнутого игрока, который находится ближе всего к центру экрана среди видимых на экране.", "Returns the ID of the streamed player closest to the screen center among those currently visible on screen.") \
    X(TagsBuiltinClosestNameDescription, "Возвращает имя из ника ближайшего к вам стримнутого игрока.", "Returns the first name from the nickname of the streamed player closest to you.") \
    X(TagsBuiltinClosestSurnameDescription, "Возвращает фамилию из ника ближайшего к вам стримнутого игрока.", "Returns the surname from the nickname of the streamed player closest to you.") \
    X(TagsBuiltinArmourDescription, "Возвращает вашу текущую броню. Если брони нет или значение недоступно, вернёт 0.", "Returns your current armour. If there is no armour or the value is unavailable, it returns 0.") \
    X(TagsBuiltinHealthDescription, "Возвращает ваше текущее здоровье локального игрока.", "Returns the current health of the local player.") \
    X(TagsBuiltinDateDescription, "Возвращает текущую локальную дату в формате ДД.ММ.ГГГГ.", "Returns the current local date formatted as DD.MM.YYYY.") \
    X(TagsBuiltinMySkinDescription, "Возвращает ID текущего скина локального игрока через model index педа.", "Returns the current local player skin ID via the ped model index.") \
    X(TagsBuiltinGetVehTypeDescription, "Возвращает тип транспорта, в котором сейчас находится локальный игрок. Если вы не в транспорте, возвращает пустую строку.", "Returns the type of vehicle the local player is currently in. If you are not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinScreenDescription, "Делает скриншот игры и сохраняет его в Documents\\\\GTA San Andreas User Files\\\\HelperByOrc\\\\screens.", "Takes a game screenshot and saves it to Documents\\\\GTA San Andreas User Files\\\\HelperByOrc\\\\screens.") \
    X(TagsBuiltinScreenFunctionDescription, "Делает скриншот игры и сохраняет его в указанную подпапку внутри Documents\\\\GTA San Andreas User Files\\\\HelperByOrc\\\\screens.", "Takes a game screenshot and saves it to the specified subfolder inside Documents\\\\GTA San Andreas User Files\\\\HelperByOrc\\\\screens.") \
    X(TagsBuiltinTPhotoDescription, "Делает игровое фото через механику фотоаппарата GTA SA и сохраняет его в галерею игры.", "Takes an in-game camera photo through GTA SA's photo mechanic and saves it to the game's gallery.") \
    X(TagsBuiltinNickRpDescription, "Возвращает ваш ник в RP-виде: Walcher_Flett станет Walcher Flett.", "Returns your nickname in RP form: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinNameDescription, "Возвращает имя из ника до символа подчёркивания: Walcher_Flett станет Walcher.", "Returns the first name before the underscore: Walcher_Flett becomes Walcher.") \
    X(TagsBuiltinSurnameDescription, "Возвращает фамилию из ника после символа подчёркивания: Walcher_Flett станет Flett.", "Returns the surname after the underscore: Walcher_Flett becomes Flett.") \
    X(TagsBuiltinTimeDescription, "Возвращает текущее локальное время в формате %H:%M:%S.", "Returns the current local time formatted as %H:%M:%S.") \
    X(TagsBuiltinTimeNoSecDescription, "Возвращает текущее локальное время без секунд в формате %H:%M.", "Returns the current local time without seconds in %H:%M format.") \
    X(TagsBuiltinTimefDescription, "Пишет текущее локальное время в указанном формате strftime. В конце формата обязательно нужна точка с запятой ;\n%a — сокращённое название дня недели\n%A — полное название дня недели\n%b — сокращённое название месяца\n%B — полное название месяца\n%c — дата и время целиком\n%d — день месяца [01-31]\n%H — час в 24-часовом формате [00-23]\n%I — час в 12-часовом формате [01-12]\n%M — минуты [00-59]\n%m — месяц [01-12]\n%p — am или pm\n%S — секунды [00-61]\n%w — день недели [0-6, где 0 — воскресенье]\n%x — дата\n%X — время\n%Y — полный год\n%y — двухзначный год [00-99]\n%% — символ %", "Prints the current local time using the specified strftime format. The format must end with a semicolon ;\n%a — abbreviated weekday name\n%A — full weekday name\n%b — abbreviated month name\n%B — full month name\n%c — full date and time\n%d — day of month [01-31]\n%H — hour in 24-hour format [00-23]\n%I — hour in 12-hour format [01-12]\n%M — minute [00-59]\n%m — month [01-12]\n%p — am or pm\n%S — second [00-61]\n%w — weekday [0-6, Sunday = 0]\n%x — date\n%X — time\n%Y — full year\n%y — two-digit year [00-99]\n%% — percent sign") \
    X(TagsBuiltinNickFunctionDescription, "Возвращает ник игрока по указанному ID. Работает и в обычном чате, и в биндах.", "Returns a player's nickname for the specified ID. Works in regular chat and in binds.") \
    X(TagsBuiltinParamcmdDescription, "Достаёт параметры из команды, которой был запущен бинд. Поддерживает селекторы 1, 1+, 3-, 2-4.", "Extracts arguments from the command that launched the bind. Supports selectors like 1, 1+, 3-, and 2-4.") \
    X(TagsBuiltinKeyEmulateDescription, "Эмулирует одно нажатие указанной виртуальной клавиши Windows и ничего не вставляет в текст. В sandbox-предпросмотре не срабатывает.", "Emulates a single press of the specified Windows virtual key and inserts no text. It stays inactive in sandbox previews.") \
    X(TagsBuiltinMathDescription, "Вычисляет арифметическое выражение. Поддерживает +, -, *, /, %, скобки и унарные знаки.", "Evaluates an arithmetic expression. Supports +, -, *, /, %, parentheses, and unary signs.") \
    X(TagsBuiltinNumberWithDotsDescription, "Форматирует число, разделяя целую часть точками по тысячам: 1000 станет 1.000, а -12345.67 станет -12.345.67.", "Formats a number by inserting dots every three digits in the integer part: 1000 becomes 1.000 and -12345.67 becomes -12.345.67.") \
    X(TagsBuiltinIfAndOrDescription, "Возвращает один из двух вариантов по условию.\n\nСинтаксис:\n[ifandor(Условие?Верно:Неверно)]\n\nКак это работает:\n1. Сначала вычисляется только Условие.\n2. Если условие истинно, раскрывается и выполняется только часть Верно.\n3. Если условие ложно, раскрывается и выполняется только часть Неверно.\n4. Невыбранная ветка вообще не раскрывается и не выполняется.\n\nЭто важно:\n- Внутри Верно и Неверно можно безопасно использовать функциональные переменные с действиями, например [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Выполнится только выбранная ветка.\n- Внутри самого условия side effects запрещены: условие нужно только для проверки.\n\nЧто можно писать в условии:\n- операторы сравнения: ==, !=, >, >=, <, <=\n- логические операторы: and, or, not\n- круглые скобки: ( )\n- числа: 1, 74, 12.5\n- строки в кавычках: \"text\" или 'text'\n- булевы значения: true, false\n- обычные и безопасные функциональные переменные после подстановки, например {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nКак сравниваются значения:\n- Если обе стороны похожи на числа, сравнение будет числовым.\n- Иначе сравнение будет строковым.\n\nПримеры:\n- [ifandor({id}==74?Мой id 74:Мой id не 74)]\n- [ifandor(1>0?[bindstart(\"10\" \"folder\")]:[bindstart(\"11\" \"folder\")])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?День:Не день)]\n- [ifandor([bindended({thisbind})]==1?Бинд завершён:Бинд ещё работает)]\n\nВажно по синтаксису:\n- Формат строго один: Условие?Верно:Неверно\n- Если в строках есть текст, лучше брать его в кавычки при сравнении.\n- Старый формат вида @...@ не используется. Используйте текущий синтаксис [ ... ].", "Returns one of two branches by condition.\n\nSyntax:\n[ifandor(Condition?True:False)]\n\nHow it works:\n1. Only the Condition is evaluated first.\n2. If the condition is true, only the True branch is expanded and executed.\n3. If the condition is false, only the False branch is expanded and executed.\n4. The branch that was not selected is never expanded or executed.\n\nThis matters:\n- You can safely use action-oriented functional variables inside True and False, such as [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Only the selected branch runs.\n- Side effects are forbidden inside the condition itself; the condition is for checking only.\n\nSupported inside the condition:\n- comparison operators: ==, !=, >, >=, <, <=\n- logical operators: and, or, not\n- parentheses: ( )\n- numbers: 1, 74, 12.5\n- quoted strings: \"text\" or 'text'\n- boolean values: true, false\n- regular and safe functional variables after expansion, such as {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nHow values are compared:\n- If both sides look like numbers, comparison is numeric.\n- Otherwise comparison is string-based.\n\nExamples:\n- [ifandor({id}==74?My id is 74:My id is not 74)]\n- [ifandor(1>0?[bindstart(\"10\" \"folder\")]:[bindstart(\"11\" \"folder\")])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?Day:Not day)]\n- [ifandor([bindended({thisbind})]==1?Bind finished:Bind still running)]\n\nSyntax notes:\n- The format is strictly Condition?True:False\n- If you compare text, quoting it is recommended.\n- The old @...@ form is not used. Use the current [ ... ] syntax.") \
    X(TagsBuiltinGetVehTypeFunctionDescription, "Возвращает тип транспорта игрока по указанному ID. Если игрок не найден или не находится в транспорте, возвращает пустую строку.", "Returns the type of vehicle used by the specified player ID. If the player is not found or is not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinWaitDescription, "Переопределяет паузу до следующей строки у уже запущенного бинда. Работает как обычная задержка между строками и ничего не вставляет в текст.", "Overrides the delay before the next line of an already running bind. Works like the regular delay between lines and inserts no text.") \
    X(TagsBuiltinBindDisableDescription, "Выключает бинд и сразу сохраняет изменение в конфиг.", "Disables a bind and persists the change to the config immediately.") \
    X(TagsBuiltinBindEnableDescription, "Включает ранее деактивированный бинд и сразу сохраняет изменение в конфиг.", "Enables a previously deactivated bind and persists the change to the config immediately.") \
    X(TagsBuiltinBindStartDescription, "Запускает выбранный бинд, если он включён и сейчас не выполняется.", "Starts the selected bind if it is enabled and not currently running.") \
    X(TagsBuiltinBindStopDescription, "Останавливает уже запущенный бинд.", "Stops a bind that is currently running.") \
    X(TagsBuiltinBindPauseDescription, "Ставит запущенный бинд на паузу.", "Pauses a currently running bind.") \
    X(TagsBuiltinBindUnpauseDescription, "Снимает паузу с запущенного бинда.", "Unpauses a paused bind.") \
    X(TagsBuiltinBindFastMenuDescription, "Показывает выбранный бинд в быстром меню и сразу сохраняет изменение.", "Shows the selected bind in the quick menu and persists the change immediately.") \
    X(TagsBuiltinBindUnfastMenuDescription, "Убирает выбранный бинд из быстрого меню и сразу сохраняет изменение.", "Removes the selected bind from the quick menu and persists the change immediately.") \
    X(TagsBuiltinBindRandomDescription, "Запускает случайный включённый бинд из выбранной папки.", "Starts a random enabled bind from the selected folder.") \
    X(TagsBuiltinBindEndedDescription, "Проверяет завершение бинда и возвращает 1 или 0.", "Checks whether the bind has ended and returns 1 or 0.") \
    X(TagsBuiltinBindPopupDescription, "Открывает попап со списком строк бинда для быстрой отправки.", "Opens a popup with the bind's lines for quick sending.") \
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
    X(NotepadIntro, "Вкладка оставлена как пустая оболочка. При необходимости сюда можно вернуть заметки или встроенный редактор текста.", "The tab is kept as an empty shell. Notes or an embedded text editor can be restored here later if needed.") \
    X(SettingsIntro, "Здесь настраиваются язык интерфейса, автоматический масштаб под разрешение экрана, пользовательский множитель масштаба и параметры быстрого меню.", "Configure interface language, automatic scaling for screen resolution, a custom scale multiplier, and quick menu settings here.") \
    X(GtaVersionFormat, "Версия GTA: %s", "GTA version: %s") \
    X(SettingsLanguage, "Язык", "Language") \
    X(SettingsUiScale, "Масштаб интерфейса", "Interface Scale") \
    X(SettingsAutoScale, "Автомасштаб под разрешение", "Auto scale for resolution") \
    X(SettingsScaleMultiplier, "Пользовательский множитель", "Custom multiplier") \
    X(SettingsEffectiveScale, "Итоговый масштаб", "Effective scale") \
    X(SettingsBlockSampHotkeys, "Блокировать горячие клавиши SA:MP в главном окне", "Block SA:MP hotkeys in the main window") \
    X(SettingsBlockSampHotkeysHint, "Когда главное окно открыто, клавиши вроде T, F6, Tab и другие горячие клавиши SA:MP не срабатывают. Отключите это, если хотите оставить стандартные клавиши SA:MP активными поверх окна.", "When the main window is open, keys like T, F6, Tab, and other SA:MP hotkeys are suppressed. Turn this off if you want the standard SA:MP hotkeys to stay active while the window is open.") \
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
    X(ToastBindTagTargetRequired, "Для %s нужно указать бинд или вызвать тег внутри запущенного бинда.", "%s requires a bind target or must be called from a running bind.") \
    X(ToastBindTagNoFolders, "Папки биндов ещё не созданы.", "No bind folders have been created yet.") \
    X(ToastBindTagFolderNotFound, "Папка для %s не найдена.", "The folder for %s was not found.") \
    X(ToastBindTagBindNotFound, "Бинд для %s не найден.", "The bind for %s was not found.") \
    X(ToastBindTagNotStarted, "Не удалось запустить бинд для %s.", "Failed to start the bind for %s.") \
    X(ToastBindTagNotRunning, "Бинд для %s не запущен.", "The bind for %s is not running.") \
    X(ToastBindTagNotPaused, "Бинд для %s не стоит на паузе.", "The bind for %s is not paused.") \
    X(ToastBindTagNoChanges, "%s не изменил состояние бинда.", "%s did not change the bind state.") \
    X(ToastBindTagPopupUnavailable, "Не удалось открыть попап строк бинда для %s.", "Failed to open the bind lines popup for %s.") \
    X(ToastBindTagStopAllEmpty, "Нет запущенных биндов для остановки.", "There are no running binds to stop.") \
    X(ToastBindTagUnknownAction, "Неизвестное действие bind-тега: %s", "Unknown bind-tag action: %s") \
    X(ToastScreenDocumentsUnavailable, "Не удалось определить папку Documents для сохранения скриншота.", "Failed to resolve the Documents folder for saving the screenshot.") \
    X(ToastScreenInvalidFolder, "Недопустимая подпапка для [screen(...)]: %s", "Invalid subfolder for [screen(...)]: %s") \
    X(ToastScreenCaptureFailed, "Не удалось сделать скриншот игры.", "Failed to capture the game screenshot.") \
    X(ToastTPhotoFailed, "Не удалось сделать фото через игровой фотоаппарат.", "Failed to take a photo through the in-game camera.") \
    X(ToastTimefMissingSemicolon, "Для [timef(...)] формат должен заканчиваться точкой с запятой ;", "The format for [timef(...)] must end with a semicolon ;") \
    X(ToastTimefEmptyFormat, "Для [timef(...)] нужно указать формат времени перед ;", "You must provide a time format before ; in [timef(...)]") \
    X(ToastTimefInvalidSpecifier, "Неподдерживаемый параметр [timef(...)] : %s", "Unsupported [timef(...)] specifier: %s") \
    X(ToastTimefFormatFailed, "Не удалось отформатировать время для [timef(...)].", "Failed to format time for [timef(...)]") \
    X(ToastIfAndOrInvalidSyntax, "Для [ifandor(...)] нужен формат Условие?Верно:Неверно", "The [ifandor(...)] tag requires the Condition?True:False format") \
    X(ToastIfAndOrEmptyCondition, "Для [ifandor(...)] нужно указать условие перед символом ?", "The [ifandor(...)] tag requires a condition before the ? symbol") \
    X(ToastIfAndOrConditionFailed, "Ошибка условия [ifandor(...)] : %s", "Failed to evaluate [ifandor(...)] condition: %s") \
    X(ToastBindSaved, "Бинд сохранён.", "Bind saved.") \
    X(ToastConfirmPrompt, "Подтвердить бинд \"%s\": [%s] принять, [%s] отменить", "Confirm bind \"%s\": [%s] accept, [%s] cancel") \
    X(ValidationBindNameRequired, "Укажите название бинда.", "Enter a bind name.") \
    X(ValidationExistingFolderRequired, "Укажите существующую папку.", "Select an existing folder.") \
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
    X(Resume, "Продолжить", "Resume") \
    X(Pause, "Пауза", "Pause") \
    X(Stop, "Стоп", "Stop") \
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

    bool BlockSampHotkeysInMainWindow() const;
    void SetBlockSampHotkeysInMainWindow(bool enabled);

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
    bool blockSampHotkeysInMainWindow_ = true;
    std::vector<unsigned int> menuToggleHotkey_{};
    float currentScale_ = 1.0f;
};
