local event_counter = 0

registerVariable(
    "moonloader_version",
    "Версия настоящего MoonLoader, в state которого выполняется provider",
    function()
        return getMoonloaderVersion()
    end,
    {
        effect = "pure",
        result_type = "int64",
        cache = "ttl",
        ttl_ms = 1000,
        example = "{moonloader_version}",
    })

registerVariable(
    "event_counter",
    "Event-driven счётчик без callback в HUD hot path",
    function()
        return event_counter
    end,
    {
        effect = "pure",
        result_type = "int64",
        cache = "event",
        example = "{event_counter}",
    })

registerFunctionalVariable(
    "ml_chat",
    "Добавляет сообщение через MoonLoader только в action-контексте",
    function(param)
        if not isSampAvailable() then
            return nil
        end
        sampAddChatMessage(tostring(param or ""), -1)
        return nil
    end,
    {
        effect = "action",
        result_type = "nil",
        cache = "none",
        example = "[ml_chat(Текст)]",
    })

function main()
    while true do
        wait(1000)
        event_counter = event_counter + 1
        publishVariable("event_counter", event_counter)
    end
end
