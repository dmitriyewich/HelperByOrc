# HelperByOrc

Нативный `ASI`-плагин для `GTA San Andreas` / `SA:MP`: бинды, команды, быстрое меню и удобная организация действий прямо в игре.

Проект переносит исходную Lua/MoonLoader-логику HelperByOrc в Win32 C++ модуль. Активные имя репозитория, solution, project и runtime-артефакта: `HelperByOrc`.

## Репозиторий

GitHub: [github.com/dmitriyewich/HelperByOrc](https://github.com/dmitriyewich/HelperByOrc)

В репозитории намеренно хранятся:

- исходный код проекта;
- файлы сборки: `HelperByOrc.slnx`, `HelperByOrc/HelperByOrc.vcxproj`, workflow-файлы;
- vendored-зависимости в `HelperByOrc/external`;
- публичная документация `README.md`.

В репозиторий намеренно не добавляются локальные runtime/build-артефакты:

- `HelperByOrc/Release`, `HelperByOrc/build`, `HelperByOrc/Debug`;
- результаты сборки `HelperByOrc.asi`, `.pdb`, `.lib`;
- локальные служебные папки вроде `.cursor`, `.codex`;
- локальные документы и конфиги `README.txt`, `context.md`, `AGENTS.md`, корневой `.gitignore`;
- reference dumps, временные распакованные DLL и локальные архивы.

Vendored-библиотеки лежат обычными файлами, без вложенных Git-репозиториев и submodule. Текущее дерево `HelperByOrc/external` включает `plugin-sdk`, `imgui`, `MinHook`, `raknet`, `SAMP-API` и `memwrapper`.

## Возможности

- Бинды на клавиши и комбинации.
- Быстрое меню биндов.
- Папки и подпапки для организации действий.
- Перетаскивание папок с видимыми зонами вставки: `Shift + drag`, верх/центр/низ строки означают вставку перед папкой, внутрь папки или после папки.
- Перенос папки на пустую область корня: папка попадает в конец списка.
- Единые условия запуска/показа для биндов, быстрого меню и папок.
- Условия активного курсора SA:MP и активного Windows-курсора.
- Вставка текста в чат и отправка команд через SA:MP-чат, с Arizona `_chat.asi` direct path и fallback на стандартный SA:MP-путь.
- Настройки интерфейса прямо в игре.
- Русская и английская локализация.

## Текущее состояние

- Цель сборки: `Win32` / `x86`.
- Формат: `ASI`.
- Основной проект: `HelperByOrc/HelperByOrc.vcxproj`.
- Solution: `HelperByOrc.slnx`.
- Выходной файл: `HelperByOrc/Release/HelperByOrc.asi`.
- Активный backend хуков: `MinHook`.
- Release-профиль оставлен нормальным и пригодным для диагностики: без LTCG, без `/GS-`, без omit frame pointers, с включёнными символами, SDL и GS.
- Arizona `_chat.asi` direct integration включён через `HelperByOrc/feature_flags.h`; если discovery `_chat.asi` не срабатывает, плагин использует стандартный SA:MP fallback для чата.

## Перетаскивание Папок

Папки переносятся в режиме `Shift + drag`. Порядок зажатия больше не важен: можно сначала зажать `Shift`, затем ЛКМ, либо начать удерживать ЛКМ и добавить `Shift` до начала движения.

Во время перетаскивания появляются реальные зоны вставки:

- верхняя часть строки папки - вставить перед этой папкой;
- центральная часть строки - перенести внутрь этой папки;
- нижняя часть строки - вставить после этой папки;
- пустая область корня - перенести папку в конец корневого списка.

Превью цели рисуется вручную: линия для вставки перед/после и рамка для переноса внутрь. Применение переноса выполняется при отпускании ЛКМ по выбранной цели, поэтому логика не зависит от нестабильного `ImGui::AcceptDragDropPayload(...).IsDelivery()` на маленьких drop-зонах.

Защиты:

- нельзя перенести папку саму в себя;
- нельзя перенести папку в собственного потомка;
- нельзя перенести защищённую корневую папку внутрь другой папки;
- нельзя создать дубль имени в целевом списке;
- no-op цели не подсвечиваются и не применяются.

## Условия

Условия биндов и быстрого меню приведены к единой модели `conditions`. Для старых конфигов сохранён fallback чтения legacy `quick_conditions`.

Поддерживаются условия, связанные с курсором:

- SA:MP cursor active;
- Windows cursor active.

Для папок доступны собственные условия отображения. Быстрое меню учитывает enabled-состояние и условия папок/биндов при построении списка действий.

## Надёжность И Диагностика

- Тяжёлая инициализация и shutdown вынесены из `DllMain` в bootstrap worker thread.
- SA:MP hooks и RakNet hooks ставятся только после SA:MP full-ready.
- D3D overlay подключается после SA:MP full-ready, чтобы не конфликтовать с курсором и вводом на загрузочном экране.
- AppCompat-диагностика логирует Layer checks для текущего exe, `__COMPAT_LAYER`, известные compatibility tags и Windows shim modules вроде `apphelp.dll`, `AcLayers.dll`, `AcGenral.dll`.
- D3D9-диагностика логирует создание dummy device, vtable targets, target module/RVA для `Reset`, `Present`, `EndScene` и итоговую hook policy.
- Если `IDirect3DDevice9::Reset` указывает в `apphelp.dll`, Reset hook намеренно пропускается. Overlay остаётся активным через `Present` / `EndScene`.
- Runtime-лог включает SA:MP readiness probes, pointer regions, transfer-owner modules для уже пропатченных SA:MP-функций и `[probe][stuck]` diagnostics, если full-ready не достигается слишком долго.

## Основные Модули

- `mod_app` - lifecycle, top-level UI, SA:MP readiness gate и cursor ownership.
- `imgui_overlay` - D3D9 hooks, ImGui initialization, WndProc routing.
- `samp_api` - безопасный доступ к SA:MP memory и readiness diagnostics.
- `samp_hooks` - обычные SA:MP hooks.
- `samp_rak_hooks` - RakNet hooks и RPC/packet interception.
- `binder_module` - command binder, папки, условия и связанный UI.
- `tags_module` - variables/tags engine.
- `hotkey_utils` - общий hotkey capture и matching.
- `text_encoding` - UTF-8/game encoding conversion.

## Сборка

Используйте MSBuild из Visual Studio.

Локальная сборка:

```powershell
MSBuild HelperByOrc.slnx /p:Configuration=Release /p:Platform=Win32 /m
```

GitHub Actions build:

- workflow: `.github/workflows/build-release-win32.yml`;
- собирает `Release|Win32`;
- проверяет vendored-зависимости перед сборкой;
- загружает `HelperByOrc.asi` и `HelperByOrc.pdb` как workflow/release artifacts.

Проект локально нацелен на Visual Studio toolset `v145`. GitHub workflow переопределяет `PlatformToolset=v143` для hosted Windows runners.

## Runtime-Файлы

- `HelperByOrc.asi` - плагин.
- `HelperByOrc.json` - настройки пользователя и конфиг биндов.
- `HelperByOrc.log` - диагностический лог.

## Диагностика Проблем

- `sampInfo=0` при `refGame=1` - раннее состояние SA:MP: GUI/game initialization уже достигнут, но `CNetGame` ещё не создан. Это не самостоятельная ошибка, если позже `sampInfo` становится non-null.
- Compatibility Mode может направлять D3D9 `Reset` через `apphelp.dll`. Плагин определяет это и пропускает только небезопасный Reset hook.
- Если SA:MP не доходит до full-ready, проверьте `[samp][diag]`, `[diag][appcompat]`, `[ui][d3d]`, loaded modules и transfer-owner строки в `HelperByOrc.log`.
