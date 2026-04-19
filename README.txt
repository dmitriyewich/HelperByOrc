[b]HelperByOrc[/b] — нативный ASI-плагин на C++ для GTA San Andreas / SA:MP. Перенос логики HelperByOrc с Lua/MoonLoader на Win32-модуль.

[b]Исходники на GitHub:[/b] [url=https://github.com/dmitriyewich/HelperByOrc]https://github.com/dmitriyewich/HelperByOrc[/url]

[b]Что на GitHub[/b]: только исходный код и профиль сборки (vcxproj, slnx и связанные файлы проекта). Готовые .asi/.pdb и каталоги сборки в репозиторий не выкладываются; крупные vendored-библиотеки из external/ (imgui, plugin-sdk и т.д.) подставляются локально.

[b]Платформа:[/b] Win32 / x86, формат ASI.

[b]Сборка:[/b] Visual Studio + MSBuild, конфигурация Release | Win32. Пример:
[code]
cd HelperByOrc
MSBuild.exe HelperByOrc.vcxproj /t:Build /p:Configuration=Release;Platform=Win32
[/code]
Подставьте полный путь к MSBuild.exe из вашей установки VS (например Program Files\Microsoft Visual Studio\2022\Community\...\MSBuild.exe или ветка 18).

[b]Результат:[/b] HelperByOrc\Release\HelperByOrc.asi

[b]Модули:[/b] imgui_overlay, mod_app, binder_module, samp_api, samp_hooks, samp_rak_hooks, tags_module, hotkey_utils, text_encoding, app_config.

[b]Имя проекта:[/b] актуально HelperByOrc; старое имя MyAsiMod не используется.
