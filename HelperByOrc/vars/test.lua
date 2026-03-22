--[[
test.lua
Пример внешних переменных для HelperByOrc / tags.lua

Папка HelperByOrc\vars загружается автоматически при запуске проекта.
Все файлы *.lua из этой папки можно использовать для регистрации своих переменных.

Доступны два регистратора:

1. registerVariable(name, desc, fn)
   Обычная внешняя переменная.
   Используется в тексте как {name}

2. registerFunctionalVariable(name, desc, fn, opts)
   Функциональная внешняя переменная.
   Используется в тексте как [name(...)]

Пояснения:
- name: имя переменной. Лучше использовать латиницу, цифры и символ _
- desc: описание, которое видно в списке тегов
- fn: функция, которая возвращает итоговое значение
- opts.example: пример для справки
- opts.no_cache = true: отключает кэш, если значение должно пересчитываться каждый раз

Для функциональной переменной fn может принимать:
- param: строку внутри круглых скобок
- thisbind_value: служебное значение текущего бинда, если оно нужно
]]

registerVariable("test", "Пример обычной внешней переменной из test.lua", function()
    return "Значение из test.lua"
end)

registerFunctionalVariable("test_func", "Пример функциональной внешней переменной. Возвращает переданный текст", function(param)
    param = tostring(param or "")
    if param == "" then
        return "[test_func(текст)]"
    end
    return param
end, {
    example = "[test_func(Пример текста)]",
})

-- Шаблон функциональной переменной без кэша:
-- registerFunctionalVariable("my_func", "Описание", function(param, thisbind_value)
--     return tostring(param or "")
-- end, {
--     example = "[my_func(пример)]",
--     no_cache = true,
-- })
