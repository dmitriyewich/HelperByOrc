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
    X(TagsBuiltinKeyDownDescription, "Зажимает указанную виртуальную клавишу Windows на заданное количество миллисекунд. Формат: [keydown(КодКлавиши;Мс)]. В уже запущенном бинде тег ставит выполнение на паузу до конца удержания.", "Holds the specified Windows virtual key for the requested number of milliseconds. Format: [keydown(KeyCode;Ms)]. In a running bind, the tag pauses execution until the hold finishes.") \
    X(TagsBuiltinStrLowDescription, "Возвращает текст в нижнем регистре. Работает и для кириллицы.", "Returns the text in lowercase. Cyrillic text is supported as well.") \
    X(TagsBuiltinAddTimeDescription, "Возвращает текущее локальное время плюс указанное смещение. Поддерживает форматы MM:SS и HH:MM:SS. Результат всегда выводится как %H:%M:%S.", "Returns the current local time plus the specified offset. Supports MM:SS and HH:MM:SS formats. The result is always printed as %H:%M:%S.") \
    X(TagsBuiltinRandomDescription, "Генерирует случайное значение. [random()] даёт число от -2147483647 до 2147483647, [random(10)] — от 1 до 10, [random(20-30)] — от 20 до 30, [random(Да;Нет;Не знаю)] — случайный вариант из списка.", "Generates a random value. [random()] returns a number from -2147483647 to 2147483647, [random(10)] returns 1 to 10, [random(20-30)] returns 20 to 30, and [random(Yes;No;Maybe)] picks a random option from the list.") \
    X(TagsBuiltinIfAndOrDescription, "Возвращает один из двух вариантов по условию.\n\nСинтаксис:\n[ifandor(Условие?Верно:Неверно)]\n\nКак это работает:\n1. Сначала вычисляется только Условие.\n2. Если условие истинно, раскрывается и выполняется только часть Верно.\n3. Если условие ложно, раскрывается и выполняется только часть Неверно.\n4. Невыбранная ветка вообще не раскрывается и не выполняется.\n\nЭто важно:\n- Внутри Верно и Неверно можно безопасно использовать функциональные переменные с действиями, например [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Выполнится только выбранная ветка.\n- Внутри самого условия side effects запрещены: условие нужно только для проверки.\n\nЧто можно писать в условии:\n- операторы сравнения: ==, !=, >, >=, <, <=\n- логические операторы: and, or, not\n- круглые скобки: ( )\n- числа: 1, 74, 12.5\n- строки в кавычках: \"text\" или 'text'\n- булевы значения: true, false\n- обычные и безопасные функциональные переменные после подстановки, например {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nКак сравниваются значения:\n- Если обе стороны похожи на числа, сравнение будет числовым.\n- Иначе сравнение будет строковым.\n\nПримеры:\n- [ifandor({id}==74?Мой id 74:Мой id не 74)]\n- [ifandor(1>0?[bindstart(\"10\" \"folder\")]:[bindstart(\"11\" \"folder\")])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?День:Не день)]\n- [ifandor([bindended({thisbind})]==1?Бинд завершён:Бинд ещё работает)]\n\nВажно по синтаксису:\n- Формат строго один: Условие?Верно:Неверно\n- Если в строках есть текст, лучше брать его в кавычки при сравнении.\n- Старый формат вида @...@ не используется. Используйте текущий синтаксис [ ... ].", "Returns one of two branches by condition.\n\nSyntax:\n[ifandor(Condition?True:False)]\n\nHow it works:\n1. Only the Condition is evaluated first.\n2. If the condition is true, only the True branch is expanded and executed.\n3. If the condition is false, only the False branch is expanded and executed.\n4. The branch that was not selected is never expanded or executed.\n\nThis matters:\n- You can safely use action-oriented functional variables inside True and False, such as [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Only the selected branch runs.\n- Side effects are forbidden inside the condition itself; the condition is for checking only.\n\nSupported inside the condition:\n- comparison operators: ==, !=, >, >=, <, <=\n- logical operators: and, or, not\n- parentheses: ( )\n- numbers: 1, 74, 12.5\n- quoted strings: \"text\" or 'text'\n- boolean values: true, false\n- regular and safe functional variables after expansion, such as {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nHow values are compared:\n- If both sides look like numbers, comparison is numeric.\n- Otherwise comparison is string-based.\n\nExamples:\n- [ifandor({id}==74?My id is 74:My id is not 74)]\n- [ifandor(1>0?[bindstart(\"10\" \"folder\")]:[bindstart(\"11\" \"folder\")])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?Day:Not day)]\n- [ifandor([bindended({thisbind})]==1?Bind finished:Bind still running)]\n\nSyntax notes:\n- The format is strictly Condition?True:False\n- If you compare text, quoting it is recommended.\n- The old @...@ form is not used. Use the current [ ... ] syntax.") \
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
    X(SettingsLogLevel, "Уровень логирования", "Log level") \
    X(SettingsLogLevelOff, "Off", "Off") \
    X(SettingsLogLevelError, "Error", "Error") \
    X(SettingsLogLevelInfo, "Info", "Info") \
    X(SettingsApplyDamageProtection, "Защита компонентов транспорта от отваливания", "Vehicle components fall-off protection") \
    X(SettingsScaleHint, "Автомасштаб берёт за основу 1920x1080 и подстраивает UI под текущее разрешение. Его можно отключить или скорректировать множителем.", "Auto scale uses 1920x1080 as the reference and adapts the UI to the current resolution. You can disable it or fine-tune it with the multiplier.") \
    X(SettingsMainWindowHotkey, "Хоткей открытия главного окна", "Main window hotkey") \
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
    X(ConditionSampCursorActive, "Активный курсор SA:MP", "SA:MP cursor active") \
    X(ConditionWindowsCursorActive, "Активный курсор Windows", "Windows cursor active") \
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
    X(ToastConditionBlocked, "Условия не выполнены: %s", "Conditions not met: %s") \
    X(ToastConditionRequireAnyNotMet, "Ни одно из отмеченных условий не выполнено.", "None of the selected conditions are met.") \
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
    X(UndoFolderMove, "Отменить перемещение", "Undo move") \
    X(ToastFolderMoveInvalid, "Нельзя перенести папку сюда.", "Cannot move the folder here.") \
    X(FolderDropInto, "Сделать подпапкой", "Nest into folder") \
    X(Name, "Название", "Name") \
    X(Save, "Сохранить", "Save") \
    X(Cancel, "Отмена", "Cancel") \
    X(DeleteFolderMoveBindsQuestion, "Удалить папку вместе с подпапками и биндами?", "Delete the folder together with its subfolders and binds?") \
    X(DeleteFolderAll, "Удалить всё", "Delete all") \
    X(DeleteFolderMoveContentsHere, "Перенести содержимое сюда", "Move contents here") \
    X(BinderRootName, "Биндер", "Binder") \
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
    X(TriggerWaitWithoutTimeout, "Дожидаться подтверждения или отклонения по триггеру", "Wait for confirmation or rejection on trigger") \
    X(CommandWaitWithoutTimeout, "Дожидаться подтверждения или отклонения по команде", "Wait for confirmation or rejection on command") \
    X(ConfirmKeyFormat, "Клавиша подтверждения: %s", "Confirm key: %s") \
    X(CancelKeyFormat, "Клавиша отклонения: %s", "Cancel key: %s") \
    X(Change, "Изменить", "Change") \
    X(BlockingConditions, "Условия бинда", "Bind conditions") \
    X(FolderConditions, "Условия папки", "Folder conditions") \
    X(ConditionCombineModeLabel, "Связка условий", "Condition combination") \
    X(ConditionCombineRequireAll, "Все отмеченные (И)", "All selected (AND)") \
    X(ConditionCombineRequireAny, "Любое отмеченное (ИЛИ)", "Any selected (OR)") \
    X(ConditionCombineHint, "И — нужны все отмеченные состояния.\nИЛИ — достаточно любого одного.", "AND requires every checked state.\nOR needs at least one checked state.") \
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
    float currentScale_ = 1.0f;
};
