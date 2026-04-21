# HelperByOrc

Нативный `ASI`-плагин на `C++` для `GTA San Andreas` / `SA:MP`.

Проект является переносом логики `HelperByOrc` с `Lua` / `MoonLoader` на нативный `Win32`-модуль. Текущее активное имя проекта, solution и выходного плагина в репозитории: `HelperByOrc`.

## GitHub

Публичный репозиторий: [github.com/dmitriyewich/HelperByOrc](https://github.com/dmitriyewich/HelperByOrc).

В удалённый репозиторий попадают **исходный код проекта**, **профиль сборки** (`HelperByOrc.vcxproj`, `HelperByOrc.slnx`, связанные файлы проекта) и **vendored-дерево** `HelperByOrc/external` (зависимости для воспроизводимой сборки на чистой машине). Снимки в `external/` хранятся как обычные файлы (без вложенных репозиториев).

Служебные вещи для локальной разработки (**корневой `.gitignore`**, **`context.md`**, **`.cursor/`** и т.п.) в удалённый репозиторий **не выкладываются**. Удобный корневой `.gitignore` держите у себя локально.

## Текущее состояние

- Целевая платформа: `Win32` / `x86`
- Формат: `ASI`
- Основной проект: `HelperByOrc/HelperByOrc.vcxproj`
- Solution-контейнер: `HelperByOrc.slnx`
- Выходной файл после сборки: `HelperByOrc/Release/HelperByOrc.asi`
- Проверка: конфигурация `Release|Win32` собирается успешно (локально)

### Недавние направления разработки

- Чат / биндер: при Arizona `_chat.asi` — runtime-поиск writer/submit; UTF-8 в ImGui-поле, ванильный SA:MP — CP1251 через `CDXUTEditBox`; из одного UTF-8 готовятся обе формы. «**Вставить в чат**» только подставляет текст; «**Открыть чат**» открывает чат и вставляет. `process_chat_input` при `_chat.asi` может писать в буфер и вызывать submit (эмуляция Enter) с fallback на стандартный путь.
- UI: компактнее окно быстрого меню по умолчанию; читаемее таблица биндов и контраст селектов (тёмно-синий акцент темы после отката эксперимента slate-blue).
- Overlay / ImGui: устойчивый ввод и курсор; лёгкий кадр без отрисовки при скрытом UI; при необходимости синхронизация позиции мыши OS -> ImGui до `NewFrame`.
- Курсорная логика: введён единый ownership для UI/чата/диалогов в `mod_app` (`UpdateOverlayCursorMode`) с общим условием удержания курсора `(ImGui UI) OR (SA:MP chat open) OR (SA:MP dialog active)` при фокусе окна игры.
- Для защиты от внешнего сброса режима (например после закрытия чата) добавлена периодическая reassert-установка `Set_CursorMode` при активном hold, чтобы не было провалов курсора и центрирования в переходах.
- Визуальный дубль убран: ImGui software cursor отключён (`io.MouseDrawCursor=false`), видимый курсор теперь один — игровой/SA:MP.
- В логах добавлены компактные диагностические флаги источников cursor-hold: `wantsUi`, `chatOpen`, `dialogOpen`, `chatOrDialog`, `shouldHold`.
- Спам reassert-логов в `HelperByOrc.log` ограничен по частоте (throttle), чтобы при долгих UI-сессиях журнал оставался читаемым.
- Уровень логирования UI по умолчанию: `Off` (включается вручную в `Настройки -> Уровень логирования` при необходимости диагностики).
- Быстрое меню биндера (дерево и каскад): хост через `OpenPopup` / `BeginPopup` со стабильным id, без агрессивного `BringWindowToDisplayFront` каждый кадр; при открытии — доводка фокуса и одноразовый sync координат мыши, чтобы не было «клик съели, а ImGui не в hover».
- В overlay для тестов отключена клавиатурная навигация ImGui (`NavEnableKeyboard` / захват клавиатуры), чтобы меньше конфликтовать с SA:MP.
- Единая фильтрация по quick-menu флагам и условиям для обоих стилей; по умолчанию каскад.
- `samp_api`: корректная работа с `SAMP_REMOTEPLAYERDATA` на старых layout (`R1`, `R3`, `R3-1`).

## Основные модули

- `imgui_overlay` — D3D9 / ImGui overlay
- `mod_app` — жизненный цикл и оболочка UI
- `binder_module` — биндер и связанный UI
- `samp_api` — доступ к памяти и структурам SA:MP
- `samp_hooks` — обычные SA:MP hooks
- `samp_rak_hooks` — RakNet hooks и перехват RPC / packet
- `tags_module` — движок переменных и тегов
- `hotkey_utils` — общая логика hotkey и popup-захвата
- `text_encoding` — конвертация строк между `UTF-8` и игровой кодировкой
- `app_config` — единый runtime-конфиг `HelperByOrc.json`

## Сборка

Используйте актуальный `MSBuild` из Visual Studio, например:

```text
C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
```

или для VS **18**:

```text
C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe
```

Из каталога репозитория:

```powershell
cd HelperByOrc
& '<путь-к-msbuild>\MSBuild.exe' 'HelperByOrc.vcxproj' '/t:Build' '/p:Configuration=Release;Platform=Win32'
```

После успешной сборки плагин:

```text
HelperByOrc/Release/HelperByOrc.asi
```

## Внешние зависимости

Проект использует vendored-зависимости внутри `HelperByOrc/external/`, включая:

- `plugin-sdk`
- `imgui`
- `SAMP-API`
- `memwrapper`

`external/` поддерживается как часть репозитория для упрощения воспроизводимой сборки на чистой машине.

## Вспомогательные скрипты

- `HelperByOrc/tools/generate_binder_test_config.py` — генерация объёмного тестового `HelperByOrc.json` для проверки биндера (локально; путь `OUT` внутри скрипта можно перенаправить под ваш modloader).

## Runtime-файлы

- `HelperByOrc.json` — единый конфиг
- `HelperByOrc.log` — лог
- `HelperByOrc.asi` — итоговый плагин

## Примечание по имени проекта

Старое локальное имя `MyAsiMod` считается устаревшим. В актуальном состоянии репозитория активные имена проекта и solution: `HelperByOrc`.
