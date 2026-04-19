[b]HelperByOrc[/b] — нативный ASI-плагин на C++ для GTA San Andreas / SA:MP. Перенос логики HelperByOrc с Lua/MoonLoader на Win32-модуль.

[b]Исходники на GitHub:[/b] [url=https://github.com/dmitriyewich/HelperByOrc]https://github.com/dmitriyewich/HelperByOrc[/url]

[b]Что на GitHub:[/b] исходный код плагина, профиль сборки (vcxproj, slnx и связанные файлы) и vendored [b]HelperByOrc\external[/b] (обычные файлы без вложенных .git). Готовые .asi/.pdb и каталоги сборки в репозиторий не выкладываются. Служебное вроде корневого .gitignore, context.md, .cursor/ — только локально.

[b]Платформа:[/b] Win32 / x86, формат ASI.

[b]Сборка:[/b] Visual Studio + MSBuild, конфигурация Release | Win32. Пример (в PowerShell параметр [b]Platform[/b] лучше в кавычках):
[code]
cd HelperByOrc
MSBuild.exe HelperByOrc.vcxproj /t:Build "/p:Configuration=Release;Platform=Win32"
[/code]
Подставьте полный путь к MSBuild.exe из вашей установки VS (например Program Files\Microsoft Visual Studio\2022\Community\...\MSBuild.exe или ветка 18).

[b]Результат:[/b] HelperByOrc\Release\HelperByOrc.asi

[b]Модули:[/b] imgui_overlay, mod_app, binder_module, samp_api, samp_hooks, samp_rak_hooks, tags_module, hotkey_utils, text_encoding, app_config.

[b]Чат (биндер / samp_api):[/b] при загруженном Arizona [b]_chat.asi[/b] — поиск writer/submit в модуле, [b]UTF-8[/b] в поле; без него — [b]CP1251[/b] и [b]CDXUTEditBox[/b]. «Вставить в чат» — только текст в поле; «Открыть чат» — открыть чат и вставить. Фоновая подстановка может использовать тот же путь.

[b]UI:[/b] быстрое меню по умолчанию компактнее; таблица биндов и селекты читаемее; тема — тёмно-синий акцент.

[b]Быстрое меню (binder):[/b]
- Стиль 1: дерево ([b]Tree[/b])
- Стиль 2: каскад ([b]cascade[/b], [b]по умолчанию[/b])
- Хост меню через popup-стек ImGui, правки фокуса/z-order и sync мыши при открытии — стабильнее клики в SA:MP overlay

[b]Имя проекта:[/b] актуально HelperByOrc; старое имя MyAsiMod не используется.
