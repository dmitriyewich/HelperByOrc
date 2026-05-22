# HelperByOrc

Нативный **ASI-плагин** для **GTA San Andreas / SA:MP** (Win32/x86): бинды, команды, быстрые действия, папки, условия, профильный блокнот и удобное ImGui-меню прямо в игре.

Проект переносит исходную Lua/MoonLoader-логику `HelperByOrc` в C++ ASI-модуль. Полная документация и инструкции вынесены в wiki: [HelperByOrc Wiki](https://github.com/dmitriyewich/HelperByOrc/wiki).

---

## Возможности

- Бинды на клавиши и комбинации.
- Быстрое меню биндов в каскадном или древовидном стиле.
- Категории Binder: несколько верхних разделов с табами, обязательной категорией `Основные` и переносом bind/папок между категориями.
- Единый Binder-список внутри каждой категории в стиле компактного проводника: иконка, название, серый запуск в скобках и действия справа.
- Root `Биндер`: bind без папки отображается прямо в корне, без отдельной секции `Вне папок`.
- Ручная сортировка папок и bind-элементов drag-and-drop с явным preview: перед элементом, внутрь папки, после элемента или в конец текущей папки.
- Быстрая навигация: назад, вверх, breadcrumb, глобальный поиск с плоскими результатами и путём, клавиши `Up`/`Down`, `Enter`, `Backspace`, `Delete`, `F2`, `Esc`.
- Inline-создание и inline-переименование папок прямо в списке.
- Quick menu показывает видимые категории табами, а внутри выбранной категории повторяет ручной порядок Binder-списка.
- Единая модель блокировок для запуска биндов, показа в быстром меню и отображения папок: отмеченное условие блокирует действие, если стало активным.
- Условия активного курсора SA:MP и активного Windows-курсора.
- Вставка текста в чат и отправка команд через SA:MP-чат.
- Arizona `_chat.asi` direct path с fallback на стандартный SA:MP-путь.
- Ввод параметров перед запуском бинда и подстановка `{{placeholders}}`.
- Теги и пользовательские переменные, включая `{thiscategory}` / `[thiscategory]` для категории текущего bind.
- Профильный блокнот: папки, поиск, избранное, split preview, Lua-compatible разметка, локальные картинки, импорт/экспорт `.txt`.
- Профили конфигурации: переключение, создание, дублирование, переименование и удаление во вкладке «Настройки».
- Настройки с верхними разделами: общее, быстрое меню, уведомления, профили, горячие клавиши и диагностика.
- Игровые исправления во вкладке «Прочее», включая защиту деталей транспорта.
- Русская и английская локализация.
- Расширенная диагностика SA:MP readiness, D3D9 overlay, AppCompat и ранних конфликтов хуков.

Подробнее по разделам:

- [Главная](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%93%D0%BB%D0%B0%D0%B2%D0%BD%D0%B0%D1%8F)
- [Биндер](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%91%D0%B8%D0%BD%D0%B4%D0%B5%D1%80)
- [Быстрое меню](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%91%D1%8B%D1%81%D1%82%D1%80%D0%BE%D0%B5-%D0%BC%D0%B5%D0%BD%D1%8E)
- [Блокнот](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%91%D0%BB%D0%BE%D0%BA%D0%BD%D0%BE%D1%82)
- [Профили](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%9F%D1%80%D0%BE%D1%84%D0%B8%D0%BB%D0%B8)
- [Теги и переменные](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%A2%D0%B5%D0%B3%D0%B8-%D0%B8-%D0%BF%D0%B5%D1%80%D0%B5%D0%BC%D0%B5%D0%BD%D0%BD%D1%8B%D0%B5)
- [Настройки](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%9D%D0%B0%D1%81%D1%82%D1%80%D0%BE%D0%B9%D0%BA%D0%B8)
- [Диагностика](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%94%D0%B8%D0%B0%D0%B3%D0%BD%D0%BE%D1%81%D1%82%D0%B8%D0%BA%D0%B0)
- [Сборка](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%A1%D0%B1%D0%BE%D1%80%D0%BA%D0%B0)

---

## Требования и ограничения

| Параметр | Значение |
|----------|----------|
| Игра | GTA San Andreas + SA:MP |
| Архитектура | **Win32 (x86)** |
| Сборка | **Release\|Win32** |
| SA:MP | Поддерживаемые `samp.dll`: **R1, R2, R3, R3-1, R4, R4-2, R5-1, DL-R1** |
| Ограничение | Для неизвестных `samp.dll` SA:MP-часть деградирует в fallback-режим |
| Ограничение | Не заменяйте `HelperByOrc.asi` во время работы игры |
| Лог | `HelperByOrc.log`, уровень: `Off`, `Error`, `Info` |

---

## Установка

1. Получите `HelperByOrc.asi` из релиза или локальной сборки.
2. Поместите `HelperByOrc.asi` в папку игры, ASI loader или modloader.
3. Запустите игру.
4. Откройте меню плагина. Комбинация по умолчанию: `Ctrl + Z`.
5. Настройте язык, масштаб, хоткеи, быстрое меню и профиль во вкладке «Настройки». Уровень лога и пути находятся в разделе «Диагностика».

Профили и конфиг хранятся в:

```text
GTA San Andreas User Files\HelperByOrc\profiles\<profile-id>\HelperByOrc.json
```

Картинки блокнота лежат рядом с конфигом профиля:

```text
GTA San Andreas User Files\HelperByOrc\profiles\<profile-id>\notepad\images
```

Если используется `portablegta`, путь к `GTA San Andreas User Files` берётся из игрового portable userfiles-getter.

---

## Сборка из исходников

```powershell
MSBuild HelperByOrc.slnx /t:Build /p:Configuration=Release /p:Platform=Win32 /m
```

Выходной файл:

```text
HelperByOrc\Release\HelperByOrc.asi
```

Локальный проект использует Visual Studio toolset `v145`. GitHub Actions переопределяет `PlatformToolset=v143` для hosted Windows runners.

---

## Диагностика

Главный файл для разбора проблем: `HelperByOrc.log`.

В первую очередь смотрите:

- `[bootstrap]` - ранний старт и shutdown;
- `[probe]`, `[probe][stuck]`, `[samp][diag]` - готовность SA:MP;
- `[ui][d3d]` - D3D9 hook policy и overlay;
- `[diag][appcompat]` - Compatibility Mode, `apphelp.dll`, `AcLayers.dll`;
- transfer-owner строки - кто уже пропатчил SA:MP-функции до плагина.

Подробно: [Диагностика](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%94%D0%B8%D0%B0%D0%B3%D0%BD%D0%BE%D1%81%D1%82%D0%B8%D0%BA%D0%B0).

---

## Автор

**[@dmitriyewich](https://github.com/dmitriyewich)** - [HelperByOrc](https://github.com/dmitriyewich/HelperByOrc)
