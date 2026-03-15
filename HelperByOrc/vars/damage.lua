local sampev = require("samp.events")

local vars = {
    hitmeid = "", hitmename = "", hitmesurname = "",
    hitbymeid = "", hitbymename = "", hitbymesurname = "",
}

local function reg(key, desc)
    registerVariable(key, desc, function()
        return vars[key] or ""
    end)
end

reg("hitmeid", "ID того, кто по мне стрелял")
reg("hitmename", "Имя того, кто по мне стрелял")
reg("hitmesurname", "Фамилия того, кто по мне стрелял")
reg("hitbymeid", "ID того, по кому я стрелял")
reg("hitbymename", "Имя того, по кому я стрелял")
reg("hitbymesurname", "Фамилия того, по кому я стрелял")

local function setVar(k, v)
    vars[k] = tostring(v or "")
end

local function splitNick(nick)
    nick = tostring(nick or ""):gsub("^%b[]", "") -- убрать [tag] в начале, если есть
    local name, surname = nick:match("^([^_]+)_(.+)$")
    if not name then
        return nick, ""
    end
    return name, surname
end

local function updateHit(prefix, playerId)
    if not playerId or playerId == 65535 then
        return
    end

    local nick = ""
    if sampGetPlayerNickname then
        local ok, res = pcall(sampGetPlayerNickname, playerId)
        if ok and type(res) == "string" then
            nick = res
        end
    end

    local name, surname = splitNick(nick)
    setVar(prefix .. "id", playerId)
    setVar(prefix .. "name", name)
    setVar(prefix .. "surname", surname)
end

function sampev.onSendTakeDamage(playerId)
    updateHit("hitme", playerId)
end

function sampev.onSendGiveDamage(playerId)
    updateHit("hitbyme", playerId)
end
