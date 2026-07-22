# Переменные из Lua

HelperByOrc поддерживает общие для всех профилей переменные из Lua. Providers
выполняются только в настоящих Lua states MoonLoader через
`HelperByOrcVarsHost.lua` и имеют доступ к его API. Обычные строковые переменные
из `tags.custom_vars` остаются профильными и не зависят от MoonLoader.

Lua имеет доступ к процессу игры. Это доверенный код, а не sandbox.

## Установка и загрузка

Lua-файлы размещаются в общей папке:

```text
GTA San Andreas User Files\HelperByOrc\vars\*.lua
```

Подпапки разрешены. Сканируются только `.lua`; лежащий в этой же папке
`transliteration.txt` не участвует в загрузке providers. Providers сортируются по
относительному пути без учёта ASCII-регистра и по умолчанию выключены.
Исходники могут быть в UTF-8 или legacy Windows-1251. Невалидные для UTF-8
метаданные и строковые результаты приводятся из Windows-1251 в UTF-8; у текста
из другой однобайтовой кодировки возможен mojibake, но provider не отбрасывается.
Включение, reload, открытие папки и состояние backend находятся в
`Прочее -> Переменные -> Lua`. Флаги включения хранятся в `profiles.json`.
Переключение профиля не перезапускает общие Lua providers.

Для Lua providers обязательны MoonLoader и совместимый Host. Нажмите
`Установить Host` или `Обновить Host`; HelperByOrc атомарно установит встроенный
скрипт в:

```text
<папка игры>\moonloader\HelperByOrcVarsHost.lua
```

При замене отличающегося host рядом создаётся `.bak`. Host должен пройти
handshake на owner thread; без MoonLoader или совместимого Host включённые
providers остаются в состоянии ожидания. После установки нужен перезапуск игры
либо полная перезагрузка скриптов MoonLoader и HelperByOrc.

Используется только один файл `HelperByOrcVarsHost.lua`. Controller загружает
этот же host для каждого provider, а bridge выдаёт новому Lua state одноразовый
`provider id + generation`. Исходник provider передаётся из ASI и выполняется
в памяти соответствующего state. Копии providers и сгенерированные wrappers в
`%TEMP%` не создаются. Поэтому MoonLoader показывает отдельную системную
загрузку одного и того же host-файла для каждого включённого provider; это не
повторная загрузка provider и не retry.

## Среда выполнения

Каждый provider работает в отдельном настоящем state MoonLoader и может
использовать его API, `main` и events. Bridge и Host сверяют версию протокола,
owner thread, generation и backend epoch до загрузки provider. Одновременно
создаётся не более одного нового state. `HelperByOrc.asi` не содержит и не
линкует отдельный Lua runtime: bridge использует LuaJIT/Lua 5.1 C API уже
загруженного MoonLoader.

## Формы и типы

- `simple`: `{name}`, без параметров;
- `function`: `[name(parameter)]`; вложенные теги параметра раскрываются до
  вызова callback.

Эффект:

- `pure`: разрешён в Binder, HUD, preview и внешнем раскрытии;
- `action`: callback вызывается только в контексте с разрешёнными side effects.
  В pure-контекстах результат пустой.

Результат может иметь тип `string`, `int64`, `double`, `bool` или `nil`.
`nil` превращается в пустую строку. Lua number является `double`, поэтому
целые значения за пределами `2^53` нужно возвращать строкой.

Имя содержит только `A-Z`, `a-z`, `0-9`, `_`, занимает 1–64 байта и
сравнивается без учёта ASCII-регистра. Встроенные переменные,
`tags.custom_vars` и Lua providers используют одно пространство имён.
Конфликтующий provider не активируется до исправления имени и reload.

## Lua API

Поддерживаются legacy-вызовы:

```lua
registerVariable(name, description, callback)
registerFunctionalVariable(name, description, callback, options)
```

`registerVariable` callback может принять `thisbind_value`.
Функциональный callback получает `(parameter, thisbind_value)`.
`thisbind_value` — строковый snapshot текущего Binder-контекста либо `nil`.

`options`:

```lua
{
    example = "[name(example)]",
    effect = "pure",             -- pure | action; default action
    result_type = "string",      -- string | int64 | double | bool | nil
    cache = "expansion",         -- expansion | none | ttl | event
    ttl_ms = 1000,               -- 1..3600000 для ttl
    no_cache = true,             -- legacy alias для cache = "none"
}
```

Дополнительные функции:

```lua
invalidateVariable("name")
publishVariable("name", value)
logVariableInfo("message")
logVariableError("message")
```

`publishVariable` применяется к `simple`-переменной с `cache = "event"`.
Пользовательский `main` и events работают в state соответствующего файла.
Регистрация и variable callbacks синхронные: внутри них нельзя вызывать
`wait`, `yield` или выполнять длительную работу. Для периодического
обновления используйте `main`/event callback и `cache = "event"`.

Примеры:

- `lua\examples\legacy.lua` — legacy API, `example` и `no_cache`;
- `lua\examples\typed_moonloader.lua` — типы, pure/action, MoonLoader API и
  event-driven update.

## Кэш и ошибки

- `expansion` — один callback на одинаковый ключ в пределах одного раскрытия;
- `none` / `no_cache` — callback при каждом обращении;
- `ttl` — результат действует `ttl_ms`;
- `event` — значение обновляется через `publishVariable`.

HUD не вызывает один callback чаще раза в 100 мс даже при `cache = "none"`.
Общего скрытого TTL для Binder или внешнего раскрытия нет.

Syntax/load error отключает только соответствующий provider. Ошибка callback
сохраняет последнее успешное значение; без него тег остаётся нераскрытым.
Instruction timeout или превышение wall-time бюджета сразу quarantines только
эту переменную до reload; обычные ошибки делают это после трёх последовательных
сбоев. JIT отключается для контролируемой Lua-функции, после чего instruction
hook прерывает Lua-код. Общий wall-time бюджет callback — 8 мс, но уже начавшийся
долгий вызов C/MoonLoader API прервать невозможно: превышение определяется после
его возврата и блокирует повторные вызовы.

## Ограничения

- Lua-файл: до 1 МиБ;
- providers: до 128;
- переменных: до 128 на provider и 1024 всего;
- описание: до 512 байт UTF-8;
- example: до 256 байт UTF-8;
- результат: до 65536 байт UTF-8;
- runtime cache: до 4096 записей.

## Диагностика

В `HelperByOrc.log` используются:

```text
[tags][code][load]
[tags][code][registry]
[tags][code][lua]
[tags][code][eval]
[tags][code][perf]
```

Ошибки Host и provider scripts также попадают в `moonloader.log`.

## Технические источники

- Lua 5.1 Reference Manual: https://www.lua.org/manual/5.1/manual.html
- LuaJIT runtime предоставляет MoonLoader: https://luajit.org/
- LuaJIT C API extensions: https://luajit.org/ext_c_api.html
- LuaJIT JIT control: https://luajit.org/ext_jit.html
- LuaJIT debug-hook limitation: https://luajit.org/faq.html
