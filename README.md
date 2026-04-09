# HelperByOrc

Нативный `ASI`-плагин на `C++` для `GTA San Andreas` / `SA:MP`.

Проект является переносом логики `HelperByOrc` с `Lua` / `MoonLoader` на нативный `Win32`-модуль. Текущее активное имя проекта, solution и выходного плагина в репозитории: `HelperByOrc`.

## Текущее состояние

- Целевая платформа: `Win32` / `x86`
- Формат: `ASI`
- Основной проект: `HelperByOrc/HelperByOrc.vcxproj`
- Solution-контейнер: `HelperByOrc.slnx`
- Выходной файл: `HelperByOrc/Release/HelperByOrc.asi`
- Текущая локальная проверка: `Release|Win32` собирается успешно

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

Используется `MSBuild` из Visual Studio 2022. Из корня репозитория:

```powershell
cd HelperByOrc
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' 'HelperByOrc.vcxproj' '/t:Build' '/p:Configuration=Release;Platform=Win32'
```

После успешной сборки плагин будет лежать по пути:

```text
HelperByOrc/Release/HelperByOrc.asi
```

## Внешние зависимости

В рабочей области проект использует локальные reference / vendored-зависимости внутри `HelperByOrc/external/`, включая:

- `plugin-sdk`
- `imgui`
- `SAMP-API`
- `memwrapper`

Эти внешние каталоги используются локально для сборки и разработки, но не являются основной частью активного репозиторного baseline.

## Runtime-файлы

- `HelperByOrc.json` — единый конфиг
- `HelperByOrc.log` — лог
- `HelperByOrc.asi` — итоговый плагин

## Примечание по имени проекта

Старое локальное имя `MyAsiMod` считается устаревшим. В актуальном состоянии репозитория активные имена проекта и solution: `HelperByOrc`.
