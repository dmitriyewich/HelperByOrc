# Context

## Назначение проекта
- `HelperByOrc` это нативный `ASI`-порт проекта `HelperByOrc` с Lua/MoonLoader на `C++` для `GTA San Andreas`.
- Старое имя `MyAsiMod` считать устаревшим; в активных путях, имени проекта и документации использовать только `HelperByOrc`.
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
- Временная диагностическая сборка: `HelperByOrc/feature_flags.h` задаёт `HELPERBYORC_ENABLE_ARIZONA_INTEGRATION=0` и `HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION=0`; Arizona-специфичные пути и прямой `_chat.asi` writer/submit отключены на этапе компиляции, чтобы изолировать ранний конфликт загрузки/курсора.
- Жизненный цикл вынесен из loader-lock пути: `DllMain` оставляет только лёгкий bootstrap (event + worker thread), а тяжёлая инициализация `ModApp::OnProcessAttach(...)` и штатный `ModApp::Shutdown()` выполняются в worker-потоке вне `DllMain`. Для ранней диагностики добавлены bootstrap-маркеры через `OutputDebugStringA` и `HelperByOrc.log` (`[bootstrap] ...`).
- Для диагностики стартовых зависаний включён gate input-pipeline: до полной инициализации SA:MP (`isSAMPInitilizeLua`) overlay откладывает установку `WndProc`, а cursor pipeline остаётся выключенным. После перехода gate `0 -> 1` `WndProc` ставится лениво.
- Установка `SampHooks` и `SampRakHooks` переведена на full-ready gate (`isSAMPInitilizeLua`): обычные и RakNet-хуки не ставятся на раннем loading screen.
- `SampApi::isSAMPInitilizeLua()` использует безопасный fallback-детектор готовности по нескольким признакам (не только `SAMP_INFO`): учитываются `SAMP_INFO`, `chat input`, `RefGame`, `dialog`; готовность считается при валидной комбинации сигналов, а в `lastError` выводится probe-маска (`sampInfo/chatInput/refGame/dialog`) для диагностики проблемных окружений.
- В runtime-лог добавлены probe-маркеры с таймштампами: `Present/EndScene` (`[ui][probe] ... ts=... init=... gate=...`), цикл refresh (`[probe] Refresh begin/end ... sampReady(beforeHooks/afterHooks)`), событие переключения gate (`[probe] SA:MP input gate changed ...`) и shutdown (`[probe] shutdown begin/end ...`).
- Для максимальной диагностики ранней загрузки `ModApp::OnProcessAttach` пишет в `HelperByOrc.log` fingerprint/mtime/FNV64 и PE-метаданные `gta_sa.exe`, `samp.dll`, `HelperByOrc.asi`, известных proxy-кандидатов, root/CLEO/MoonLoader/modloader/SAMPFUNCS inventory, а также snapshot уже загруженных модулей с тегами риска (`sampfuncs`, `moonloader`, `d3d9-proxy`, `input-proxy`, `chat-hook`, `overlay`, `crashfix` и т.п.). Пока SA:MP не готов, runtime дополнительно пишет snapshot новых модулей, чтобы поймать ранний конфликт загрузки.
- `SampApi::LogReadinessDiagnostics(...)` фиксирует PE-сведения текущего `samp.dll`, глобальные указатели (`sampInfo`, `chat`, `chatInput`, `dialog`, `refGame`) с `VirtualQuery`-описанием регионов памяти, ключевые поля `CNetGame`/pools/RakClient/chat/dialog и первые байты критичных SA:MP-функций. Для ранних `E9`/`E8`/`FF 25`/`push-ret`/`mov eax; jmp` патчей SA:MP-функций дополнительно декодируется transfer target с owner-модулем/RVA, чтобы в логе было видно, какая ASI/DLL уже перехватила `CInput_*`, `CDialog_*`, `AddEntry` и другие точки до наших хуков. Состояние `sampInfo=0` при `refGame=1` трактуется как ранний этап: SA:MP уже дошёл до GUI/game init, но `CNetGame` ещё не сконструирован; это не ошибка само по себе, если позже появляется ненулевой `sampInfo`.
- Если SA:MP остаётся до full-ready gate дольше ~8 секунд после детекта поддержанного `samp.dll`, `ModApp::Tick` пишет `[probe][stuck]` и повторный `LogReadinessDiagnostics("stuck")`; это предназначено для случаев, когда `chat/chatInput/dialog/CNetGame` не создаются и нужно определить ранний конфликт загрузки.
- Startup diagnostics логирует AppCompat Layers из `HKCU/HKLM\Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers` для путей игры, чтобы подтверждать или исключать Compatibility Mode, связанный с `apphelp.dll`. Дополнительное совпадение по строке `arizona` в тегах модулей отключено, пока `HELPERBYORC_ENABLE_ARIZONA_INTEGRATION=0`.
- AppCompat diagnostics усилены: отдельно проверяется точная registry-запись для текущего `gta_sa.exe`, логируется `__COMPAT_LAYER`, распознанные теги (`disable-fullscreen-optimizations`, `dwm8-16bit-mitigation`, `run-as-admin`, `win7-compat`, `ignore-free-library` и т.п.) и список загруженных shim DLL (`apphelp.dll`, `AcLayers.dll`, `AcGenral.dll`, `AcDwm.dll` и др.). Это нужно для случаев, когда Compatibility Mode приходит через launcher/environment, а не через явную `Layers`-запись текущего exe.
- D3D/overlay диагностика расширена: окно игры сначала ищется через глобальный GTA HWND `0x00C8CF88`, затем через `IDirect3DDevice9::GetCreationParameters`, и только потом через foreground-окно текущего процесса. Логируются тайминги `Direct3DCreate9` и dummy `CreateDevice`, resolved HWND/class/title/pid/tid, vtable snapshot, hook policy, а также module/RVA для vtable-целей `Reset`/`Present`/`EndScene`. Dummy device сначала создаётся через `D3DDEVTYPE_NULLREF + D3DCREATE_DISABLE_DRIVER_MANAGEMENT`, затем fallback на HAL. Если `IDirect3DDevice9::Reset` указывает в системный compatibility shim `apphelp.dll`, Reset-hook не ставится: overlay продолжает работать через `Present`/`EndScene`, а в лог пишется явное предупреждение.
- RakNet прямые вызовы через `RakClientInterface` закреплены на явных индексах vtable для SA:MP/RakNet: `Send(BitStream)` = `6`, `RPC(BitStream)` = `25`, чтобы не зависеть от компиляторской раскладки C++-интерфейса в нашем коде.
- **Фокус окна (ввод не уходит в плагин из чужих приложений):** хоткеи биндера, переключение главного меню по комбинации и перехват `WndProc` для ImGui / захвата хоткея выполняются только если окно GTA (или дочернее с фокусом ввода) на переднем плане — `ImGuiOverlay::IsGameWindowForeground()` (тот же критерий, что для `UpdateOverlayCursorMode`: `GetForegroundWindow` == `gameWindow` или `IsChild(gameWindow, …)`). Перед `BinderModule::Tick` выставляется `SetGameInputForeground(...)`; при потере фокуса — `ResetInputState`, без `SyncPressedKeysWithAsyncState` на `GetAsyncKeyState` (иначе клавиши, набранные в другом окне, ломали бинды). Для `UpdateHotkeyState` при возврате фокуса — ресинхрон `menuToggleWasDown_` без ложного переключения меню.
- **Папки в биндере (реализовано):** ручная сортировка папок и вложенности через DnD. **Shift + перетаскивание** — перенос **папки** (payload строки `BINDER_FOLDER_ID`, int id); **без Shift** — по-прежнему перенос **бинда** на папку (`BINDER_HOTKEY_INDEX`). DnD-лини (вставка между соседями, «в конец» списка, цель «внутрь папки» на `TreeNode`) рисуются **только** из **цикла родителя** (корневой список в `DrawFolderPane`, дочерний — в `DrawFolderTreeNode` при открытой ветке с детьми), **не** отдельным виджетом **до** `ImGui::TreeNodeEx` того же узла — иначе ломается стек ImGui (`TreePop` / `PopID`). Для `TreePop`: на ветвях вызывать, только если `TreeNodeEx` реально сделал `TreePush` (в коде: `needTreePop` = в `flags` **нет** `ImGuiTreeNodeFlags_NoTreePushOnOpen`; тогда `if (opened && needTreePop) ImGui::TreePop()`; у листьев `Leaf+NoTreePushOnOpen` при открытом состоянии `TreePop` **не** нужен). Пока **поиск папок** не пуст (после `Trim`) — DnD **папок** отключён; DnD биндов в папки остаётся. Строки UI/undo: `ui_settings` (например `ToastFolderMoved`, `UndoFolderMove`, `FolderDragDisabledWithSearch`). В `##binder_folders_tree` для плотного списка может использоваться `ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, …)`.
- Overlay / UI: поведение курсора и SA:MP cursor mode унифицированы через `mod_app` (`UpdateOverlayCursorMode`) и `imgui_overlay`; при скрытом UI каждый кадр вызывается лёгкий проход ImGui без отрисовки (`AdvanceImGuiFrameWithoutUi`), чтобы не залипали фокус/hover после быстрого меню; при показе overlay, если после `ImGui_ImplWin32_NewFrame` позиция мыши невалидна (или первый кадр после простоя), перед `ImGui::NewFrame` подставляется `GetCursorPos` + `ScreenToClient` — иначе Win32-backend даёт кадры без hover при по-прежнему перехваченных в `WndProc` кликах.
- Cursor ownership: в `UpdateOverlayCursorMode` действует единое условие удержания курсора `(ImGui UI) OR (SA:MP chat open) OR (SA:MP dialog active)` при фокусе окна игры; при активном hold добавлена периодическая reassert-установка `Set_CursorMode` (анти-рассинхрон, когда SA:MP/chat сбрасывает курсор между кадрами). ImGui software cursor отключён (`io.MouseDrawCursor = false`), чтобы в игре был один видимый курсор (игровой/SA:MP). Для runtime-диагностики в trace добавлены флаги `wantsUi/chatOpen/dialogOpen/chatOrDialog/shouldHold`; повторяющиеся `reassert`-логи ограничены по интервалу, чтобы не зашумлять `HelperByOrc.log`.
- Биндер — быстрое меню: два стиля — `Tree` (дерево) и каскад (`BeginMenu`, в конфиге `quick_menu_style: cascade`); по умолчанию каскад. Старые значения `style2` / `style3` в JSON читаются как каскад. Фильтрация папок/биндов и условий quick menu одинаковая для обоих стилей. Хост быстрого меню (и каскад, и дерево) заведён через `OpenPopup` / `BeginPopup` / `EndPopup` со стабильным id `##helperbyorc_qm_host` (вместо постоянного поднятия окна `BringWindowToDisplayFront` каждый кадр); при `quickMenuFocusPending` — `BringWindowToFocusFront` и одноразовый `syncOsMouseToImGui()` (`GetCursorPos` + `ScreenToClient` по `PlatformHandle`, затем `io.AddMousePosEvent`), чтобы сразу после открытия не было рассинхрона «клики ест WndProc, а `WantCaptureMouse`/hover ещё нет». Дублирующий `AddMousePosEvent` внутри `DrawQuickMenu` после `NewFrame` убран.
- `imgui_overlay`: для overlay отключены `ImGuiConfigFlags_NavEnableKeyboard` и захват клавиатуры навигацией (`io.ConfigNavCaptureKeyboard = false`), чтобы реже конфликтовать с SA:MP при всплывающих меню.
- `samp_api` / чат: в текущей диагностической сборке узкая интеграция с Arizona `_chat.asi` отключена compile-time. `EnsureChatAsiInputDiscovery()` пишет skip-лог один раз, `TrySetChatInputTextViaChatAsi()` и `TryProcessChatInputViaChatAsi()` возвращают `false`, поэтому `Set_ChatInputText` и `process_chat_input` используют стандартный путь SA:MP (`CDXUTEditBox::SetText` + `CInput::ProcessInput`). Fallback через `CDXUTEditBox::SetText` ожидает **CP1251**; из исходного UTF-8 готовится CP1251-форма.
- Биндер `DoSend`: «**Вставить в чат**» — только `Set_ChatInputText(..., openChat=false, ...)` без принудительного открытия/закрытия чата; «**Открыть чат**» — по-прежнему открывает чат и подставляет текст в поле ввода.
- UI: дефолтный размер окна быстрого меню уменьшен (порядка **214×277** логических единиц до масштаба); в главном окне усилен контраст таблиц/селектов и оформление строк таблицы биндов (выбор, hover, выравнивание имён); карточки секций (`DrawSectionCard`) опираются на цвет фона темы; актуальная палитра после серии правок — снова **тёмно-синий акцент** (эксперимент slate-blue откатили).
- `samp_api`: для `R1` / `R3` / `R3-1` смещение `SAMP_REMOTEPLAYERDATA_OFFSET == 0` трактуется как встроенный layout в `CRemotePlayer` (база = указатель на игрока), без ложного разыменования как «отдельный объект по адресу 0».
- `kthook` удалён из активного проекта и больше не используется в `HelperByOrc.vcxproj`.
- Release-профиль сборки нормализован из «агрессивного» в обычный диагностируемый профиль: отключены `/GL`/LTCG, убран `/GS-`, включены `/GS` и `/sdl`, сохранены frame pointers (`/Oy-`), включены RTTI и debug symbols. В `plugin-sdk` premake release-профиль также переведён на `linktimeoptimization "Off"` и `symbols "On"`.
- `BlastHackNet/SAMP-API` добавлен в рабочую область как vendored reference:
  - локальный путь: `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\external\SAMP-API`
  - upstream: `https://github.com/BlastHackNet/SAMP-API`
  - ветка в локальной копии: `multiver`
  - сейчас это reference/dependency для выборочного использования, но не обязательная часть активной сборки
- `Dear ImGui` в активной сборке vendored из official upstream:
  - локальный путь: `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\external\imgui`
  - upstream: `https://github.com/ocornut/imgui`
  - текущая синхронизированная версия: `1.92.7`
- `Font Awesome 7` иконки теперь доступны в активном `ImGui` UI:
  - vendored runtime-данные: `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\font_awesome7_data.h`
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
- `tags_module` для отдельного нативного движка переменных/тегов и его UI во вкладке `Прочее`.
- `text_encoding` для конвертации строк между внутренним `UTF-8` и игровой кодировкой.
- В активной сборке есть `MinHook`-обвязка:
  - `HelperByOrc/minhook_utils.h`
- Архивный `_thirdparty/RakLua` не является частью активной сборки `HelperByOrc.vcxproj` и может содержать старые ссылки на `kthook`.

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
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\HelperByOrc.vcxproj`
- Контейнер solution:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc.slnx`

## Публикация на GitHub
- Удалённый репозиторий: `https://github.com/dmitriyewich/HelperByOrc` (`origin`).
- На GitHub выкладываются **только** исходный код проекта, **профиль сборки** (`*.vcxproj`, `*.slnx`, связанные файлы проекта), workflow `.github/workflows/build-release-win32.yml` и **vendored** `HelperByOrc/external` (полное дерево файлов зависимостей для воспроизводимой сборки; в индексе корневого репозитория — без вложенных `.git` в подпапках `external`). В корне допускаются `README.md`, `README.txt`, `context.md` (документация) и корневой `.gitignore`.
- **Не** публиковать на GitHub: артефакты сборки (`*.asi`, `*.pdb`, каталоги `Release/`, `Debug/`, промежуточный `build/`), каталог `.cursor/`, локальные `refecence/`, `tmp_upx/`, архивы/распакованные DLL для анализа, служебные файлы `.codex/`, и прочий мусор по корневому `.gitignore`.

## Локальный Git
- Корень рабочей области `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie` инициализирован как локальный `git`-репозиторий с привязкой к `origin` (`https://github.com/dmitriyewich/HelperByOrc`).
- Любое обновление репозитория, включая `git commit`, `git push`, создание `PR` или другую публикацию изменений, выполнять только после явной команды пользователя `Зафиксируй изменения`.
- До команды `Зафиксируй изменения` допустимы только локальные правки, просмотр diff/status, сборка и другие непубликующие проверки.
- Базовый сценарий для локальной работы:
  - `git status`
  - `git diff`
  - `git diff -- HelperByOrc/binder_module.cpp`
- Для новых крупных точек синхронизации допустимо делать отдельные локальные baseline-коммиты.
- Generated/runtime-мусор не должен попадать в индекс:
  - `.claude/`
  - `.codex/`
  - временные `_tmp*`-директории
  - `.vs/`
  - build output (`HelperByOrc/build/`, `HelperByOrc/Release/`, `HelperByOrc/Debug/`, `HelperByOrc/external/plugin-sdk/output/`)
  - runtime-файлы `HelperByOrc.json` и `HelperByOrc.log`
- Каталоги `HelperByOrc/external/imgui`, `.../SAMP-API`, `.../plugin-sdk`, `.../memwrapper` на машине разработчика могли изначально быть отдельными `git`-клонами; для **публикации на `origin`** они хранятся как **обычный vendored-снимок без вложенного `.git`**, чтобы не было `gitlink/submodule` в корневом репозитории. Обновить версию vendored-библиотеки: снова клонировать нужный upstream в `external/<имя>` (или `git pull` до удаления `.git`) и после синхронизации снова удалить вложенный `.git` перед коммитом в корень, если политика репозитория — plain tree. Архивный `_thirdparty/RakLua` по-прежнему не часть активной сборки.

## Журнал действий Codex
- Для служебной памяти по проекту использовать папку:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\.codex`
- В `.codex` можно сохранять краткий локальный журнал действий, принятых допущений и следующих шагов, чтобы было видно, что уже делалось по задаче. По текущей политике репозитория `.codex` полностью остаётся локальным и не публикуется на GitHub.
- `context.md` и файлы внутри `.codex` хранить в нормальном `UTF-8`.

## Сборка
- На этой машине `MSBuild.exe` есть в `PATH` как команда `msbuild` (разрешается в `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe`). Для скриптов и окружений без Visual Studio Developer shell по-прежнему можно вызывать полный путь к `MSBuild.exe`.
- Рабочий сценарий сборки для задач по этому проекту:
  - только `build -> release`
- Под `release` в этом проекте считать именно:
  - `Release|Win32`
- Не предлагать и не использовать `Debug` как основной целевой артефакт без отдельной явной просьбы.
- Собирать из директории:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc`
- Команда сборки (эквивалентно вызову `msbuild` из `PATH` с теми же аргументами):

```powershell
cd C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc
msbuild 'C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\HelperByOrc.vcxproj' '/t:Build' '/p:Configuration=Release;Platform=Win32'
```

- Ожидаемый выходной файл:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\Release\HelperByOrc.asi`
- Бинарники и другие артефакты из `HelperByOrc\Release\` не копировать, не раскладывать по игровым папкам и не деплоить куда-либо автоматически.
- Любое копирование `.asi`, `.pdb`, `.lib` или других release-артефактов из `Release` выполнять только после отдельной явной команды пользователя.
- Промежуточные файлы активного проекта теперь ожидаемо живут в:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\build\Debug`
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\build\Release`
- Для локальной проверки `C++`-изменений обязательной считать `Release|Win32`.
- На момент обновления этого файла `Release|Win32` собирается успешно.
- GitHub Actions workflow `.github/workflows/build-release-win32.yml` собирает `Release|Win32`, предварительно проверяет наличие vendored-зависимостей (`plugin-sdk`, `imgui`, `MinHook`, `raknet`, `SAMP-API`, `memwrapper`) и отсутствие вложенных `.git` внутри `HelperByOrc/external`. Для GitHub-hosted runner workflow явно передаёт `PlatformToolset=v143`; локальный проект на этой машине остаётся на `v145`/VS18.

## Единый конфиг
- Единый runtime-конфиг проекта:
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\HelperByOrc.json`
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
  - `tags`: данные отдельного модуля переменных/тегов.
- Текущее ожидаемое содержимое секции `ui`:
  - `language`: `ru` или `en`.
  - `auto_scale`: `true/false`.
  - `scale_multiplier`: пользовательский множитель масштаба.
  - `log_level`: `off`, `error` или `info` (по умолчанию `info`).
  - `apply_damage_protection`: `true/false` (по умолчанию `true`); включает/выключает только действие `CDamageManager_ApplyDamage` detour по блокировке отваливания компонентов транспорта, сам хук остаётся активным.
  - `open_menu_hotkey`: массив virtual-key кодов для открытия главного окна, по умолчанию `Ctrl + Z`.
- Секция `binder` хранит состояние биндов, папок, хоткеев, quick menu и других binder-данных.
- Секция `tags` зарезервирована под нативный модуль переменных/тегов; текущая минимальная форма:
  - `custom_vars`: объект пользовательских переменных `name -> value`.
- Пример актуальной формы:

```json
{
  "schema_version": 1,
  "ui": {
    "language": "ru",
    "auto_scale": true,
    "scale_multiplier": 1.0,
    "log_level": "info",
    "apply_damage_protection": true,
    "open_menu_hotkey": [17, 90]
  },
  "binder": {
  },
  "tags": {
    "custom_vars": {}
  }
}
```

### Точки расширения для новых модулей
- Новый модуль должен резервировать себе отдельную секцию верхнего уровня, например `chat_helper`, `notepad`, `rpc_monitor`.
- Имена секций должны быть стабильными, короткими и отражать ответственность модуля.
- Модуль сам отвечает за свою внутреннюю JSON-структуру, но не должен менять чужие секции без явной причины.
- Если требуется миграция со старого отдельного конфига, допустим одноразовый fallback-read со старого файла с последующей записью в `HelperByOrc.json`.
- При добавлении новой секции нужно сразу обновлять этот `context.md`: перечислить секцию, её назначение и правило миграции, если она есть.

## Версии SA:MP и адреса памяти

### Канонические имена версий
- В пользовательской документации и UI использовать читаемые имена сборок:
  - `R1`, `R2`, `R3`, `R3-1`, `R4`, `R4-2`, `R5-1`, `DL-R1`
- Во внутреннем коде `SampApi::Version` используются идентификаторы `R1`, `R2`, `R3`, `R3_1`, `R4`, `R4_2`, `R5_1`, `DL_R1` (подчёркивание вместо дефиса там, где нельзя иначе).
- Для этого проекта считать подтверждённым, что:
  - `0.3.7 R5-1 == 0.3.7 R5-2` по адресам
  - `0.3.DL-R1 == 0.3.DL-R1-2` по адресам
- Старое имя колонки `R5_2` в истории проекта считать устаревшим; в активном коде канонический внутренний идентификатор — `R5_1` (соответствует пользовательскому `R5-1`).

### Матрица версий в таблицах смещений
- Каждый `SampApi::VersionedOffset` в `HelperByOrc/samp_api/core/samp_api_core.inl` задаётся **восемью** значениями в фиксированном порядке колонок:
  - `R1`, `R2`, `R3`, `R3-1`, `R4`, `R4-2`, `R5-1`, `DL-R1`
- Новый `VersionedOffset` или правка существующего должны сохранять эту же восьмёрку; нельзя молча заполнять только часть колонок и считать задачу закрытой.

### Обязательные версии (для полноты смещений)
- Для **`SampApi::main_offsets`**, RakNet-таблицы в **`samp_rak_hooks.cpp`** и любых новых versioned-полей нужно иметь осмысленное значение **для каждой поддерживаемой** сборки ниже (см. `entryPoint`). Нулевое значение допустимо только как осознанный sentinel (см. «Известные пробелы») или как явно задокументированный «пока неизвестно».

### Детект и формальная поддержка
- В **`SampApi::entryPoint`** перечислены известные entry point’ы `samp.dll`; флаг **`supported`**:
  - `E` — детектируется, но **не** считается целевой клиентской сборкой для фич плагина (`supported: false`)
  - `R1`, `R2`, `R3`, `R3-1`, `R4`, `R4-2`, `R5-1`, `DL-R1` — **поддерживаемые** (`supported: true`) и участвуют в матрице смещений

### BlastHack `SAMP-API`
- Локально добавленный `BlastHackNet/SAMP-API` (`HelperByOrc/external/SAMP-API`) используется как typed reference по классам `CRemotePlayer`, `CPlayerInfo`, `CPed` и т.д.
- Заголовки для разных веток (например `0.3.7-R1`, `0.3.7-R3-1`, `0.3.7-R5-1`, `0.3.DL-1`) удобно сверять при **offsetof** и проверке layout; таблицы `main_offsets` в проекте покрывают **полную восьмиколоночную** матрицу, а не только четыре классических канонических клиента.

### Правило по новым адресам
- Если добавляется новый `VersionedOffset`, он должен сразу содержать ячейки для всех восьми колонок в порядке из `samp_api_core.inl`.
- Если значение пока неизвестно — оставить `0` и **явно** зафиксировать это в документации и итоге задачи (как и раньше).
- Нельзя молча иметь значение только для одной версии при остальных нулях без пометки.

### Что уже покрыто
- `SampApi::VersionedOffset` хранит **восемь** полей: `R1`, `R2`, `R3`, `R3_1`, `R4`, `R4_2`, `R5_1`, `DL_R1`; метод **`Get(Version)`** выбирает колонку по детектированной версии.
- `SampApi::main_offsets` и RakNet-блок **`kRakOffsets`** в `samp_rak_hooks.cpp` следуют той же восьмиколоночной схеме (см. комментарий в `samp_api_core.inl`).
- `main_offsets.rakclient_interface` для **`R5-1`** со значением `0` не считать отсутствующим адресом:
  - это валидный **member-offset** (`RakClientInterface*` в начале соответствующего поля `CNetGame`), согласовано с референсами вроде `RakHook/offsets.cpp`
- Часть обязательных канонических имён из старого контекста (`R1`, `R3-1`, `R5-1`, `DL-R1`) по-прежнему разумна как **минимальный набор для регрессии**, но код и таблицы оффсетов официально шире — см. матрицу выше.

### Известные пробелы по адресам
- Далее — особые случаи полей `main_offsets`, где **`0` допустим как семантика**, а не только как «дыра»:
- `main_offsets.SAMP_SLOCALPLAYERID_OFFSET`:
  - для `DL-R1` в таблице `0`
  - это валидный **member-offset** в начале структуры пула; не считать пропуском только из‑за нуля
- `main_offsets.SAMP_INFO_OFFSET_Pools_Veh`:
  - для `R5-1` в таблице `0`
  - это валидный **member-offset** (транспортный пул в начале подструктуры); не считать пропуском только из‑за нуля
- `main_offsets.SAMP_REMOTEPLAYERDATA_OFFSET`:
  - для `R1`, `R3` и `R3-1` задано `0` как **встроенный layout** внутри `CRemotePlayer`, а не как отсутствие поля
  - код трактует `0` как «база = указатель на `CRemotePlayer`», без разыменования как «указатель на отдельный объект по смещению 0»
- `main_offsets.pSAMP_Actor`:
  - далеко не везде нули; для части версий заданы ненулевые смещения к `sampActor` относительно блока remote data; там, где в колонке `0`, доступ к актёру через этот путь может быть недоступен — полноценная actor-логика не закрыта для всей матрицы
- Таблицы намеренно содержат нули в отдельных колонках (например **`CInput_Close_fix`** там, где обходной фикс в рантайме не используется); это не всегда «пробел», а отсутствие применения.
- В `samp_rak_hooks.cpp` для **`kRakOffsets`** по текущему состоянию нулевых ячеек в восьми колонках нет (все четыре поля блока заполнены по всем поддерживаемым сборкам).

## Архитектура проекта
- `main.cpp`
  - минимальный `DllMain` с bootstrap (`event` + worker thread)
  - тяжёлый lifecycle (`OnProcessAttach`/`Shutdown`) выполняется в worker вне `DllMain`
- `imgui_overlay.*`
  - D3D9 hooks (`EndScene`/`Reset`) через `MinHook`
  - инициализация official vendored `ImGui 1.92.7`
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
  - `C:\Games\CODEX\MyAsiMod\MyAsiModReshenie\HelperByOrc\external\SAMP-API`
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
- На текущем состоянии после синхронизации `Dear ImGui` до official `1.92.7` и фикса ownership ввода:
  - `Release|Win32` собирается успешно
  - основной `ImGui`-overlay использует текущую архитектуру проекта, а не переносится из `SaFraps`
  - `SaFraps` полезен только как behavioural reference для cursor mode при открытом UI
- Финальная игровая проверка SA:MP hooks, диалогов, RPC, чата, кодировок и UI всё равно делается в рантайме игры.
