[b]HelperByOrc[/b] — нативный ASI-плагин на C++ для GTA San Andreas / SA:MP. Перенос логики HelperByOrc с Lua/MoonLoader на Win32-модуль.

[b]Исходники на GitHub:[/b] [url=https://github.com/dmitriyewich/HelperByOrc]https://github.com/dmitriyewich/HelperByOrc[/url]

[b]Что на GitHub[/b]: исходный код, профиль сборки (vcxproj, slnx и связанные файлы проекта) и рабочее дерево [b]HelperByOrc\external[/b] (vendored-зависимости для воспроизводимой сборки). Готовые .asi/.pdb и каталоги сборки в репозиторий не выкладываются.

[b]Платформа:[/b] Win32 / x86, формат ASI.

[b]Сборка:[/b] Visual Studio + MSBuild, конфигурация Release | Win32. Пример:
[code]
cd HelperByOrc
MSBuild.exe HelperByOrc.vcxproj /t:Build /p:Configuration=Release;Platform=Win32
[/code]
Подставьте полный путь к MSBuild.exe из вашей установки VS (например Program Files\Microsoft Visual Studio\2022\Community\...\MSBuild.exe или ветка 18).

[b]Результат:[/b] HelperByOrc\Release\HelperByOrc.asi

[b]Модули:[/b] imgui_overlay, mod_app, binder_module, samp_api, samp_hooks, samp_rak_hooks, tags_module, hotkey_utils, text_encoding, app_config.

[b]Быстрое меню (binder):[/b]
- Стиль 1: дерево
- Стиль 2: двухпанельный
- Стиль 3: каскадный BeginMenu ([b]по умолчанию[/b])

[b]Имя проекта:[/b] актуально HelperByOrc; старое имя MyAsiMod не используется.
