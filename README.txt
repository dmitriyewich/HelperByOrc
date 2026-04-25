[center][size=18][b]HelperByOrc[/b][/size][/center]
[center]ASI-плагин для SA:MP: бинды, команды, быстрые действия и удобное меню.[/center]

[b]Что это?[/b]
HelperByOrc — нативный ASI-плагин для GTA San Andreas / SA:MP. Он помогает быстро отправлять команды, фразы и действия по горячим клавишам.

[b]Что умеет[/b]
[LIST]
[*]Бинды на клавиши и комбинации.
[*]Быстрое меню биндов.
[*]Папки и подпапки для организации биндов.
[*]Ручная сортировка папок: Shift + перетаскивание папки.
[*]Вставка текста в чат и отправка команд через стандартный SA:MP-путь.
[*]Настройки интерфейса прямо в игре.
[*]Русская и английская локализация.
[/LIST]

[b]Что изменено в текущей версии[/b]
[LIST]
[*]Плагин больше не цепляет overlay слишком рано: D3D/ImGui слой включается только после полной готовности SA:MP.
[*]SampHooks и RakNet-хуки ставятся только после SA:MP full-ready, а не на загрузочном экране.
[*]Добавлена расширенная диагностика зависаний загрузки: состояние SA:MP, указатели, память, владельцы ранних JMP/CALL-патчей.
[*]Добавлена диагностика Compatibility Mode: __COMPAT_LAYER, AppCompat Layers, apphelp.dll, AcLayers.dll, AcGenral.dll.
[*]Если D3D9 Reset уходит через apphelp.dll, Reset-хук пропускается безопасно. Overlay продолжает работать через Present/EndScene.
[*]Профиль Release сделан обычным: без агрессивного LTCG и без отключения защит компилятора.
[*]Arizona/_chat.asi прямой путь временно отключён; используется стандартный SA:MP fallback для чата.
[/LIST]

[b]Установка[/b]
[LIST=1]
[*]Скопируйте HelperByOrc.asi в папку ASI/modloader вашего клиента.
[*]Запустите игру.
[*]Откройте меню плагина. Комбинация по умолчанию: Ctrl + Z.
[/LIST]

[b]Файлы[/b]
[LIST]
[*]HelperByOrc.asi — сам плагин.
[*]HelperByOrc.json — настройки и бинды.
[*]HelperByOrc.log — лог для диагностики.
[/LIST]

[b]Если игра зависает на загрузке[/b]
[LIST]
[*]Присылайте HelperByOrc.log целиком.
[*]Важные строки в логе: [i][diag][appcompat][/i], [i][ui][d3d][/i], [i][samp][diag][/i], [i][probe][stuck][/i].
[*]sampInfo=0 при refGame=1 — это ранний этап SA:MP до создания CNetGame. Это проблема только если состояние долго не меняется.
[*]Compatibility Mode / Disable fullscreen optimizations могут вмешиваться в D3D9 через apphelp.dll. Плагин это логирует.
[/LIST]

[b]Исходники[/b]
[LIST]
[*]GitHub: [url=https://github.com/dmitriyewich/HelperByOrc]github.com/dmitriyewich/HelperByOrc[/url]
[*]В репозитории лежат исходники, профиль сборки, workflow и vendored-библиотеки в HelperByOrc/external.
[*]Артефакты сборки, локальные временные файлы и служебные каталоги не публикуются.
[/LIST]

[right]HelperByOrc[/right]
