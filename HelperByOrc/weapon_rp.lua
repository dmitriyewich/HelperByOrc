-- HelperByOrc/weapon_rp.lua
local M = {}
local imgui = require "mimgui"
local ffi = require "ffi"
local encoding = require "encoding"
encoding.default = "CP1251"
local u8 = encoding.UTF8

-- ===== настройки =====
M.config = {
	tick_ms = 80,
	stable_need = 3,
	cooldown_frames = 10,

	auto_rp = true,
	change_as_two_lines = false, -- смена всегда в одну строку
	ignore_knuckles = true,

	prefix = "/me",
	min_me_gap_ms = 1000,		-- пауза между несколькими /me, если вдруг будут
	max_len = 96,				-- ЖЁСТКИЙ лимит на длину одной строки

	flavor_level = 2,			-- 1 сдержанно, 2 поярче, 3 ещё разнообразнее

	sender = nil,				-- function(line) ... end (nil -> sampProcessChatInput)
}

local CONFIG_PATH = "moonloader/HelperByOrc/weapon_rp.json"

local function json_decode(s)
        local ok, res = pcall(function()
                return decodeJson and decodeJson(s) or nil
        end)
        if ok and res then return res end
        local ok2, dk = pcall(require, "dkjson")
        if ok2 and dk and dk.decode then return dk.decode(s) end
        return nil
end

local function json_encode(t)
        local ok, res = pcall(function()
                return encodeJson and encodeJson(t) or nil
        end)
        if ok and res then return res end
        local ok2, dk = pcall(require, "dkjson")
        if ok2 and dk and dk.encode then return dk.encode(t, {indent = true}) end
        return nil
end

local function load_cfg()
        local f = io.open(CONFIG_PATH, "rb")
        if not f then return end
        local data = f:read("*a")
        f:close()
        local tbl = json_decode(data)
        if type(tbl) ~= "table" then return end
        for k, v in pairs(tbl) do
                if M.config[k] ~= nil then M.config[k] = v end
        end
        M.rpTakeNames = M.config.rpTakeNames
        M.rp_guns = M.config.rp_guns
end

local function save_cfg()
        local data = json_encode(M.config)
        if not data then return end
        local f = io.open(CONFIG_PATH, "wb")
        if f then
                f:write(data)
                f:close()
        end
end

M.save = save_cfg
M.reload = load_cfg

-- ===== события/внутрянка =====
local running, thr = false, nil
local prev_weapon, candidate_weapon, stable_count, cooldown = -1, -1, 0, 0
local cb_any, cb_show, cb_hide, cb_change
function M.onAny(fn)	cb_any = fn end
function M.onShow(fn)   cb_show = fn end
function M.onHide(fn)   cb_hide = fn end
function M.onChange(fn) cb_change = fn end

-- быстрые сеттеры
function M.setAutoRp(b)			  M.config.auto_rp = not not b end
function M.setChangeMode(two_lines)  M.config.change_as_two_lines = not not two_lines end
function M.setSender(fn)			 M.config.sender = fn end
function M.setPrefix(p)			  M.config.prefix = tostring(p or "/me") end
function M.setStableNeed(n)		  M.config.stable_need = math.max(1, tonumber(n) or 3) end
function M.setCooldownFrames(n)	  M.config.cooldown_frames = math.max(0, tonumber(n) or 0) end
function M.setIgnoreKnuckles(b)	  M.config.ignore_knuckles = not not b end
function M.setMinMeGapMs(n)		  M.config.min_me_gap_ms = math.max(0, tonumber(n) or 0) end
function M.setFlavorLevel(n)		 M.config.flavor_level = math.max(1, math.min(3, tonumber(n) or 2)) end
function M.setMaxLen(n)			  M.config.max_len = math.max(20, tonumber(n) or 90) end

M.config.rpTakeNames = {
	[1] = {"из-за спины", "за спину"},
	[2] = {"из кармана", "в карман"},
	[3] = {"с пояса", "на пояс"},
	[4] = {"из кобуры", "в кобуру"},
	[5] = {"из ножен", "в ножны"},
	[6] = {"из подсумка", "в подсумок"},
	[7] = {"с плеча", "на плечо"},
	[8] = {"из сумки", "в сумку"},
}
-- короткие варианты мест (для ужатия)
M.config.rpTakeNamesShort = {
	[1] = {"со спины","за спину"},
	[2] = {"из кармана","в карман"},
	[3] = {"с пояса","на пояс"},
	[4] = {"из кобуры","в кобуру"},
	[5] = {"из ножен","в ножны"},
	[6] = {"из подсумка","в подсумок"},
	[7] = {"с плеча","на плечо"},
	[8] = {"из сумки","в сумку"},
}

-- глаголы по месту ношения
M.config.verb_map = {
	[1] = {show={"достал","снял"}, hide={"убрал","повесил"}},
	[2] = {show={"достал","вытащил"}, hide={"убрал","спрятал"}},
	[3] = {show={"снял","достал"}, hide={"убрал","вернул"}},
	[4] = {show={"достал","вытащил"}, hide={"убрал","спрятал"}},
	[5] = {show={"обнажил","вытащил"}, hide={"вложил","убрал"}},
	[6] = {show={"извлёк","достал"}, hide={"убрал","засунул"}},
	[7] = {show={"снял","достал"}, hide={"повесил","убрал"}},
	[8] = {show={"достал","вынул"}, hide={"убрал","спрятал"}},
}

-- приправы
M.config.adv_show = {"уверенно","быстрым движением","плавно","ловко","легким движением руки"}
M.config.adv_hide = {"аккуратно","спокойно","без лишнего шума","коротким движением","бережно"}
M.config.connectors_full  = {", затем ","; после чего ","; и тут же ",", не теряя времени, "}
M.config.connectors_short = {", затем "}

-- оружие (name + короткое имя short)
M.config.rp_guns = {
	[0]  = {name="кулаки", enable=false, rpTake=2, short="кулаки"},
	[1]  = {name="кастеты", enable=true,  rpTake=2, short="кастеты"},
	[2]  = {name="клюшку для гольфа", enable=true, rpTake=1, short="клюшку"},
	[3]  = {name="дубинку", enable=true, rpTake=3, short="дубинку"},
	[4]  = {name="нож", enable=true, rpTake=3, short="нож"},
	[5]  = {name="биту", enable=true, rpTake=1, short="биту"},
	[6]  = {name="лопату", enable=true, rpTake=1, short="лопату"},
	[7]  = {name="кий", enable=true, rpTake=1, short="кий"},
	[8]  = {name="катану", enable=true, rpTake=5, short="катану"},
	[9]  = {name="бензопилу", enable=true, rpTake=1, short="бензопилу"},

	[10] = {name="дилдо", enable=true, rpTake=2, short="дилдо"},
	[11] = {name="дилдо", enable=true, rpTake=2, short="дилдо"},
	[12] = {name="вибратор", enable=true, rpTake=2, short="вибратор"},
	[13] = {name="вибратор", enable=true, rpTake=2, short="вибратор"},
	[14] = {name="букет цветов", enable=true, rpTake=2, short="букет"},
	[15] = {name="трость", enable=true, rpTake=3, short="трость"},

	[16] = {name="осколочную гранату", enable=true, rpTake=6, short="гранату"},
	[17] = {name="газовую гранату",   enable=true, rpTake=6, short="газ. гранату"},
	[18] = {name="коктейль Молотова", enable=true, rpTake=6, short="молотов"},

	[22] = {name="пистолет Colt 45", enable=true, rpTake=4, short="Colt 45"},
	[23] = {name="электрошокер Taser X26P", enable=true, rpTake=4, short="Taser X26P"},
	[24] = {name="пистолет Desert Eagle", enable=true, rpTake=4, short="Deagle"},

	[25] = {name="дробовик", enable=true, rpTake=7, short="дробовик"},
	[26] = {name="обрез", enable=true, rpTake=7, short="обрез"},
	[27] = {name="тактический обрез", enable=true, rpTake=7, short="обрез"},

	[28] = {name="пистолет-пулемёт Micro Uzi", enable=true, rpTake=7, short="Uzi"},
	[29] = {name="пистолет-пулемёт MP5", enable=true, rpTake=7, short="MP5"},
	[30] = {name="автомат AK-47", enable=true, rpTake=7, short="AK-47"},
	[31] = {name="автомат M4", enable=true, rpTake=7, short="M4"},
	[32] = {name="пистолет-пулемёт Tec-9", enable=true, rpTake=7, short="Tec-9"},

	[33] = {name="винтовку Rifle", enable=true, rpTake=7, short="Rifle"},
	[34] = {name="снайперскую винтовку", enable=true, rpTake=7, short="снайперку"},

	[35] = {name="гранатомёт", enable=true, rpTake=1, short="гранатомёт"},
	[36] = {name="пусковую установку с наведением", enable=true, rpTake=1, short="ПУ с навед."},
	[37] = {name="огнемёт", enable=true, rpTake=1, short="огнемёт"},
	[38] = {name="миниган", enable=true, rpTake=1, short="миниган"},

	[39] = {name="заряд C4", enable=true, rpTake=6, short="C4"},
	[40] = {name="детонатор", enable=true, rpTake=2, short="детонатор"},

	[41] = {name="перцовый баллончик", enable=true, rpTake=3, short="баллончик"},
	[42] = {name="огнетушитель", enable=true, rpTake=1, short="огнетушитель"},
	[43] = {name="фотоаппарат", enable=true, rpTake=8, short="фотоаппарат"},
	[44] = {name="прибор ночного видения", enable=true, rpTake=8, short="ПНВ"},
	[45] = {name="тепловизор", enable=true, rpTake=8, short="тепловизор"},
	[46] = {name="парашют", enable=true, rpTake=1, short="парашют"},

	-- причины урона — выключены
	[49] = {name="автомобиль", enable=false, rpTake=1, short="автомобиль"},
	[50] = {name="лопасти вертолёта", enable=false, rpTake=1, short="лопасти вертолёта"},
	[51] = {name="бомбу", enable=false, rpTake=1, short="бомбу"},
	[54] = {name="коллизию", enable=false, rpTake=1, short="коллизию"},

	-- ARZ кастом
	[71] = {name="пистолет Desert Eagle Steel", enable=true, rpTake=4, short="Deagle Steel"},
	[72] = {name="пистолет Desert Eagle Gold",  enable=true, rpTake=4, short="Deagle Gold"},
	[73] = {name="пистолет Glock Gradient",	 enable=true, rpTake=4, short="Glock"},
	[74] = {name="пистолет Desert Eagle Flame",  enable=true, rpTake=4, short="Deagle Flame"},
	[75] = {name="револьвер Python Royal",	  enable=true, rpTake=4, short="Python R."},
	[76] = {name="револьвер Python Silver",	 enable=true, rpTake=4, short="Python S."},

	[77] = {name="автомат AK-47 Roses", enable=true, rpTake=7, short="AK-47 Roses"},
	[78] = {name="автомат AK-47 Gold",  enable=true, rpTake=7, short="AK-47 Gold"},

	[79] = {name="пулемёт M249 Graffiti", enable=true, rpTake=7, short="M249 Graf."},
	[80] = {name="карабин Сайга (золото)", enable=true, rpTake=7, short="Сайга Gold"},
	[81] = {name="пистолет-пулемёт Standard", enable=true, rpTake=7, short="ПП Std."},
	[82] = {name="пулемёт M249", enable=true, rpTake=7, short="M249"},
	[83] = {name="пистолет-пулемёт Skorpion", enable=true, rpTake=7, short="Skorpion"},

	[84] = {name="автомат AKS-74 (камуфляж)", enable=true, rpTake=7, short="AKS-74 camo"},
	[85] = {name="автомат AK-47 (камуфляж)",  enable=true, rpTake=7, short="AK-47 camo"},
	[86] = {name="дробовик Rebecca", enable=true, rpTake=7, short="Rebecca"},

	[87] = {name="портальную пушку", enable=true, rpTake=8, short="портал-пушку"},
	[88] = {name="ледяной меч", enable=true, rpTake=5, short="ледяной меч"},
	[89] = {name="портальную пушку", enable=true, rpTake=8, short="портал-пушку"},

	[90] = {name="светошумовую гранату", enable=true, rpTake=6, short="светошум. гранату"},
	[91] = {name="ослепляющую гранату",  enable=true, rpTake=6, short="ослепл. гранату"},

	[92] = {name="снайперскую винтовку McMillan TAC-50", enable=true, rpTake=7, short="TAC-50"},
	[93] = {name="электрошоковый пистолет", enable=true, rpTake=4, short="электрошокер"},
}

-- слот -> фолбэк
M.config.slot_to_take = { [0]=2,[1]=1,[2]=4,[3]=7,[4]=7,[5]=7,[6]=7,[7]=1,[8]=6,[9]=8,[10]=8,[11]=2,[12]=2 }

M.rpTakeNames = M.config.rpTakeNames
M.rp_guns = M.config.rp_guns

-- ===== очередь /me с паузой =====
local send_queue, send_thr = {}, nil
local function ensure_send_thread()
	if send_thr and send_thr:status() ~= "dead" then return end
	send_thr = lua_thread.create(function()
		while true do
			if #send_queue > 0 then
				local s = table.remove(send_queue, 1)
                                s = u8:decode(s)
				if s and s ~= "" then
					if type(M.config.sender) == "function" then
						pcall(M.config.sender, s)
					elseif sampSendChat then
						sampSendChat(s)
					end
					wait(M.config.min_me_gap_ms)
				else wait(50) end
			else wait(50) end
		end
	end)
end
local function send_chat(line)
	if not line or line == "" then return end
	ensure_send_thread()
	table.insert(send_queue, line)
end

-- ===== утилиты =====
local function safe_pcall(f, ...) if type(f) == "function" then pcall(f, ...) end end

local function is_empty_weapon(id)
	if id == -1 or id == 0 then return true end
	if id == 1 and M.config.ignore_knuckles then return true end
	return false
end

local function get_slot_take_for(id)
        local ok, slot = pcall(getWeapontypeSlot, id)
        slot = ok and slot or -1
        return M.config.slot_to_take[slot] or 2
end

local function winfo(id)
        local g = M.config.rp_guns[id]
	if g then return g end
	return {name=("оружие %d"):format(id), short=("оружие %d"):format(id), enable=true, rpTake=get_slot_take_for(id)}
end

-- длина строки (UTF-8/CP1251 совместимо)
local function str_len(s)
	local ok, utf8 = pcall(require, "lua-utf8")
	if ok and utf8 and utf8.len then
		local ok2, n = pcall(utf8.len, s)
		if ok2 and n then return n end
	end
	return #s
end

local function pick(t, salt)
	local n = #t
	if n == 0 then return nil end
	local x = math.floor((os.clock()*1000 + (salt or 0))) % n + 1
	if M.config.flavor_level == 1 then x = ((salt or 0) % n) + 1 end
	return t[x]
end

-- конструкторы фраз с опциями
local function place_phrase(take, is_from, compact)
        local src = compact and M.config.rpTakeNamesShort or M.config.rpTakeNames
	local p = src[take] and src[take][is_from and 1 or 2] or ""
	return p
end

local function verb_for(take, kind, plain)
        local vm = M.config.verb_map[take] or {}
	local set = (kind == "show") and (vm.show or {"достал"}) or (vm.hide or {"убрал"})
	if plain then return set[1] or "достал" end
	return pick(set, (kind=="show") and 17 or 23) or set[1] or "достал"
end

local function adv_for(kind, with_adv)
	if not with_adv or M.config.flavor_level < 2 then return "" end
        local a = (kind=="show") and pick(M.config.adv_show, 7) or pick(M.config.adv_hide, 13)
	return a and (a.." ") or ""
end

local function gun_name(g, short) return (short and g.short) or g.name end

local function build_shown(id, opts)
	local g = winfo(id); if g.enable == false then return nil end
	local take = g.rpTake or get_slot_take_for(id)
	local v = verb_for(take, "show", opts.plain_verbs)
	local a = adv_for("show", opts.adverbs)
	local place = place_phrase(take, true, opts.short_place)
	local name = gun_name(g, opts.short_names)
	return ("%s %s%s %s %s"):format(M.config.prefix, a, v, place, name)
end

local function build_hidden(id, opts)
	local g = winfo(id); if g.enable == false then return nil end
	local take = g.rpTake or get_slot_take_for(id)
	local v = verb_for(take, "hide", opts.plain_verbs)
	local a = adv_for("hide", opts.adverbs)
	local place = place_phrase(take, false, opts.short_place)
	local name = gun_name(g, opts.short_names)
	return ("%s %s%s %s %s"):format(M.config.prefix, a, v, place, name)
end

local function join_changed(hidden_str, shown_str, connector)
	local s1 = hidden_str:gsub("^/me%s*", "")
    local s2 = shown_str:gsub("^/me%s*", "")
	return ("%s %s%s%s"):format(M.config.prefix, s1, connector, s2)
end

-- попытки ужатия строки по уровням
local function try_build(kind, newId, oldId, level)
	local opts = {
		adverbs	  = (level <= 1),
		plain_verbs  = (level >= 2),
		short_names  = (level >= 3),
		short_place  = (level >= 4),
	}
        local conn_set = (level >= 1) and M.config.connectors_short or M.config.connectors_full
	local connector = pick(conn_set, (newId or 0)+(oldId or 0)) or ", затем "

	if kind == "shown" then
		return build_shown(newId, opts)
	elseif kind == "hidden" then
		return build_hidden(oldId, opts)
	else
		local h = build_hidden(oldId, opts)
		local s = build_shown(newId, opts)
		if h and s then
			return join_changed(h, s, connector)
		end
		return s or h
	end
end

-- финальная сборка с жёстким контролем длины
function M.makeRpLine(kind, newId, oldId)
	-- уровни ужатия:
	-- 0: максимум украшений
	-- 1: короткий коннектор
	-- 2: без наречий + простые глаголы
	-- 3: короткие имена оружия
	-- 4: короткие места
	-- 5: без места ношения вообще (радикально)
	local line
	for lvl = 0, 4 do
		line = try_build(kind, newId, oldId, lvl)
		if line and str_len(line) <= M.config.max_len then return line end
	end
	-- радикальная компактность: без места ношения
	local function compact_no_place(kind2, nid, oid)
		local opts = {adverbs=false, plain_verbs=true, short_names=true, short_place=true}
		if kind2 == "shown" then
			local g = winfo(nid); if g.enable == false then return nil end
			return ("%s достал %s"):format(M.config.prefix, gun_name(g, true))
		elseif kind2 == "hidden" then
			local g = winfo(oid); if g.enable == false then return nil end
			return ("%s убрал %s"):format(M.config.prefix, gun_name(g, true))
		else
			local go, gn = winfo(oid), winfo(nid)
			if (go.enable == false) and (gn.enable == false) then return nil end
			local conn = ", затем "
			local part1 = go.enable ~= false and ("убрал "..gun_name(go, true)) or nil
			local part2 = gn.enable ~= false and ("достал "..gun_name(gn, true)) or nil
			if part1 and part2 then
				return ("%s %s%s%s"):format(M.config.prefix, part1, conn, part2)
			end
			return ("%s %s"):format(M.config.prefix, part1 or part2 or "")
		end
	end
	line = compact_no_place(kind, newId, oldId)
	if line and str_len(line) <= M.config.max_len then return line end

	-- совсем край: если всё ещё длинно, усечём по слову и добавим "…"
	if line and str_len(line) > M.config.max_len then
		local s = line
		while str_len(s) > M.config.max_len - 1 do
			s = s:match("^(.*)%s+[^%s]+$") or s:sub(1, M.config.max_len - 1)
		end
		return s .. "…"
	end
	return line
end

-- авто-RP
local function auto_rp(kind, newW, oldW)
	if not M.config.auto_rp then return end
	local line = M.makeRpLine(kind, newW, oldW)
	if line and line ~= "" then send_chat(line) end
end

local function fire(kind, newW, oldW)
	safe_pcall(cb_any, kind, newW, oldW)
	if kind == "shown"  then safe_pcall(cb_show, newW, oldW)
	elseif kind == "hidden" then safe_pcall(cb_hide, newW, oldW)
	elseif kind == "changed" then safe_pcall(cb_change, newW, oldW)
	end
	auto_rp(kind, newW, oldW)
end

-- ===== детектор =====
local function update_once()
	local ped = PLAYER_PED
	if not ped then return end
	local w = getCurrentCharWeapon(ped)
	if w == nil then return end

	if w == candidate_weapon then
		if stable_count < M.config.stable_need then
			stable_count = stable_count + 1
		end
	else
		candidate_weapon = w
		stable_count = 1
	end

	if cooldown > 0 then
		cooldown = cooldown - 1
		return
	end

	if stable_count >= M.config.stable_need and candidate_weapon ~= prev_weapon then
		local oldW, newW = prev_weapon, candidate_weapon
		local kind
		if is_empty_weapon(oldW) and not is_empty_weapon(newW) then
			kind = "shown"
		elseif not is_empty_weapon(oldW) and is_empty_weapon(newW) then
			kind = "hidden"
		else
			kind = "changed"
		end
		prev_weapon = candidate_weapon
		cooldown = M.config.cooldown_frames
		fire(kind, newW, oldW)
	end
end

function M.start(interval_ms)
	if running then return end
	running = true
	prev_weapon, candidate_weapon, stable_count, cooldown = 0, 0, 0, 0
	if interval_ms then M.config.tick_ms = interval_ms end
	ensure_send_thread()
	thr = lua_thread.create(function()
		while running do
			wait(M.config.tick_ms)
			update_once()
		end
	end)
end

function M.stop()
	running = false
	if thr and thr:status() ~= "dead" then thr:terminate() end
	thr = nil
end

-- пример: логи в чат
function M.bindExample()
	M.onAny(function(kind, newW, oldW)
		if sampAddChatMessage then
			sampAddChatMessage(("[оружие] %s: %d -> %d"):format(kind, oldW or -1, newW or -1), 0xFFCC66)
		end
	end)
end
-- ===== Настройки UI =====
local function tooltip(text)
        if imgui.IsItemHovered() then imgui.SetTooltip(text) end
end
function M.DrawSettingsInline()
        local run = imgui.new.bool(running)
        if imgui.Checkbox("Включить модуль", run) then
                if run[0] then M.start() else M.stop() end
        end
        tooltip("Запускает или останавливает отслеживание смены оружия")

        local auto = imgui.new.bool(M.config.auto_rp)
        if imgui.Checkbox("Авто /me", auto) then
                M.setAutoRp(auto[0])
                save_cfg()
        end
        tooltip("Автоматически отправлять RP фразы в чат")

        local two = imgui.new.bool(M.config.change_as_two_lines)
        if imgui.Checkbox("Смена в две строки", two) then
                M.setChangeMode(two[0])
                save_cfg()
        end
        tooltip("При смене оружия отправлять две отдельные /me")

        local ign = imgui.new.bool(M.config.ignore_knuckles)
        if imgui.Checkbox("Игнорировать кастеты", ign) then
                M.setIgnoreKnuckles(ign[0])
                save_cfg()
        end
        tooltip("Не реагировать на переключение на кастеты")

        imgui.Separator()

        local prefix = imgui.new.char[16](M.config.prefix)
        if imgui.InputText("Префикс", prefix, ffi.sizeof(prefix)) then
                M.setPrefix(ffi.string(prefix))
                save_cfg()
        end
        tooltip("Команда, используемая для RP, например /me")

        local tick = ffi.new("int[1]", M.config.tick_ms)
        if imgui.InputInt("Интервал проверки (мс)", tick) then
                M.config.tick_ms = math.max(10, tick[0])
                save_cfg()
        end
        tooltip("Как часто проверяется текущее оружие")

        local stable = ffi.new("int[1]", M.config.stable_need)
        if imgui.InputInt("Кадров стабильности", stable) then
                M.setStableNeed(stable[0])
                save_cfg()
        end
        tooltip("Сколько кадров оружие должно быть одинаковым для фиксации")

        local cooldown = ffi.new("int[1]", M.config.cooldown_frames)
        if imgui.InputInt("Кадров задержки", cooldown) then
                M.setCooldownFrames(cooldown[0])
                save_cfg()
        end
        tooltip("Задержка после обнаружения смены")

        local min_gap = ffi.new("int[1]", M.config.min_me_gap_ms)
        if imgui.InputInt("Пауза между /me (мс)", min_gap) then
                M.setMinMeGapMs(min_gap[0])
                save_cfg()
        end
        tooltip("Минимальный интервал между RP сообщениями")

        local max_len = ffi.new("int[1]", M.config.max_len)
        if imgui.InputInt("Макс длина строки", max_len) then
                M.setMaxLen(max_len[0])
                save_cfg()
        end
        tooltip("Ограничение длины сформированной строки")

        local flavor = ffi.new("int[1]", M.config.flavor_level)
        if imgui.SliderInt("Уровень разнообразия", flavor, 1, 3) then
                M.setFlavorLevel(flavor[0])
                save_cfg()
        end
        tooltip("1 — минимум украшений, 3 — максимум разнообразия")
end

load_cfg()

return M
