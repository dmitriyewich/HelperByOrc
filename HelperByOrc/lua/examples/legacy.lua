registerVariable("test", "Пример обычной legacy-переменной", function()
    return "Значение из legacy.lua"
end)

registerFunctionalVariable(
    "test_func",
    "Возвращает переданный текст",
    function(param)
        param = tostring(param or "")
        if param == "" then
            return "[test_func(текст)]"
        end
        return param
    end,
    {
        example = "[test_func(Пример текста)]",
    })

registerFunctionalVariable(
    "legacy_no_cache",
    "Legacy callback без parse-scope cache",
    function(param, thisbind_value)
        return tostring(param or "") .. (thisbind_value and " @ " .. thisbind_value or "")
    end,
    {
        example = "[legacy_no_cache(пример)]",
        no_cache = true,
    })
