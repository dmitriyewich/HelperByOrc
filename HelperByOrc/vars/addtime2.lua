registerFunctionalVariable('addtime2', 'Текущее время + указанное время, MM:SS', function(param)
    param = tostring(param or "")
    local min, sec = param:match('(%d+):(%d+)')
    min, sec = tonumber(min), tonumber(sec)
    if min and sec then
        return os.date("%H:%M:%S", os.time() + (min*60) + sec)
    end
    return "[addtime2("..param..")]"
end)
