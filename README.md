# HelperByOrc

Нативный **ASI-плагин** для **GTA San Andreas / SA:MP** (Win32/x86): биндер сценариев, быстрое меню, теги, общий блокнот, HUD и фильтр сообщений в одном внутриигровом интерфейсе.

Подробная документация: [HelperByOrc Wiki](https://github.com/dmitriyewich/HelperByOrc/wiki).

## Возможности

- Сценарии из текста, команд, задержек, уведомлений и действий с чатом или диалогами.
- Запуск биндов по горячим клавишам, командам, сообщениям и через быстрое меню.
- Категории, папки, поиск, ручная сортировка и два вида списка: `Проводник` и `Две панели`.
- Условия запуска и видимости по состоянию игры, SA:MP, чата, курсора и транспорта.
- Встроенные теги и пользовательские переменные для игровых данных, текста и вычислений.
- Ручной выбор цели для `{target...}`: `Q` включает курсор, а видимый игрок под ним выделяется белой контурной обводкой.
- Общие для всех профилей переменные из Lua через MoonLoader Host.
- Общий для всех профилей блокнот: встроенная инструкция, поиск внутри и по всем заметкам, разметка, изображения, импорт/экспорт и живые `.txt`.
- HUD-конструктор с HUD-блоками, слоями, группами, условиями и шаблонами.
- Фильтр сообщений по обычному тексту и регулярным выражениям.
- Неинтерактивные системные OSD-уведомления или запись в лог с независимыми группами и антифлудом.
- Профили с отдельными биндами, HUD, фильтрами и настройками; заметки остаются общими.
- Интеграция со стандартным SA:MP и дополнительными интерфейсами Arizona: `_chat.asi` и CEF-диалогами.

## Скриншоты

![HelperByOrc - Binder](https://raw.githubusercontent.com/wiki/dmitriyewich/HelperByOrc/screens/binder-list.png)

![HelperByOrc - фильтр сообщений](https://raw.githubusercontent.com/wiki/dmitriyewich/HelperByOrc/screens/unwanted.png)

## Требования

| Параметр | Значение |
|---|---|
| Игра | GTA San Andreas + SA:MP |
| Архитектура | Win32 / x86 |
| Конфигурация | `Release\|Win32` |
| Поддерживаемые `samp.dll` | R1, R2, R3, R3-1, R4, R4-2, R5-1, DL-R1 |

Неизвестные версии `samp.dll` обрабатываются безопасно: зависящие от адресов функции отключаются или используют доступный резервный путь.

## Установка

1. Получите `HelperByOrc.asi` из релиза или локальной сборки.
2. Поместите файл в папку игры, ASI Loader или modloader.
3. Запустите игру и откройте меню комбинацией `Ctrl + Z`.
4. Выберите профиль и настройте язык, масштаб, горячие клавиши и быстрое меню. Ручной выбор цели для `{targetid}` включён по умолчанию: нажмите `Q`, наведите курсор на игрока и нажмите ЛКМ. Функция включается и выключается, а её клавиша меняется в `Настройки -> Горячие клавиши`.

Конфиг активного профиля:

```text
GTA San Andreas User Files\HelperByOrc\profiles\<profile-id>\HelperByOrc.json
```

Общий Блокнот хранится в `GTA San Andreas User Files\HelperByOrc\notepad` и не меняется при переключении профиля.

При использовании `portablegta` базовая папка user files определяется самой игрой. Не заменяйте `HelperByOrc.asi`, пока игра запущена.

Lua-переменные размещаются в `GTA San Andreas User Files\HelperByOrc\vars` и подпапках;
поддерживаются UTF-8 и legacy Windows-1251, а новые providers нужно явно включить
в `Прочее -> Переменные -> Lua`.
Для их выполнения нужны MoonLoader и
`moonloader\HelperByOrcVarsHost.lua`; установить или обновить Host можно там же.
Физически используется один host-файл: MoonLoader повторно загружает его для
каждого включённого provider и создаёт отдельные states без копий providers и
сгенерированных файлов в `%TEMP%`.
Lua выполняется как доверенный код внутри процесса, а не sandbox.

Подробный локальный контракт: [переменные из Lua](HelperByOrc/CODE_VARIABLES.md).

## Документация

- [Первый запуск](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%93%D0%BB%D0%B0%D0%B2%D0%BD%D0%B0%D1%8F)
- [Биндер](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%91%D0%B8%D0%BD%D0%B4%D0%B5%D1%80)
- [Быстрое меню](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%91%D1%8B%D1%81%D1%82%D1%80%D0%BE%D0%B5-%D0%BC%D0%B5%D0%BD%D1%8E)
- [Теги и переменные](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%A2%D0%B5%D0%B3%D0%B8-%D0%B8-%D0%BF%D0%B5%D1%80%D0%B5%D0%BC%D0%B5%D0%BD%D0%BD%D1%8B%D0%B5)
- [Блокнот](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%91%D0%BB%D0%BE%D0%BA%D0%BD%D0%BE%D1%82)
- [HUD](https://github.com/dmitriyewich/HelperByOrc/wiki/HUD)
- [Игнорирование сообщений](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%98%D0%B3%D0%BD%D0%BE%D1%80%D0%B8%D1%80%D0%BE%D0%B2%D0%B0%D0%BD%D0%B8%D0%B5-%D1%81%D0%BE%D0%BE%D0%B1%D1%89%D0%B5%D0%BD%D0%B8%D0%B9)
- [Диагностика](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%94%D0%B8%D0%B0%D0%B3%D0%BD%D0%BE%D1%81%D1%82%D0%B8%D0%BA%D0%B0)

## Сборка

```powershell
MSBuild HelperByOrc.slnx /t:Build /p:Configuration=Release /p:Platform=Win32 /m
```

Результат: `HelperByOrc\Release\HelperByOrc.asi`. Подробнее: [Сборка](https://github.com/dmitriyewich/HelperByOrc/wiki/%D0%A1%D0%B1%D0%BE%D1%80%D0%BA%D0%B0).

## Автор

**[@dmitriyewich](https://github.com/dmitriyewich)** — [HelperByOrc](https://github.com/dmitriyewich/HelperByOrc)
