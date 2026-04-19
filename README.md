# HelperByOrc

Нативный `ASI`-плагин на `C++` для `GTA San Andreas` / `SA:MP`.

Проект является переносом логики `HelperByOrc` с `Lua` / `MoonLoader` на нативный `Win32`-модуль. Текущее активное имя проекта, solution и выходного плагина в репозитории: `HelperByOrc`.

## GitHub

Публичный репозиторий: [github.com/dmitriyewich/HelperByOrc](https://github.com/dmitriyewich/HelperByOrc).

В удалённый репозиторий попадают **исходный код**, **профиль сборки** (`HelperByOrc.vcxproj`, `HelperByOrc.slnx`, связанные файлы проекта) и рабочее дерево `HelperByOrc/external` (vendored-зависимости, нужные для воспроизводимой сборки).

В репозитории **нет** корневого `.gitignore` и файла `context.md`: они используются только локально при разработке. Чтобы не засорять индекс, держите у себя корневой `.gitignore` или добавляйте в git только нужные пути.

## Текущее состояние

- Целевая платформа: `Win32` / `x86`
- Формат: `ASI`
- Основной проект: `HelperByOrc/HelperByOrc.vcxproj`
- Solution-контейнер: `HelperByOrc.slnx`
- Выходной файл после сборки: `HelperByOrc/Release/HelperByOrc.asi`
- Проверка: конфигурация `Release|Win32` собирается успешно (локально)

### Недавние направления разработки

- Overlay / ImGui: устойчивый ввод и курсор при быстром меню и при переходе из «простоя» UI.
- Быстрое меню биндера:
  - Стиль 1: дерево (`Tree`)
  - Стиль 2: двухпанельный
  - Стиль 3: каскадный `BeginMenu` (теперь стиль по умолчанию)
  - единая фильтрация по quick-menu флагам и условиям для всех стилей
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

- `HelperByOrc/tools/generate_binder_test_config.py` — генерация объёмного тестового `HelperByOrc.json` для проверки биндера (локально).

## Runtime-файлы

- `HelperByOrc.json` — единый конфиг
- `HelperByOrc.log` — лог
- `HelperByOrc.asi` — итоговый плагин

## Примечание по имени проекта

Старое локальное имя `MyAsiMod` считается устаревшим. В актуальном состоянии репозитория активные имена проекта и solution: `HelperByOrc`.
