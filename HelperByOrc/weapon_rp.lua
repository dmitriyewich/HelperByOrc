-- HelperByOrc/weapon_rp.lua
local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local funcs = require("HelperByOrc.funcs")
local mimgui_funcs

-- ===== настройки =====
M.config = {
	tick_ms = 80,
	stable_need = 3,
	cooldown_frames = 10,

	auto_mode = 0, -- 0 = авто /me, 1 = по ПКМ
	change_as_two_lines = false, -- смена всегда в одну строку
	ignore_knuckles = true,

	prefix = "/me",
	min_me_gap_ms = 1000, -- пауза между несколькими /me, если вдруг будут
	max_len = 96, -- ЖЁСТКИЙ лимит на длину одной строки

        flavor_level = 2, -- 1 сдержанно, 2 поярче, 3 ещё разнообразнее

        sender = nil, -- function(line) ... end (nil -> sampProcessChatInput)

        -- пресеты RP-фраз
        presets = {},
        active_preset = 1,
}

local preset_editor_open = imgui.new.bool(false)

local CONFIG_PATH = "moonloader/HelperByOrc/weapon_rp.json"

function M.attachModules(mod)
	funcs = mod.funcs or funcs
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs
end

-- шаблоны по типу ношения
local slot_templates = {
	[1] = {
		rpTakeNames = { "из-за спины", "за спину" },
		rpTakeNamesShort = { "со спины", "за спину" },
		verb_map = { show = { "достал", "снял" }, hide = { "убрал", "повесил" } },
	},
	[2] = {
		rpTakeNames = { "из кармана", "в карман" },
		rpTakeNamesShort = { "из кармана", "в карман" },
		verb_map = { show = { "достал", "вытащил" }, hide = { "убрал", "спрятал" } },
	},
	[3] = {
		rpTakeNames = { "с пояса", "на пояс" },
		rpTakeNamesShort = { "с пояса", "на пояс" },
		verb_map = { show = { "снял", "достал" }, hide = { "убрал", "вернул" } },
	},
	[4] = {
		rpTakeNames = { "из кобуры", "в кобуру" },
		rpTakeNamesShort = { "из кобуры", "в кобуру" },
		verb_map = { show = { "достал", "вытащил" }, hide = { "убрал", "спрятал" } },
	},
	[5] = {
		rpTakeNames = { "из ножен", "в ножны" },
		rpTakeNamesShort = { "из ножен", "в ножны" },
		verb_map = { show = { "обнажил", "вытащил" }, hide = { "вложил", "убрал" } },
	},
	[6] = {
		rpTakeNames = { "из подсумка", "в подсумок" },
		rpTakeNamesShort = { "из подсумка", "в подсумок" },
		verb_map = { show = { "извлёк", "достал" }, hide = { "убрал", "засунул" } },
	},
	[7] = {
		rpTakeNames = { "с плеча", "на плечо" },
		rpTakeNamesShort = { "с плеча", "на плечо" },
		verb_map = { show = { "снял", "достал" }, hide = { "повесил", "убрал" } },
	},
	[8] = {
		rpTakeNames = { "из сумки", "в сумку" },
		rpTakeNamesShort = { "из сумки", "в сумку" },
		verb_map = { show = { "достал", "вынул" }, hide = { "убрал", "спрятал" } },
	},
}

local global_templates = {
	adv_show = {
		"уверенно",
		"быстрым движением",
		"плавно",
		"ловко",
		"легким движением руки",
	},
	adv_hide = {
		"аккуратно",
		"спокойно",
		"без лишнего шума",
		"коротким движением",
		"бережно",
	},
	connectors_full = {
		", затем ",
		"; после чего ",
		"; и тут же ",
		", не теряя времени, ",
	},
	connectors_short = { ", затем " },
}

local function deepcopy(t)
	if type(t) ~= "table" then
		return t
	end
	local r = {}
	for k, v in pairs(t) do
		r[k] = deepcopy(v)
	end
	return r
end

local function apply_defaults(g)
	local slot = g.slot_to_take or g.rpTake or 2
	g.slot_to_take = slot
	g.rpTake = nil
	local tmpl = slot_templates[slot] or slot_templates[2]
	if tmpl then
		g.rpTakeNames = g.rpTakeNames or deepcopy(tmpl.rpTakeNames)
		g.rpTakeNamesShort = g.rpTakeNamesShort or deepcopy(tmpl.rpTakeNamesShort)
		g.verb_map = g.verb_map or deepcopy(tmpl.verb_map)
	end
	for k, v in pairs(global_templates) do
		g[k] = g[k] or deepcopy(v)
	end
	return g
end

local rebuild_weapon_lists

local function default_preset()
        return {
                full_name = "",
                short_name = "",
                slot = 2,
                actions = { "достал", "убрал" },
                adv_show = { "аккуратно", "плавно" },
                adv_hide = { "аккуратно", "плавно" },
                connectors = { "затем" },
                selected_actions = {},
                selected_adv_show = {},
                selected_adv_hide = {},
                selected_connectors = {},
                enable = true,
        }
end

local function draw_pill(tag, selected)
        imgui.PushStyleVar(imgui.StyleVar.FrameRounding, 12)
        imgui.PushStyleColor(imgui.Col.Button, selected and imgui.ImVec4(0.20, 0.40, 0.60, 0.80) or imgui.ImVec4(0.15, 0.18, 0.22, 0.80))
        imgui.PushStyleColor(imgui.Col.ButtonHovered, imgui.ImVec4(0.25, 0.45, 0.65, 0.80))
        imgui.PushStyleColor(imgui.Col.ButtonActive, imgui.ImVec4(0.30, 0.50, 0.80, 0.80))
        local clicked = imgui.Button(tag)
        imgui.PopStyleColor(3)
        imgui.PopStyleVar()
        return clicked
end

local function build_preview(p)
        local parts = {}
        if p.full_name and p.full_name ~= "" then table.insert(parts, p.full_name) end
        for act, _ in pairs(p.selected_actions or {}) do
                table.insert(parts, act)
        end
        for adv, _ in pairs(p.selected_adv_show or {}) do
                table.insert(parts, adv)
        end
        for conn, _ in pairs(p.selected_connectors or {}) do
                table.insert(parts, conn)
        end
        for adv, _ in pairs(p.selected_adv_hide or {}) do
                table.insert(parts, adv)
        end
        return table.concat(parts, " ")
end

local function load_cfg()
	local tbl = funcs.loadTableFromJson(CONFIG_PATH)
	for k, v in pairs(tbl) do
		if k == "auto_rp" and tbl.auto_mode == nil then
			M.config.auto_mode = v and 0 or 1
		elseif M.config[k] ~= nil then
			if type(M.config[k]) == "table" and type(v) == "table" then
				local t = {}
				for kk, vv in pairs(v) do
					t[tonumber(kk) or kk] = vv
				end
				M.config[k] = t
			else
				M.config[k] = v
			end
		end
	end
	M.config.rp_guns = M.config.rp_guns or {}
	for _, g in pairs(M.config.rp_guns) do
		apply_defaults(g)
	end
	M.rp_guns = M.config.rp_guns
	rebuild_weapon_lists()
end

local function save_cfg()
	funcs.saveTableToJson(M.config, CONFIG_PATH)
end

M.save = save_cfg
M.reload = load_cfg

-- ===== события/внутрянка =====
local running, thr = false, nil
local prev_weapon, candidate_weapon, stable_count, cooldown = -1, -1, 0, 0
local pending_old, pending_new
local cb_any, cb_show, cb_hide, cb_change
function M.onAny(fn)
	cb_any = fn
end
function M.onShow(fn)
	cb_show = fn
end
function M.onHide(fn)
	cb_hide = fn
end
function M.onChange(fn)
	cb_change = fn
end

-- быстрые сеттеры
function M.setAutoRp(b)
	M.config.auto_mode = b and 0 or 1
end
function M.setChangeMode(two_lines)
	M.config.change_as_two_lines = not not two_lines
end
function M.setSender(fn)
	M.config.sender = fn
end
function M.setPrefix(p)
	M.config.prefix = tostring(p or "/me")
end
function M.setStableNeed(n)
	M.config.stable_need = math.max(1, tonumber(n) or 3)
end
function M.setCooldownFrames(n)
	M.config.cooldown_frames = math.max(0, tonumber(n) or 0)
end
function M.setIgnoreKnuckles(b)
	M.config.ignore_knuckles = not not b
end
function M.setMinMeGapMs(n)
	M.config.min_me_gap_ms = math.max(0, tonumber(n) or 0)
end
function M.setFlavorLevel(n)
	M.config.flavor_level = math.max(1, math.min(3, tonumber(n) or 2))
end
function M.setMaxLen(n)
	M.config.max_len = math.max(20, tonumber(n) or 90)
end

-- оружие (name + короткое имя short)
M.config.rp_guns = {
	[0] = { name = "кулаки", enable = false, slot_to_take = 2, short = "кулаки" },
	[1] = { name = "кастеты", enable = true, slot_to_take = 2, short = "кастеты" },
	[2] = { name = "клюшку для гольфа", enable = true, slot_to_take = 1, short = "клюшку" },
	[3] = { name = "дубинку", enable = true, slot_to_take = 3, short = "дубинку" },
	[4] = { name = "нож", enable = true, slot_to_take = 3, short = "нож" },
	[5] = { name = "биту", enable = true, slot_to_take = 1, short = "биту" },
	[6] = { name = "лопату", enable = true, slot_to_take = 1, short = "лопату" },
	[7] = { name = "кий", enable = true, slot_to_take = 1, short = "кий" },
	[8] = { name = "катану", enable = true, slot_to_take = 5, short = "катану" },
	[9] = { name = "бензопилу", enable = true, slot_to_take = 1, short = "бензопилу" },

	[10] = { name = "дилдо", enable = true, slot_to_take = 2, short = "дилдо" },
	[11] = { name = "дилдо", enable = true, slot_to_take = 2, short = "дилдо" },
	[12] = { name = "вибратор", enable = true, slot_to_take = 2, short = "вибратор" },
	[13] = { name = "вибратор", enable = true, slot_to_take = 2, short = "вибратор" },
	[14] = { name = "букет цветов", enable = true, slot_to_take = 2, short = "букет" },
	[15] = { name = "трость", enable = true, slot_to_take = 3, short = "трость" },

	[16] = { name = "осколочную гранату", enable = true, slot_to_take = 6, short = "гранату" },
	[17] = { name = "газовую гранату", enable = true, slot_to_take = 6, short = "газ. гранату" },
	[18] = { name = "коктейль Молотова", enable = true, slot_to_take = 6, short = "молотов" },

	[22] = { name = "пистолет Colt 45", enable = true, slot_to_take = 4, short = "Colt 45" },
	[23] = { name = "электрошокер Taser X26P", enable = true, slot_to_take = 4, short = "Taser X26P" },
	[24] = { name = "пистолет Desert Eagle", enable = true, slot_to_take = 4, short = "Deagle" },

	[25] = { name = "дробовик", enable = true, slot_to_take = 7, short = "дробовик" },
	[26] = { name = "обрез", enable = true, slot_to_take = 7, short = "обрез" },
	[27] = { name = "тактический обрез", enable = true, slot_to_take = 7, short = "обрез" },

	[28] = { name = "пистолет-пулемёт Micro Uzi", enable = true, slot_to_take = 7, short = "Uzi" },
	[29] = { name = "пистолет-пулемёт MP5", enable = true, slot_to_take = 7, short = "MP5" },
	[30] = { name = "автомат AK-47", enable = true, slot_to_take = 7, short = "AK-47" },
	[31] = { name = "автомат M4", enable = true, slot_to_take = 7, short = "M4" },
	[32] = { name = "пистолет-пулемёт Tec-9", enable = true, slot_to_take = 7, short = "Tec-9" },

	[33] = { name = "винтовку Rifle", enable = true, slot_to_take = 7, short = "Rifle" },
	[34] = { name = "снайперскую винтовку", enable = true, slot_to_take = 7, short = "снайперку" },

	[35] = { name = "гранатомёт", enable = true, slot_to_take = 1, short = "гранатомёт" },
	[36] = {
		name = "пусковую установку с наведением",
		enable = true,
		slot_to_take = 1,
		short = "ПУ с навед.",
	},
	[37] = { name = "огнемёт", enable = true, slot_to_take = 1, short = "огнемёт" },
	[38] = { name = "миниган", enable = true, slot_to_take = 1, short = "миниган" },

	[39] = { name = "заряд C4", enable = true, slot_to_take = 6, short = "C4" },
	[40] = { name = "детонатор", enable = true, slot_to_take = 2, short = "детонатор" },

	[41] = { name = "перцовый баллончик", enable = true, slot_to_take = 3, short = "баллончик" },
	[42] = { name = "огнетушитель", enable = true, slot_to_take = 1, short = "огнетушитель" },
	[43] = { name = "фотоаппарат", enable = true, slot_to_take = 8, short = "фотоаппарат" },
	[44] = { name = "прибор ночного видения", enable = true, slot_to_take = 8, short = "ПНВ" },
	[45] = { name = "тепловизор", enable = true, slot_to_take = 8, short = "тепловизор" },
	[46] = { name = "парашют", enable = true, slot_to_take = 1, short = "парашют" },

	-- причины урона — выключены
	[49] = { name = "автомобиль", enable = false, slot_to_take = 1, short = "автомобиль" },
	[50] = {
		name = "лопасти вертолёта",
		enable = false,
		slot_to_take = 1,
		short = "лопасти вертолёта",
	},
	[51] = { name = "бомбу", enable = false, slot_to_take = 1, short = "бомбу" },
	[54] = { name = "коллизию", enable = false, slot_to_take = 1, short = "коллизию" },

	-- ARZ кастом
	[71] = { name = "пистолет Desert Eagle Steel", enable = true, slot_to_take = 4, short = "Deagle Steel" },
	[72] = { name = "пистолет Desert Eagle Gold", enable = true, slot_to_take = 4, short = "Deagle Gold" },
	[73] = { name = "пистолет Glock Gradient", enable = true, slot_to_take = 4, short = "Glock" },
	[74] = { name = "пистолет Desert Eagle Flame", enable = true, slot_to_take = 4, short = "Deagle Flame" },
	[75] = { name = "револьвер Python Royal", enable = true, slot_to_take = 4, short = "Python R." },
	[76] = { name = "револьвер Python Silver", enable = true, slot_to_take = 4, short = "Python S." },

	[77] = { name = "автомат AK-47 Roses", enable = true, slot_to_take = 7, short = "AK-47 Roses" },
	[78] = { name = "автомат AK-47 Gold", enable = true, slot_to_take = 7, short = "AK-47 Gold" },

	[79] = { name = "пулемёт M249 Graffiti", enable = true, slot_to_take = 7, short = "M249 Graf." },
	[80] = { name = "карабин Сайга (золото)", enable = true, slot_to_take = 7, short = "Сайга Gold" },
	[81] = { name = "пистолет-пулемёт Standard", enable = true, slot_to_take = 7, short = "ПП Std." },
	[82] = { name = "пулемёт M249", enable = true, slot_to_take = 7, short = "M249" },
	[83] = { name = "пистолет-пулемёт Skorpion", enable = true, slot_to_take = 7, short = "Skorpion" },

	[84] = { name = "автомат AKS-74 (камуфляж)", enable = true, slot_to_take = 7, short = "AKS-74 camo" },
	[85] = { name = "автомат AK-47 (камуфляж)", enable = true, slot_to_take = 7, short = "AK-47 camo" },
	[86] = { name = "дробовик Rebecca", enable = true, slot_to_take = 7, short = "Rebecca" },

	[87] = { name = "портальную пушку", enable = true, slot_to_take = 8, short = "портал-пушку" },
	[88] = { name = "ледяной меч", enable = true, slot_to_take = 5, short = "ледяной меч" },
	[89] = { name = "портальную пушку", enable = true, slot_to_take = 8, short = "портал-пушку" },

	[90] = {
		name = "светошумовую гранату",
		enable = true,
		slot_to_take = 6,
		short = "светошум. гранату",
	},
	[91] = {
		name = "ослепляющую гранату",
		enable = true,
		slot_to_take = 6,
		short = "ослепл. гранату",
	},

	[92] = {
		name = "снайперскую винтовку McMillan TAC-50",
		enable = true,
		slot_to_take = 7,
		short = "TAC-50",
	},
	[93] = {
		name = "электрошоковый пистолет",
		enable = true,
		slot_to_take = 4,
		short = "электрошокер",
	},
}

-- ===== очередь /me с паузой =====
local send_queue, send_thr = {}, nil
local function ensure_send_thread()
	if send_thr and send_thr:status() ~= "dead" then
		return
	end
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
				else
					wait(50)
				end
			else
				wait(50)
			end
		end
	end)
end
local function send_chat(line)
	if not line or line == "" then
		return
	end
	ensure_send_thread()
	table.insert(send_queue, line)
end

-- ===== утилиты =====
local function safe_pcall(f, ...)
	if type(f) == "function" then
		pcall(f, ...)
	end
end

local function is_empty_weapon(id)
	if id == -1 or id == 0 then
		return true
	end
	if id == 1 and M.config.ignore_knuckles then
		return true
	end
	return false
end

local slot_to_take_fallback =
	{ [0] = 2, [1] = 1, [2] = 4, [3] = 7, [4] = 7, [5] = 7, [6] = 7, [7] = 1, [8] = 6, [9] = 8, [10] = 8, [11] = 2, [12] = 2 }

local function get_slot_take_for(id)
	local ok, slot = pcall(getWeapontypeSlot, id)
	slot = ok and slot or -1
	if slot == -1 then
		for s, ids in pairs(weaponsBySlot) do
			for _, wid in ipairs(ids) do
				if wid == id then
					slot = s
					break
				end
			end
			if slot ~= -1 then
				break
			end
		end
	end
	return slot_to_take_fallback[slot] or 2
end

local function winfo(id)
	local g = M.config.rp_guns[id]
	if g then
		return g
	end
	g = {
		name = ("оружие %d"):format(id),
		short = ("оружие %d"):format(id),
		enable = true,
		slot_to_take = get_slot_take_for(id),
	}
	return apply_defaults(g)
end

-- длина строки (UTF-8/CP1251 совместимо)
local function str_len(s)
	local ok, utf8 = pcall(require, "lua-utf8")
	if ok and utf8 and utf8.len then
		local ok2, n = pcall(utf8.len, s)
		if ok2 and n then
			return n
		end
	end
	return #s
end

local function pick(t, salt)
	local n = #t
	if n == 0 then
		return nil
	end
	local x = math.floor((os.clock() * 1000 + (salt or 0))) % n + 1
	if M.config.flavor_level == 1 then
		x = ((salt or 0) % n) + 1
	end
	return t[x]
end

-- конструкторы фраз с опциями
local function place_phrase(g, is_from, compact)
	local src = compact and g.rpTakeNamesShort or g.rpTakeNames
	local p = src and src[is_from and 1 or 2] or ""
	return p
end

local function verb_for(g, kind, plain)
	local vm = g.verb_map or {}
	local set = (kind == "show") and (vm.show or { "достал" }) or (vm.hide or { "убрал" })
	if plain then
		return set[1] or "достал"
	end
	return pick(set, (kind == "show") and 17 or 23) or set[1] or "достал"
end

local function adv_for(g, kind, with_adv)
	if not with_adv or M.config.flavor_level < 2 then
		return ""
	end
	local list = (kind == "show") and g.adv_show or g.adv_hide
	local a = list and pick(list, (kind == "show") and 7 or 13)
	return a and (a .. " ") or ""
end

local function gun_name(g, short)
	return (short and g.short) or g.name
end

local function build_shown(id, opts)
	local g = winfo(id)
	if g.enable == false then
		return nil
	end
	local v = verb_for(g, "show", opts.plain_verbs)
	local a = adv_for(g, "show", opts.adverbs)
	local place = place_phrase(g, true, opts.short_place)
	local name = gun_name(g, opts.short_names)
	return ("%s %s%s %s %s"):format(M.config.prefix, a, v, place, name)
end

local function build_hidden(id, opts)
	local g = winfo(id)
	if g.enable == false then
		return nil
	end
	local v = verb_for(g, "hide", opts.plain_verbs)
	local a = adv_for(g, "hide", opts.adverbs)
	local place = place_phrase(g, false, opts.short_place)
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
		adverbs = (level <= 1),
		plain_verbs = (level >= 2),
		short_names = (level >= 3),
		short_place = (level >= 4),
	}
	local gn = newId and winfo(newId) or nil
	local go = oldId and winfo(oldId) or nil
	local base = gn or go or {}
	local conn_set = (level >= 1) and (base.connectors_short or {}) or (base.connectors_full or {})
	local connector = pick(conn_set, (newId or 0) + (oldId or 0)) or ", затем "

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
		if line and str_len(line) <= M.config.max_len then
			return line
		end
	end
	-- радикальная компактность: без места ношения
	local function compact_no_place(kind2, nid, oid)
		local opts = { adverbs = false, plain_verbs = true, short_names = true, short_place = true }
		if kind2 == "shown" then
			local g = winfo(nid)
			if g.enable == false then
				return nil
			end
			return ("%s достал %s"):format(M.config.prefix, gun_name(g, true))
		elseif kind2 == "hidden" then
			local g = winfo(oid)
			if g.enable == false then
				return nil
			end
			return ("%s убрал %s"):format(M.config.prefix, gun_name(g, true))
		else
			local go, gn = winfo(oid), winfo(nid)
			if (go.enable == false) and (gn.enable == false) then
				return nil
			end
			local conn = ", затем "
			local part1 = go.enable ~= false and ("убрал " .. gun_name(go, true)) or nil
			local part2 = gn.enable ~= false and ("достал " .. gun_name(gn, true)) or nil
			if part1 and part2 then
				return ("%s %s%s%s"):format(M.config.prefix, part1, conn, part2)
			end
			return ("%s %s"):format(M.config.prefix, part1 or part2 or "")
		end
	end
	line = compact_no_place(kind, newId, oldId)
	if line and str_len(line) <= M.config.max_len then
		return line
	end

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
	if M.config.auto_mode == 0 then
		local line = M.makeRpLine(kind, newW, oldW)
		if line and line ~= "" then
			send_chat(line)
		end
	else
		if not pending_old then
			pending_old = oldW
		end
		pending_new = newW
	end
end

local function fire(kind, newW, oldW)
	safe_pcall(cb_any, kind, newW, oldW)
	if kind == "shown" then
		safe_pcall(cb_show, newW, oldW)
	elseif kind == "hidden" then
		safe_pcall(cb_hide, newW, oldW)
	elseif kind == "changed" then
		safe_pcall(cb_change, newW, oldW)
	end
	auto_rp(kind, newW, oldW)
end

-- ===== детектор =====
local function update_once()
	if M.config.auto_mode == 1 and pending_new and type(isButtonPressed) == "function" and isButtonPressed(0, 6) then
		local oldW, newW = pending_old or -1, pending_new
		local kind
		if is_empty_weapon(oldW) and not is_empty_weapon(newW) then
			kind = "shown"
		elseif not is_empty_weapon(oldW) and is_empty_weapon(newW) then
			kind = "hidden"
		elseif oldW ~= newW then
			kind = "changed"
		end
		if kind then
			local line = M.makeRpLine(kind, newW, oldW)
			if line and line ~= "" then
				send_chat(line)
			end
		end
		pending_old, pending_new = nil, nil
	end
	local ped = PLAYER_PED
	if not ped then
		return
	end
	local w = getCurrentCharWeapon(ped)
	if w == nil then
		return
	end

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
	if running then
		return
	end
	running = true
	prev_weapon, candidate_weapon, stable_count, cooldown = 0, 0, 0, 0
	pending_old, pending_new = nil, nil
	if interval_ms then
		M.config.tick_ms = interval_ms
	end
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
	pending_old, pending_new = nil, nil
	if thr and thr:status() ~= "dead" then
		thr:terminate()
	end
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
	if imgui.IsItemHovered() then
		imgui.SetTooltip(text)
	end
end

local auto_mode_labels = { "Авто /me", "По ПКМ" }
local auto_mode_labels_ffi = imgui.new["const char*"][#auto_mode_labels](auto_mode_labels)

local weapon_list = {
	{ 0, "Fist" },
	{ 1, "Brass Knuckles" },
	{ 2, "Golf Club" },
	{ 3, "Nightstick" },
	{ 4, "Knife" },
	{ 5, "Baseball Bat" },
	{ 6, "Shovel" },
	{ 7, "Pool Cue" },
	{ 8, "Katana" },
	{ 9, "Chainsaw" },
	{ 10, "Purple Dildo" },
	{ 11, "Dildo" },
	{ 12, "Vibrator" },
	{ 13, "Silver Vibrator" },
	{ 14, "Flowers" },
	{ 15, "Cane" },
	{ 16, "Grenade" },
	{ 17, "Tear Gas" },
	{ 18, "Molotov Cocktail" },
	{ 22, "9mm" },
	{ 23, "Silenced 9mm" },
	{ 24, "Desert Eagle" },
	{ 25, "Shotgun" },
	{ 26, "Sawnoff Shotgun" },
	{ 27, "Combat Shotgun" },
	{ 28, "Micro Uzi" },
	{ 29, "MP5" },
	{ 30, "AK-47" },
	{ 31, "M4" },
	{ 32, "Tec-9" },
	{ 33, "Country Rifle" },
	{ 34, "Sniper Rifle" },
	{ 35, "RPG" },
	{ 36, "HS Rocket" },
	{ 37, "Flamethrower" },
	{ 38, "Minigun" },
	{ 39, "Satchel Charge" },
	{ 40, "Detonator" },
	{ 41, "Spraycan" },
	{ 42, "Fire Extinguisher" },
	{ 43, "Camera" },
	{ 44, "Night Vis Goggles" },
	{ 45, "Thermal Goggles" },
	{ 46, "Parachute" },
}
local weapon_ids = {}

-- позиция в спрайт-листе -> id оружия ("?" и "ADD" для кастомных)
local weaponsID = {
	0,
	1,
	2,
	3,
	4,
	5,
	6,
	7,
	8,
	9,
	22,
	23,
	24,
	25,
	26,
	27,
	28,
	29,
	32,
	30,
	31,
	33,
	34,
	35,
	36,
	37,
	38,
	16,
	17,
	18,
	39,
	41,
	42,
	43,
	10,
	11,
	12,
	13,
	14,
	15,
	44,
	45,
	46,
	40,
	"?",
	"ADD",
}

local sprite_idx_by_weapon = {}
local unknown_sprite_idx = 1
for idx, id in ipairs(weaponsID) do
	if id == "?" then
		unknown_sprite_idx = idx
	elseif type(id) == "number" then
		sprite_idx_by_weapon[id] = idx
	end
end

-- стандартное распределение ID по слотам
local weaponsBySlot = {
	[0] = { 0, 1 },
	[1] = { 2, 3, 4, 5, 6, 7, 8, 9 },
	[2] = { 22, 23, 24 },
	[3] = { 25, 26, 27 },
	[4] = { 28, 29, 32 },
	[5] = { 30, 31 },
	[6] = { 33, 34 },
	[7] = { 35, 36, 37, 38 },
	[8] = { 16, 17, 18, 39 },
	[9] = { 41, 42, 43 },
	[10] = { 10, 11, 12, 13, 14, 15 },
	[11] = { 44, 45, 46 },
	[12] = { 40 },
}
function rebuild_weapon_lists()
	weapon_ids = {}
	local seen = {}
	for i, w in ipairs(weapon_list) do
		weapon_ids[#weapon_ids + 1] = w[1]
		seen[w[1]] = true
	end
	for id, _ in pairs(M.config.rp_guns or {}) do
		if not seen[id] then
			weapon_ids[#weapon_ids + 1] = id
		end
	end
end
rebuild_weapon_lists()

local function drawWeaponEditPopup(id)
	local popup = "weapon_edit_" .. id
	local g = M.config.rp_guns[id] or { name = "", enable = true, slot_to_take = 2, short = "" }
	apply_defaults(g)
	if imgui.BeginPopup(popup) then
		local gname = imgui.new.char[64](g.name or "")
		if imgui.InputText("Полное имя", gname, ffi.sizeof(gname)) then
			g.name = ffi.string(gname)
			M.config.rp_guns[id] = g
			save_cfg()
			rebuild_weapon_lists()
		end
		tooltip("Название оружия")
		local gshort = imgui.new.char[32](g.short or "")
		if imgui.InputText("Короткое имя", gshort, ffi.sizeof(gshort)) then
			g.short = ffi.string(gshort)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Сокращённое название")
		local grp = ffi.new("int[1]", g.slot_to_take or 2)
		if imgui.InputInt("Место ношения", grp) then
			g.slot_to_take = grp[0]
			apply_defaults(g)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Индекс места ношения")
		local from_full = imgui.new.char[32]((g.rpTakeNames and g.rpTakeNames[1]) or "")
		if imgui.InputText("Откуда", from_full, ffi.sizeof(from_full)) then
			g.rpTakeNames = g.rpTakeNames or { "", "" }
			g.rpTakeNames[1] = ffi.string(from_full)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Фраза при доставании")
		local to_full = imgui.new.char[32]((g.rpTakeNames and g.rpTakeNames[2]) or "")
		if imgui.InputText("Куда", to_full, ffi.sizeof(to_full)) then
			g.rpTakeNames = g.rpTakeNames or { "", "" }
			g.rpTakeNames[2] = ffi.string(to_full)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Фраза при убирании")
		local from_short = imgui.new.char[32]((g.rpTakeNamesShort and g.rpTakeNamesShort[1]) or "")
		if imgui.InputText("Откуда кратко", from_short, ffi.sizeof(from_short)) then
			g.rpTakeNamesShort = g.rpTakeNamesShort or { "", "" }
			g.rpTakeNamesShort[1] = ffi.string(from_short)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Краткая фраза откуда")
		local to_short = imgui.new.char[32]((g.rpTakeNamesShort and g.rpTakeNamesShort[2]) or "")
		if imgui.InputText("Куда кратко", to_short, ffi.sizeof(to_short)) then
			g.rpTakeNamesShort = g.rpTakeNamesShort or { "", "" }
			g.rpTakeNamesShort[2] = ffi.string(to_short)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Краткая фраза куда")
		local show_verbs = imgui.new.char[128](table.concat(g.verb_map and g.verb_map.show or {}, ","))
		if imgui.InputText("Глаголы достать", show_verbs, ffi.sizeof(show_verbs)) then
			g.verb_map = g.verb_map or {}
			g.verb_map.show = funcs.parseList(ffi.string(show_verbs))
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Синонимы достать")
		local hide_verbs = imgui.new.char[128](table.concat(g.verb_map and g.verb_map.hide or {}, ","))
		if imgui.InputText("Глаголы убрать", hide_verbs, ffi.sizeof(hide_verbs)) then
			g.verb_map = g.verb_map or {}
			g.verb_map.hide = funcs.parseList(ffi.string(hide_verbs))
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Синонимы убрать")
		local adv_show = imgui.new.char[256](table.concat(g.adv_show or {}, "\n"))
		if
			imgui.InputTextMultiline(
				"Наречия достать",
				adv_show,
				ffi.sizeof(adv_show),
				imgui.ImVec2(0, 80)
			)
		then
			g.adv_show = funcs.parseLines(ffi.string(adv_show))
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Наречия при доставании")
		local adv_hide = imgui.new.char[256](table.concat(g.adv_hide or {}, "\n"))
		if
			imgui.InputTextMultiline("Наречия убрать", adv_hide, ffi.sizeof(adv_hide), imgui.ImVec2(0, 80))
		then
			g.adv_hide = funcs.parseLines(ffi.string(adv_hide))
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Наречия при убирании")
		local conn_full = imgui.new.char[256](table.concat(g.connectors_full or {}, "\n"))
		if
			imgui.InputTextMultiline("Соединители", conn_full, ffi.sizeof(conn_full), imgui.ImVec2(0, 80))
		then
			g.connectors_full = funcs.parseLines(ffi.string(conn_full), true)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Полные связки")
		local conn_short = imgui.new.char[256](table.concat(g.connectors_short or {}, "\n"))
		if
			imgui.InputTextMultiline(
				"Соединители короткие",
				conn_short,
				ffi.sizeof(conn_short),
				imgui.ImVec2(0, 80)
			)
		then
			g.connectors_short = funcs.parseLines(ffi.string(conn_short), true)
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Короткие связки")
		local gen = imgui.new.bool(g.enable ~= false)
		if imgui.Checkbox("Включить", gen) then
			g.enable = gen[0]
			M.config.rp_guns[id] = g
			save_cfg()
		end
		tooltip("Использовать эту запись")
		imgui.EndPopup()
	end
end

local function drawWeaponCards()
	local availWidth = imgui.GetContentRegionAvail().x
	-- card is ~1.5x wider than sprite to leave room around the 60x20 image
	local cardWidth, cardHeight = 90, 40 -- 60x20 icon on top + text below
	local spacingX, spacingY = 8, 8
	local columns = math.max(1, math.floor((availWidth + spacingX) / (cardWidth + spacingX)))
	local x0 = imgui.GetCursorScreenPos().x
	local y0 = imgui.GetCursorScreenPos().y

	local cards = {}
	for _, id in ipairs(weapon_ids) do
		local g = M.config.rp_guns[id]
		if g then
			cards[#cards + 1] = { id = id, g = g }
		end
	end

	for n, card in ipairs(cards) do
		local id, g = card.id, card.g
		local x = x0 + (((n - 1) % columns) * (cardWidth + spacingX))
		local y = y0 + (math.floor((n - 1) / columns)) * (cardHeight + spacingY)
		imgui.SetCursorScreenPos(imgui.ImVec2(x, y))
		local pmin = imgui.GetCursorScreenPos()
		local pmax = imgui.ImVec2(pmin.x + cardWidth, pmin.y + cardHeight)
		local hovered = imgui.IsMouseHoveringRect(pmin, pmax)
		local dl = imgui.GetWindowDrawList()
		dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.FrameBg]), 4)
		dl:AddRect(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Border]), 4)
		local spr = sprite_idx_by_weapon[id] or unknown_sprite_idx
		-- draw sprite at top centered horizontally
		imgui.SetCursorScreenPos(imgui.ImVec2(pmin.x + (cardWidth - 60) / 2, pmin.y))
		mimgui_funcs.drawWeaponZoom(mimgui_funcs.weapon_standard, spr, imgui.ImVec2(60, 20), 1.0)
		-- draw label below sprite
		local label = g.short or g.name or ("Weapon " .. id)
		local txt = imgui.CalcTextSize(label)
		local txt_pos = imgui.ImVec2(pmin.x + (cardWidth - txt.x) / 2, pmin.y + 20 + ((cardHeight - 20 - txt.y) / 2))
		dl:AddText(txt_pos, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Text]), label)
		if hovered then
			dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(imgui.ImVec4(0, 0, 0, 0.5)), 4)
			imgui.SetCursorScreenPos(pmin)
			if imgui.Button("Edit##" .. id, imgui.ImVec2(cardWidth, cardHeight)) then
				imgui.OpenPopup("weapon_edit_" .. id)
			end
		end
		drawWeaponEditPopup(id)
	end

	local add_x = x0 + ((#cards % columns) * (cardWidth + spacingX))
	local add_y = y0 + (math.floor(#cards / columns) * (cardHeight + spacingY))
	imgui.SetCursorScreenPos(imgui.ImVec2(add_x, add_y))
	local pmin = imgui.GetCursorScreenPos()
	local pmax = imgui.ImVec2(pmin.x + cardWidth, pmin.y + cardHeight)
	local dl = imgui.GetWindowDrawList()
	dl:AddRectFilled(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.FrameBg]), 4)
	dl:AddRect(pmin, pmax, imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Border]), 4)
	imgui.SetCursorScreenPos(pmin)
	if imgui.Button("+##add_weapon", imgui.ImVec2(cardWidth, cardHeight)) then
		imgui.OpenPopup("weapon_add_popup")
	end

	if imgui.BeginPopup("weapon_add_popup") then
		M._new_wid = M._new_wid or imgui.new.int(0)
		M._new_wname = M._new_wname or imgui.new.char[64]()
		imgui.InputInt("ID", M._new_wid)
		imgui.InputText("Имя", M._new_wname, ffi.sizeof(M._new_wname))
		if imgui.Button("OK") then
			local nid = M._new_wid[0]
			local nname = ffi.string(M._new_wname)
			if nname ~= "" then
				local gnew = M.config.rp_guns[nid] or { enable = true, slot_to_take = get_slot_take_for(nid) }
				gnew.name = nname
				gnew.short = gnew.short or nname
				apply_defaults(gnew)
				M.config.rp_guns[nid] = gnew
				rebuild_weapon_lists()
				save_cfg()
			end
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

function M.DrawSettingsInline()
	local run = imgui.new.bool(running)
	if imgui.Checkbox("Включить модуль", run) then
		if run[0] then
			M.start()
		else
			M.stop()
		end
	end
	tooltip("Запускает или останавливает отслеживание смены оружия")

	local mode = ffi.new("int[1]", M.config.auto_mode)
	if imgui.Combo("Режим /me", mode, auto_mode_labels_ffi, #auto_mode_labels) then
		M.config.auto_mode = mode[0]
		save_cfg()
	end
	tooltip("Способ отправки RP: автоматически или по нажатию ПКМ")

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

	local flavor = ffi.new("int[1]", M.config.flavor_level)
	if imgui.SliderInt("Уровень разнообразия", flavor, 1, 3) then
		M.setFlavorLevel(flavor[0])
		save_cfg()
	end
        tooltip("1 — минимум украшений, 3 — максимум разнообразия")

        imgui.Separator()
        if imgui.Button("Редактор пресетов") then
                preset_editor_open[0] = true
        end
        if preset_editor_open[0] then
                M.DrawPresetEditor()
        end

        if imgui.CollapsingHeader("Дополнительно") then
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
		tooltip(
			"Сколько кадров оружие должно быть одинаковым для фиксации"
		)

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

		imgui.Separator()
                if imgui.CollapsingHeader("Оружие") then
                        drawWeaponCards()
                end
        end
end

function M.DrawPresetEditor()
        mimgui_funcs.SciFiTheme()
        imgui.SetNextWindowSize(imgui.ImVec2(620, 480), imgui.Cond.Once)
        if not imgui.Begin("RP Preset Editor", preset_editor_open, imgui.WindowFlags.NoCollapse) then
                imgui.End()
                return
        end

        local presets = M.config.presets
        if #presets == 0 then presets[1] = default_preset() end
        local active = M.config.active_preset or 1
        active = math.max(1, math.min(active, #presets))
        local preset = presets[active]

        local names = {}
        for i, p in ipairs(presets) do
                names[#names + 1] = (p.full_name ~= "" and p.full_name) or ("Preset " .. i)
        end
        -- build ffi array of preset names for ImGui Combo
        local names_ffi = imgui.new["const char*"][#names](names)
        local idx = ffi.new("int[1]", active - 1)
        if imgui.Combo("Presets", idx, names_ffi, #presets) then
                active = idx[0] + 1
                M.config.active_preset = active
                preset = presets[active]
        end
        imgui.SameLine()
        imgui.TextDisabled("(?)")
        if imgui.IsItemHovered() then imgui.SetTooltip("Редактор пресетов RP-фраз") end

        imgui.Columns(2, nil, false)
        local total_w = imgui.GetWindowWidth()
        imgui.SetColumnWidth(0, total_w * 0.6)
        local start_pos = imgui.GetCursorScreenPos()
        local x = start_pos.x + imgui.GetColumnWidth(0)
        local y0 = imgui.GetWindowPos().y + imgui.GetStyle().WindowPadding.y
        local y1 = imgui.GetWindowPos().y + imgui.GetWindowHeight() - imgui.GetStyle().WindowPadding.y
        imgui.GetWindowDrawList():AddLine(imgui.ImVec2(x, y0), imgui.ImVec2(x, y1), imgui.GetColorU32Vec4(imgui.ImVec4(0.2, 0.4, 0.6, 0.4)))

        -- Left column
        imgui.BeginChild("builder", imgui.ImVec2(0, 0), false)
        imgui.Text("Предмет")
        imgui.PushItemWidth(-1)
        local buf_full = ffi.new("char[64]", preset.full_name or "")
        if imgui.InputText("##full", buf_full, ffi.sizeof(buf_full)) then
                preset.full_name = ffi.string(buf_full)
        end
        imgui.SameLine()
        imgui.SetNextItemWidth(120)
        local buf_short = ffi.new("char[32]", preset.short_name or "")
        if imgui.InputText("##short", buf_short, ffi.sizeof(buf_short)) then
                preset.short_name = ffi.string(buf_short)
        end
        imgui.PopItemWidth()

        imgui.Separator()
        imgui.Text("Действия")
        for _, v in ipairs(preset.actions or {}) do
                local sel = preset.selected_actions and preset.selected_actions[v]
                if draw_pill(v, sel) then
                        preset.selected_actions = preset.selected_actions or {}
                        if sel then
                                preset.selected_actions[v] = nil
                        else
                                preset.selected_actions[v] = true
                        end
                end
                imgui.SameLine()
        end
        imgui.NewLine()

        imgui.Text("Наречия достать")
        for _, v in ipairs(preset.adv_show or {}) do
                local sel = preset.selected_adv_show and preset.selected_adv_show[v]
                if draw_pill(v, sel) then
                        preset.selected_adv_show = preset.selected_adv_show or {}
                        if sel then
                                preset.selected_adv_show[v] = nil
                        else
                                preset.selected_adv_show[v] = true
                        end
                end
                imgui.SameLine()
        end
        imgui.NewLine()

        imgui.Text("Наречия убрать")
        for _, v in ipairs(preset.adv_hide or {}) do
                local sel = preset.selected_adv_hide and preset.selected_adv_hide[v]
                if draw_pill(v, sel) then
                        preset.selected_adv_hide = preset.selected_adv_hide or {}
                        if sel then
                                preset.selected_adv_hide[v] = nil
                        else
                                preset.selected_adv_hide[v] = true
                        end
                end
                imgui.SameLine()
        end
        imgui.NewLine()

        imgui.Text("Соединители")
        for _, v in ipairs(preset.connectors or {}) do
                local sel = preset.selected_connectors and preset.selected_connectors[v]
                if draw_pill(v, sel) then
                        preset.selected_connectors = preset.selected_connectors or {}
                        if sel then
                                preset.selected_connectors[v] = nil
                        else
                                preset.selected_connectors[v] = true
                        end
                end
                imgui.SameLine()
        end
        imgui.NewLine()
        imgui.EndChild()

        imgui.NextColumn()

        -- Right column
        imgui.BeginChild("preview", imgui.ImVec2(0, 0), false)
        imgui.Text("Превью")
        imgui.BeginChild("preview_box", imgui.ImVec2(0, 120), true)
        imgui.TextWrapped(build_preview(preset))
        imgui.EndChild()
        imgui.Separator()
        local enable = imgui.new.bool(preset.enable ~= false)
        if imgui.Checkbox("Включить", enable) then
                preset.enable = enable[0]
        end
        imgui.SameLine()
        if imgui.Button("Скопировать") then
                imgui.SetClipboardText(build_preview(preset))
        end
        imgui.Separator()
        if imgui.Button("Сохранить пресет", imgui.ImVec2(-1, 0)) then
                save_cfg()
        end
        if imgui.Button("Сброс", imgui.ImVec2(-1, 0)) then
                presets[active] = default_preset()
        end
        imgui.EndChild()

        imgui.Columns(1)
        imgui.End()
end

load_cfg()

return M
