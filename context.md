# Context

## Назначение проекта
- `MyAsiMod` это нативный `ASI`-порт проекта `HelperByOrc` с Lua/MoonLoader на `C++` для `GTA San Andreas`.
- Runtime-имя плагина и пользовательских артефактов:
  - `HelperByOrc.asi`
  - `HelperByOrc.json`
  - `HelperByOrc.log`
- Смысл и поведение нужно переносить из исходного Lua-проекта, а не перепридумывать без необходимости.
- Базовый референс по логике:
  - `C:\Games\CODEX\HelperByOrc`

## Текущее состояние
- Активный проект собирается как `Win32` `ASI`-плагин.
- Активный backend для хуков: `MinHook`.
- `kthook` удалён из активного проекта и больше не используется в `MyAsiMod.vcxproj`.
- `BlastHackNet/SAMP-API` добавлен в рабочую область как vendored reference:
  - локальный путь: `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\external\SAMP-API`
  - upstream: `https://github.com/BlastHackNet/SAMP-API`
  - ветка в локальной копии: `multiver`
  - сейчас это reference/dependency для выборочного использования, но не обязательная часть активной сборки
- `Dear ImGui` в активной сборке vendored из official upstream:
  - локальный путь: `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\external\imgui`
  - upstream: `https://github.com/ocornut/imgui`
  - текущая синхронизированная версия: `1.92.7 WIP`
- `Font Awesome 7` иконки теперь доступны в активном `ImGui` UI:
  - vendored runtime-данные: `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\font_awesome7_data.h`
  - merge в основной `ImGui`-шрифт выполняется в `imgui_overlay.*`
  - Lua-референс по именам и base85-данным: `C:\Games\CODEX\HelperByOrc\lib\fAwesome7.lua`
- Основные рабочие модули:
  - `imgui_overlay` для D3D9/ImGui overlay.
- `hotkey_utils` для общей hotkey-логики (`normalize/match/capture/conflict basis`) между `imgui_overlay` и `binder_module`.
  - UI захвата комбинаций тоже должен быть общим: все настройки hotkey в интерфейсе открываются через единый popup из `hotkey_utils`, а не через отдельные inline/popup-реализации по модулям.
  - `mod_app` для UI-оболочки и жизненного цикла.
  - `samp_api` для нативного доступа к SA:MP.
  - `samp_hooks` для обычных SA:MP hooks.
  - `samp_rak_hooks` для RakNet hooks и RPC/packet interception.
  - `text_encoding` для конвертации строк между внутренним `UTF-8` и игровой кодировкой.
- В активной сборке есть `MinHook`-обвязка:
  - `MyAsiMod/minhook_utils.h`
- Архивный `_thirdparty/RakLua` не является частью активной сборки `MyAsiMod.vcxproj` и может содержать старые ссылки на `kthook`.

## Целевая среда
- Игра: `GTA San Andreas 1.0 US`
- Платформа: только `Win32`
- Формат: `ASI`, не `MoonLoader`
- Архитектура: только `x86`
- `x64`-конфигурации в проект не добавлять

## Рабочая область
- Корень:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie`
- Основной проект:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\MyAsiMod.vcxproj`
- Контейнер solution:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiModReshenie.slnx`

## Локальный Git
- Корень рабочей области `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie` теперь инициализирован как локальный `git`-репозиторий.
- Этот `git` используется локально для diff/history; наличие `remote` не обязательно.
- Базовый сценарий для локальной работы:
  - `git status`
  - `git diff`
  - `git diff -- MyAsiMod/binder_module.cpp`
- Для новых крупных точек синхронизации допустимо делать отдельные локальные baseline-коммиты.
- Generated/runtime-мусор не должен попадать в индекс:
  - `.claude/`
  - временные `_tmp*`-директории
  - `.vs/`
  - build output (`MyAsiMod/build/`, `MyAsiMod/Release/`, `MyAsiMod/Debug/`, `MyAsiMod/external/plugin-sdk/output/`)
  - runtime-файлы `HelperByOrc.json` и `HelperByOrc.log`
- Внутри рабочей области уже есть вложенные upstream-репозитории (`external/imgui`, `external/SAMP-API`, `external/plugin-sdk`, `external/memwrapper`, `_thirdparty/RakLua`, временные референсы). Корневой локальный `git` не должен индексировать их как `gitlink/submodule`; для корневого baseline они исключаются через `.gitignore`.

## Сборка
- Использовать `MSBuild` только по этому пути:
  - `C:\MicrosoftVisualStudio\Packages\MSBuild\Current\Bin\MSBuild.exe`
- Не рассчитывать на `msbuild` из `PATH`.
- Собирать из директории:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod`
- Команда сборки:

```powershell
cd C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod
& 'C:\MicrosoftVisualStudio\Packages\MSBuild\Current\Bin\MSBuild.exe' 'MyAsiMod.vcxproj' '/t:Build' '/p:Configuration=Release;Platform=Win32'
```

- Ожидаемый выходной файл:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\Release\HelperByOrc.asi`
- Промежуточные файлы активного проекта теперь ожидаемо живут в:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\build\Debug`
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\build\Release`
- Для локальной проверки `C++`-изменений обязательной считать `Release|Win32`.
- На момент обновления этого файла `Release|Win32` собирается успешно.

## Единый конфиг
- Единый runtime-конфиг проекта:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\HelperByOrc.json`
- Прямую запись в `.json` из отдельных модулей не делать.
- Любой модуль должен писать в единый конфиг только через очередь мутаций `AppConfig`, чтобы запись на диск шла последовательно и не было одновременной записи из разных источников.
- Для типового сценария модуль должен иметь собственную top-level секцию и обновлять её через `QueueSectionReplace(sectionName, value)`.
- Если модулю нужна более точечная модификация нескольких секций сразу, использовать `QueueMutation(...)`, но всё равно через `AppConfig`.
- Новые модули не должны плодить отдельные runtime-конфиги без отдельной причины и явного решения.

### Структура `HelperByOrc.json`
- Корень JSON-объекта:
  - `schema_version`: версия схемы общего конфига.
  - `ui`: настройки интерфейса и локализации.
  - `binder`: данные биндер-модуля.
- Текущее ожидаемое содержимое секции `ui`:
  - `language`: `ru` или `en`.
  - `auto_scale`: `true/false`.
  - `scale_multiplier`: пользовательский множитель масштаба.
  - `open_menu_hotkey`: массив virtual-key кодов для открытия главного окна, по умолчанию `Ctrl + Z`.
- Секция `binder` хранит состояние биндов, папок, хоткеев, quick menu и других binder-данных.
- Пример актуальной формы:

```json
{
  "schema_version": 1,
  "ui": {
    "language": "ru",
    "auto_scale": true,
    "scale_multiplier": 1.0,
    "open_menu_hotkey": [17, 90]
  },
  "binder": {
  }
}
```

### Точки расширения для новых модулей
- Новый модуль должен резервировать себе отдельную секцию верхнего уровня, например `chat_helper`, `notepad`, `rpc_monitor`.
- Имена секций должны быть стабильными, короткими и отражать ответственность модуля.
- Модуль сам отвечает за свою внутреннюю JSON-структуру, но не должен менять чужие секции без явной причины.
- Если требуется миграция со старого отдельного конфига, допустим одноразовый fallback-read со старого файла с последующей записью в `HelperByOrc.json`.
- При добавлении новой секции нужно сразу обновлять этот `CONTEXT.md`: перечислить секцию, её назначение и правило миграции, если она есть.

## Версии SA:MP и адреса памяти

### Обязательные версии
- Все versioned memory addresses и offsets нужно поддерживать для:
  - `R1`
  - `R3-1`
  - `R5-2`
  - `DL-R1`

### Детект и формальная поддержка
- В `SampApi::entryPoint` как поддерживаемые сейчас отмечены:
  - `R1`
  - `R3-1`
  - `R5-2`
  - `DL-R1`
- Версии `E`, `R2`, `R3`, `R4`, `R4-2` сейчас не считать целевыми.

### BlastHack `SAMP-API`
- Локально добавленный `BlastHackNet/SAMP-API` полезен как typed reference по SA:MP-классам, структурам и части offsets.
- Его multiver-ветка покрывает:
  - `R1`
  - `R3-1`
  - `R5-1`
  - `DL-R1`
- Для текущего проекта это означает:
  - `R1`, `R3-1`, `DL-R1` можно безопасно сверять с ним как с дополнительным референсом
  - `R5-2` в нём явно не представлен, поэтому нельзя автоматически считать `R5-1 == R5-2`
  - любой перенос адресов, layout-ов или классов из `R5-1` в `R5-2` нужно отдельно верифицировать

### Правило по новым адресам
- Если добавляется новый `VersionedOffset`, он должен сразу иметь значения для `R1`, `R3-1`, `R5-2`, `DL-R1`.
- Если какое-то значение пока неизвестно, это нужно:
  - оставить как `0`
  - отдельно явно зафиксировать в документации и в результате задачи
- Нельзя молча добавлять адрес только для одной версии и считать задачу завершённой.

### Что уже покрыто
- `SampApi::VersionedOffset` хранит именно четыре целевые колонки:
  - `R1`
  - `R3_1`
  - `R5_2`
  - `DL_R1`
- Основной набор `SampApi::main_offsets` в целом ориентирован именно на эти четыре версии.
- В `samp_rak_hooks.cpp` version-specific RakNet offsets (`handleRpc`, `stringWriteEncoder`, `stringReadDecoder`, `compressorPtr`) заполнены для всех четырёх целевых версий.
- `main_offsets.rakclient_interface` для `R5-2` со значением `0` не считать отсутствующим адресом:
  - это подтверждено сравнением с `C:\Games\CODEX\AsiPluginTemplate\RakHook\source\RakHook\offsets.cpp`
  - для `R5-2` это валидный `member-offset`, то есть `RakClientInterface*` читается прямо из начала `CNetGame`

### Известные пробелы по адресам
- Ниже перечислен полный текущий список полей `SampApi::main_offsets`, где для обязательных версий есть `0`.
- `main_offsets.SAMP_SLOCALPLAYERID_OFFSET`:
  - для `DL-R1` сейчас `0`
  - это влияет на `Local_ID()` и код, который от него зависит
- `main_offsets.SAMP_INFO_OFFSET_Pools_Veh`:
  - для `R5-2` сейчас `0`
  - vehicle-pool dependent логика для `R5-2` покрыта не полностью
- `main_offsets.SAMP_REMOTEPLAYERDATA_OFFSET`:
  - для `R1` и `R3-1` сейчас `0`
  - чтение remote-player data в этих версиях неполное
- `main_offsets.pSAMP_Actor`:
  - сейчас `0` для всех четырёх обязательных версий
  - actor-related доступ фактически не реализован
- Вне списка выше других `0` в обязательных колонках текущего `SampApi::main_offsets` сейчас нет.
- В `samp_rak_hooks.cpp` version-specific RakNet offsets для обязательных версий заполнены полностью; нулевых значений там сейчас нет.

## Архитектура проекта
- `main.cpp`
  - только минимальный `DllMain`
  - без тяжёлой логики
- `imgui_overlay.*`
  - D3D9 hooks (`EndScene`/`Reset`) через `MinHook`
  - инициализация official vendored `ImGui 1.92.7 WIP`
  - `WndProc`, hotkey, render
  - централизованный ownership ввода для `ImGui`
  - при активном `ImGui`-вводе переключает SA:MP cursor mode через `SampApi::Set_CursorMode`
- `hotkey_utils.*`
  - shared runtime для hotkey-комбинаций
  - нормализация, сравнение, capture и базовые conflict-helpers
  - используется как общая основа для hotkey открытия главного окна и hotkey-логики биндер-модуля
- `mod_app.*`
  - жизненный цикл мода
  - основной UI shell
- `samp_api.*`
  - доступ к памяти SA:MP
  - version-aware offsets
- `samp_hooks.*`
  - высокоуровневые SA:MP hooks
- `samp_rak_hooks.*`
  - RakNet hooks
  - отправка/приём RPC и packets
- `text_encoding.*`
  - единая точка конвертации строк
- `debug_log.*`
  - файловый лог

## Кодировки
- `context.md` и все новые текстовые файлы хранить в нормальном `UTF-8`.
- Не сохранять документацию в `CP1251`.
- Не смешивать ручные конвертации кодировок по месту.
- На границе с игрой, SA:MP, чатом, диалогами и сетевыми сообщениями использовать `text_encoding.*`.

## Основные референсы
- Исходный Lua-проект:
  - `C:\Games\CODEX\HelperByOrc\src`
- Lua-референс по SA:MP API и offsets:
  - `C:\Games\CODEX\HelperByOrc\src\HelperByOrc\samp.lua`
- Lua-референс по hooks:
  - `C:\Games\CODEX\HelperByOrc\src\HelperByOrc\my_hooks.lua`
- Контекст исходного Lua-проекта:
  - `C:\Games\CODEX\HelperByOrc\CONTEXT.md`
- Lua-референс по `Font Awesome 7`:
  - `C:\Games\CODEX\HelperByOrc\lib\fAwesome7.lua`
- Дополнительный typed reference по SA:MP:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\MyAsiMod\external\SAMP-API`
- Upstream этого reference:
  - `https://github.com/BlastHackNet/SAMP-API`
- Upstream текущего `Dear ImGui`:
  - `https://github.com/ocornut/imgui`
- Тема проекта:
  - `https://www.blast.hk/threads/251477/`

## Критические правила
- Проект только `x86 (Win32)`.
- `x64` и посторонние solution-platform entries считать мусором для этого проекта.
- Все пользовательские тексты интерфейса, toast/notification-сообщения, подписи кнопок, заголовки, подсказки и прочие user-facing строки должны быть доступны минимум на `ru` и `en`.
- Пользовательские строки хранить по ключам в отдельном модуле локализации внутри проекта, а не в конфиге.
- В текущем проекте единой точкой локализации считать `ui_settings.*`. При добавлении новых строк сначала добавлять ключ в этот модуль, потом использовать ключ в коде.
- Нельзя добавлять новые user-facing строки напрямую в `mod_app.cpp`, `binder_module.cpp` или другие runtime-модули мимо ключей локализации.
- Для компактных действий в `ImGui` UI можно использовать иконки `Font Awesome 7` вместо текстовых кнопок, если это не ухудшает читаемость.
- Если добавляются новые `Font Awesome 7` иконки, использовать уже vendored-интеграцию через `font_awesome7_data.h` и merge в `imgui_overlay.*`, а не тянуть новый внешний runtime-зависимый источник.
- Переключение языка должно оставаться доступным во вкладке `Настройки` главного окна.
- Настройки масштаба интерфейса должны оставаться в главном окне во вкладке `Настройки`; при добавлении нового ImGui UI его размеры и отступы нужно пропускать через общий механизм scale.
- Все сценарии настройки комбинаций клавиш в UI (`open_menu_hotkey`, hotkey бинда, quick menu, confirm/cancel keys и т.д.) должны использовать единый capture popup из `hotkey_utils`.
- Любая тяжёлая инициализация должна жить вне `DllMain`.
- Hooks должны ставиться и сниматься чисто.
- После shutdown нельзя оставлять подвешенные:
  - `WndProc`
  - `D3D` state
  - `MinHook` hooks
  - указатели на интерфейсы и игровые объекты
- Если `ImGui` владеет вводом:
  - курсор и mouse-look не должны одновременно управлять игрой
  - при захвате ввода нужно переводить SA:MP в cursor mode `LOCKCAMANDCONTROL`
  - при отпускании ввода нужно возвращать cursor mode `NONE`
- Overlay notifications без фокуса ввода можно рендерить без блокировки управления игрой.
- Любые игровые указатели нужно проверять перед использованием.
- При ошибке детекта версии или недоступности `samp.dll` код должен деградировать безопасно.
- Vendored-библиотеки в `external/` не трогать без отдельной причины.
- Новую логику переносить в существующие модули, а не раздувать `main.cpp`.
- Если переносится функция из Lua, сохранять максимально близкий смысл и поведение.
- Пользовательский UI не засорять временными debug-кнопками и тестовым мусором.

## Локальная проверка
- Для изменений в документации сборка не обязательна.
- Для изменений в `C++` обязательная проверка:
  - `Release|Win32`
- На текущем состоянии после синхронизации `Dear ImGui` до official `1.92.7 WIP` и фикса ownership ввода:
  - `Release|Win32` собирается успешно
  - основной `ImGui`-overlay использует текущую архитектуру проекта, а не переносится из `SaFraps`
  - `SaFraps` полезен только как behavioural reference для cursor mode при открытом UI
- Финальная игровая проверка SA:MP hooks, диалогов, RPC, чата, кодировок и UI всё равно делается в рантайме игры.
