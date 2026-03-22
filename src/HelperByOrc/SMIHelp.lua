local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local SMIHelp = {}

-- ========= ЗАВИСИМОСТИ =========
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local ffi = require("ffi")
local imgui = require("mimgui")
local paths = require("HelperByOrc.paths")
local mimgui_funcs
local new = imgui.new

local str = ffi.string
local sizeof = ffi.sizeof

local bit = require("bit") -- для UTF-8 разборки и флагов
local vk = require("vkeys")
local ok_fa, fa = pcall(require, "fAwesome7")
if not ok_fa or type(fa) ~= "table" then
	fa = setmetatable({}, {
		__index = function()
			return ""
		end,
	})
end

local floor = math.floor
local min = math.min
local max = math.max

local ImVec2 = imgui.ImVec2
local ImVec4 = imgui.ImVec4
local InputTextFlags = imgui.InputTextFlags
local StyleVar = imgui.StyleVar
local Col = imgui.Col
local WindowFlags = imgui.WindowFlags
local bit_bor = bit.bor

-- опциональные зависимости (совместимость/сейв конфига)
local funcs
local SMILive
local correct_module
local samp_module
local trim
local imgui_text_safe
local imgui_text_wrapped_safe
local imgui_text_colored_safe
local imgui_bullet_text_safe
local imgui_set_tooltip_safe
local utf8_len
local tolower_utf8

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
	SMILive = mod.SMILive or SMILive
	correct_module = mod.correct or correct_module or require("HelperByOrc.correct")
	samp_module = mod.samp or samp_module

	trim = funcs.trim
	utf8_len = funcs.utf8_len
	tolower_utf8 = funcs.string_lower

	imgui_text_safe = mimgui_funcs.imgui_text_safe
	imgui_text_wrapped_safe = mimgui_funcs.imgui_text_wrapped_safe
	imgui_text_colored_safe = mimgui_funcs.imgui_text_colored_safe
	imgui_bullet_text_safe = mimgui_funcs.imgui_bullet_text_safe
	imgui_set_tooltip_safe = mimgui_funcs.imgui_set_tooltip_safe
end

local config_manager_ref
local event_bus_ref

syncDependencies()

-- ========= КОНСТАНТЫ/НАСТРОЙКИ =========
local CONFIG_PATH_REL = "SMIHelp.json"

local INPUT_MAX = 80 -- жёсткий лимит символов UTF-8
local LIMIT_WARN_RATIO = 0.90 -- порог предупреждения 90%

local SECTION_H = 0 -- высота секции конструктора
local BTN_H = 24 -- высота кнопок сеток
local TYPEW_BASE, TYPEW_MIN = 150, 110
local PRICEW_BASE, PRICEW_MIN = 165, 120
local OBJ_BTN_MIN_W = 88
local KBD_BTN_MIN_W = 56
local PANEL_PAD = 4
local OBJ_PANEL_TRIM = 115
local SIDE_PANEL_RATIO = 0.26
local RIGHT_PANEL_WIDTH_MULT = 1.25
local RIGHT_PANEL_W_COLLAPSED = 0
local NUMPAD = { "1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0" }

local OBJECTS_DEFAULT = {
	{ L("smi_help.text.text"), L("smi_help.text.text_1") },
	{ L("smi_help.text.text_2"), L("smi_help.text.text_3") },
	{ L("smi_help.text.text_4"), L("smi_help.text.text_5") },
	{ L("smi_help.text.text_6"), L("smi_help.text.text_7") },
	{ L("smi_help.text.text_8"), L("smi_help.text.text_9") },
	{ L("smi_help.text.text_10"), L("smi_help.text.text_11") },
	{ L("smi_help.text.text_12"), L("smi_help.text.text_13") },
	{ L("smi_help.text.text_14"), L("smi_help.text.text_15") },
	{ L("smi_help.text.text_16"), L("smi_help.text.text_17") },
	{ L("smi_help.text.text_18"), L("smi_help.text.text_19") },
	{ L("smi_help.text.text_20"), L("smi_help.text.text_21") },
	{ L("smi_help.text.text_22"), L("smi_help.text.text_23") },
	{ L("smi_help.text.text_24"), L("smi_help.text.text_25") },
	{ L("smi_help.text.text_26"), L("smi_help.text.text_27") },
	{ L("smi_help.text.text_28"), L("smi_help.text.text_29") },
	{ L("smi_help.text.text_30"), L("smi_help.text.text_31") },
	{ L("smi_help.text.text_32"), L("smi_help.text.text_33") },
	{ L("smi_help.text.text_34"), L("smi_help.text.text_35") },
	{ L("smi_help.text.text_36"), L("smi_help.text.text_37") },
	{ L("smi_help.text.text_38"), L("smi_help.text.text_39") },
}

local SPELLER_DEBOUNCE_SEC = 0.5 -- дребезг запуска автокорректора

-- ========= УТИЛИТЫ =========
local function safe(s)
	return s or ""
end

-- ========= UTF-8: длина и обрезка по символам =========

local function utf8_truncate(s, max_chars)
	s = tostring(s or "")
	if max_chars <= 0 then
		return ""
	end
	local len = #s
	local i, n = 1, 0
	while i <= len do
		if n == max_chars then
			return s:sub(1, i - 1)
		end
		local c = s:byte(i)
		if c < 0x80 then
			i = i + 1
		elseif c < 0xE0 then
			i = i + 2
		elseif c < 0xF0 then
			i = i + 3
		else
			i = i + 4
		end
		n = n + 1
	end
	return s
end

local function clamp80(s)
	return utf8_truncate(tostring(s or ""), INPUT_MAX)
end

-- ========= КОНФИГ =========
local Config = { data = {} }

local function ensure_price_type_map(map, types)
	if type(map) ~= "table" then
		map = {}
	end
	for _, type_name in ipairs(types or {}) do
		if map[type_name] == nil then
			map[type_name] = "both"
		end
	end
	return map
end

function Config:load()
	local t = funcs.loadTableFromJson(CONFIG_PATH_REL, {})
	if type(t) ~= "table" then
		t = {}
	end

	local function ensure_table(key, def)
		if type(t[key]) ~= "table" then
			t[key] = def
		end
	end

	t.type_buttons = (type(t.type_buttons) == "table" and t.type_buttons)
		or {
			L("smi_help.text.text_40"),
			L("smi_help.text.text_41"),
			L("smi_help.text.text_42"),
			L("smi_help.text.text_43"),
			L("smi_help.text.text_44"),
			L("smi_help.text.text_45"),
			L("smi_help.text.text_46"),
			L("smi_help.text.text_47"),
		}
	t.objects = (type(t.objects) == "table" and t.objects) or OBJECTS_DEFAULT
	-- миграция старого формата (строки → пары)
	do
		local migrated = {}
		for _, v in ipairs(t.objects) do
			if type(v) == "string" then
				migrated[#migrated + 1] = { v, v }
			elseif type(v) == "table" then
				migrated[#migrated + 1] = v
			end
		end
		t.objects = migrated
	end
	local prices_default = {
		L("smi_help.text.text_48"),
		L("smi_help.text.text_49"),
		L("smi_help.text.text_50"),
		L("smi_help.text.text_51"),
		L("smi_help.text.text_52"),
		L("smi_help.text.text_53"),
	}
	local prices_buy_default = { L("smi_help.text.text_49"), L("smi_help.text.text_51"), L("smi_help.text.text_53") }
	local prices_sell_default = { L("smi_help.text.text_48"), L("smi_help.text.text_50"), L("smi_help.text.text_52") }
	local function merge_prices_for_load(list_a, list_b)
		local merged = {}
		local seen = {}
		local function push(list)
			for i = 1, #list do
				local item = list[i]
				if item and not seen[item] then
					merged[#merged + 1] = item
					seen[item] = true
				end
			end
		end
		push(list_a or {})
		push(list_b or {})
		if #merged == 0 then
			push(prices_default)
		end
		return merged
	end
	t.prices_buy = (type(t.prices_buy) == "table" and t.prices_buy) or prices_buy_default
	t.prices_sell = (type(t.prices_sell) == "table" and t.prices_sell) or prices_sell_default
	t.prices = merge_prices_for_load(t.prices_buy, t.prices_sell)
	t.currencies = (type(t.currencies) == "table" and t.currencies)
		or { "$", L("smi_help.text.text_54"), L("smi_help.text.text_55"), L("smi_help.text.text_56"), L("smi_help.text.text_57"), L("smi_help.text.text_58") }
	t.addons = (type(t.addons) == "table" and t.addons)
		or { L("smi_help.text.text_59"), L("smi_help.text.text_60"), L("smi_help.text.text_61"), L("smi_help.text.text_62") }

	ensure_table("templates", {
		{
			category = L("smi_help.text.text_63"),
			text = L("smi_help.text.text_64"),
		},
		{
			category = L("smi_help.text.text_65"),
			texts = {
				{
					L("smi_help.text.text_66"),
					L("smi_help.text.text_67"),
					L("smi_help.text.text_68"),
				},
				{ L("smi_help.text.text_69") },
			},
		},
	})

	ensure_table("history", {})
	t.history_limit = (type(t.history_limit) == "number" and t.history_limit) or 100

	local default_price_type_map = {
		[L("smi_help.text.text_40")] = "buy",
		[L("smi_help.text.text_41")] = "sell",
		[L("smi_help.text.text_42")] = "buy",
		[L("smi_help.text.text_43")] = "sell",
		[L("smi_help.text.text_44")] = "buy",
		[L("smi_help.text.text_45")] = "both",
		[L("smi_help.text.text_46")] = "sell",
		[L("smi_help.text.text_47")] = "buy",
	}
	ensure_table("price_type_map", default_price_type_map)
	t.price_type_map = ensure_price_type_map(t.price_type_map, t.type_buttons)

	ensure_table("autocorrect", {
		{ L("smi_help.text.text_70"), "TwinTurbo" },
		{ L("smi_help.text.text_71"), L("smi_help.text.text_72") },
		{ L("smi_help.text.text_73"), L("smi_help.text.text_74") },
	})

	-- память по никнеймам (MRU)
	ensure_table("nick_memory", {}) -- формат: nick -> { last_incoming=string, last_sent=string, ts=number }
	t.nick_memory_limit = (type(t.nick_memory_limit) == "number" and t.nick_memory_limit) or 100
	if type(t.nick_memory._order) ~= "table" then
		t.nick_memory._order = {}
	end -- порядок MRU

	-- режим вставки объектов
	if type(t.objects_use_full) ~= "boolean" then
		t.objects_use_full = true -- по умолчанию: автоматически (полное если влезает)
	end

	-- настройки таймеров
	if type(t.vip_timer_enabled) ~= "boolean" then
		t.vip_timer_enabled = true
	end
	t.vip_timer_delay = (type(t.vip_timer_delay) == "number" and t.vip_timer_delay) or 10

	if type(t.btn_timer_enabled) ~= "boolean" then
		t.btn_timer_enabled = true
	end
	t.btn_timer_delay = (type(t.btn_timer_delay) == "number" and t.btn_timer_delay) or 3

	self.data = t
end

function Config:save()
	if config_manager_ref then
		config_manager_ref.markDirty("SMIHelp")
	else
		local data = self.data or {}
		funcs.saveTableToJson(data, CONFIG_PATH_REL)
	end
end

local function add_to_history(s)
	local t = Config.data
	if type(s) ~= "string" then
		return
	end
	s = trim(s)
	if s == "" then
		return
	end
	for i = #t.history, 1, -1 do
		if t.history[i] == s then
			table.remove(t.history, i)
		end
	end
	table.insert(t.history, 1, s)
	while #t.history > (t.history_limit or 100) do
		table.remove(t.history)
	end
	Config:save()
end
SMIHelp.AddToHistory = add_to_history

Config:load()

-- ========= Память по никнеймам: MRU 100 =========
local function nickmem_get_mem()
	local mem = Config.data.nick_memory or {}
	if type(mem._order) ~= "table" then
		mem._order = {}
	end
	Config.data.nick_memory = mem
	return mem
end

local function nickmem_touch(mem, nick)
	local order = mem._order
	for i = #order, 1, -1 do
		if order[i] == nick then
			table.remove(order, i)
			break
		end
	end
	table.insert(order, 1, nick)
end

local function nickmem_trim(mem, limit)
	limit = limit or (Config.data.nick_memory_limit or 100)
	local order = mem._order
	while #order > limit do
		local victim = table.remove(order) -- из хвоста
		mem[victim] = nil
	end
end

local function nickmem_save(nick, incoming, sent)
	if not nick or nick == "" then
		return
	end
	local mem = nickmem_get_mem()
	mem[nick] = {
		last_incoming = trim(incoming or ""),
		last_sent = clamp80(sent or ""),
		ts = os.time(),
	}
	nickmem_touch(mem, nick)
	nickmem_trim(mem)
	Config:save()
end

-- ========= ПУБЛИЧНЫЕ ПОЛЯ (совместимость) =========
SMIHelp.timer_send = true
SMIHelp.timer_send_enabled = Config.data.vip_timer_enabled
SMIHelp.timer_send_delay = Config.data.vip_timer_delay or 10
SMIHelp.timer_send_clock = os.clock()

SMIHelp.btn_timer = true
SMIHelp.btn_timer_enabled = Config.data.btn_timer_enabled
SMIHelp.btn_timer_delay = Config.data.btn_timer_delay or 3
SMIHelp.btn_timer_clock = os.clock()

SMIHelp.timer_news = true
SMIHelp.timer_news_delay = 4.5
SMIHelp.timer_news_clock = os.clock()

SMIHelp.left_w_smi = new.bool(false)
SMIHelp.right_w_smi = new.bool(false)
SMIHelp.up_w_smi = new.bool(false)
SMIHelp.down_w_smi = new.bool(false)
SMIHelp.acticve_redall = false
SMIHelp.pasr_find = "ALL"
SMIHelp.filter_SMI = imgui.ImGuiTextFilter()

-- ========= ГЛОБАЛЬНЫЙ UI-ФОНТ =========
local ctx -- forward-declaration для доступа из OnInitialize
local bigFont = nil
local _imguiSubs = {}
_imguiSubs[#_imguiSubs + 1] = imgui.OnInitialize(function()
	local io = imgui.GetIO()
	bigFont = io.Fonts:AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 18.0, nil, io.Fonts:GetGlyphRangesCyrillic())
	ctx.bigFont = bigFont
end)

-- ========= ЛОКАЛЬНОЕ СОСТОЯНИЕ =========
local State = {
	show_dialog = new.bool(false),
	edit_buf = new.char[1024](),
	filter_buf = imgui.new.char[128](),
	last_dialog_id = nil,
	last_dialog_title = "",
	last_dialog_text = "",
	original_ad_text = "",

	-- мета об объявлении
	sender_nick = "",
	auto_memory_used = false,

	selected_category = L("smi_help.text.text_75"),

	want_place_cursor = false,
	want_focus_input = false,
	collapse_selection_after_focus = false,
	cursor_action = nil, -- 'to_next_quote' | 'to_end' | 'to_first_empty_quotes' | 'to_addon_end'
	cursor_action_data = nil,
	hist_index = nil,
	last_edit_text = "",

	win_pos = ImVec2(100, 100),
	win_size = ImVec2(1280, 650),
	compact_applied = false,
}
local right_panel_collapsed = new.bool(true)

local function history_reset_index()
	State.hist_index = #(Config.data.history or {}) + 1
end

-- ========= CTX и ПОДМОДУЛИ =========
ctx = {
	SMIHelp = SMIHelp,
	Config = Config,
	State = State,
	-- зависимости (обновляются в syncDependencies/updateCtx)
	funcs = funcs,
	mimgui_funcs = mimgui_funcs,
	correct_module = correct_module,
	SMILive = SMILive,
	samp_module = samp_module,
	-- кэшированные хелперы
	trim = trim,
	utf8_len = utf8_len,
	tolower_utf8 = tolower_utf8,
	imgui_text_safe = imgui_text_safe,
	imgui_text_wrapped_safe = imgui_text_wrapped_safe,
	imgui_text_colored_safe = imgui_text_colored_safe,
	imgui_bullet_text_safe = imgui_bullet_text_safe,
	imgui_set_tooltip_safe = imgui_set_tooltip_safe,
	-- утилиты
	safe = safe,
	clamp80 = clamp80,
	utf8_truncate = utf8_truncate,
	-- общие функции
	add_to_history = add_to_history,
	history_reset_index = history_reset_index,
	nickmem_get_mem = nickmem_get_mem,
	nickmem_save = nickmem_save,
	ensure_price_type_map = ensure_price_type_map,
	-- UI состояние
	bigFont = bigFont,
	right_panel_collapsed = right_panel_collapsed,
	-- константы
	INPUT_MAX = INPUT_MAX,
	LIMIT_WARN_RATIO = LIMIT_WARN_RATIO,
	SECTION_H = SECTION_H,
	BTN_H = BTN_H,
	TYPEW_BASE = TYPEW_BASE,
	TYPEW_MIN = TYPEW_MIN,
	PRICEW_BASE = PRICEW_BASE,
	PRICEW_MIN = PRICEW_MIN,
	OBJ_BTN_MIN_W = OBJ_BTN_MIN_W,
	KBD_BTN_MIN_W = KBD_BTN_MIN_W,
	PANEL_PAD = PANEL_PAD,
	OBJ_PANEL_TRIM = OBJ_PANEL_TRIM,
	SIDE_PANEL_RATIO = SIDE_PANEL_RATIO,
	RIGHT_PANEL_WIDTH_MULT = RIGHT_PANEL_WIDTH_MULT,
	RIGHT_PANEL_W_COLLAPSED = RIGHT_PANEL_W_COLLAPSED,
	NUMPAD = NUMPAD,
	OBJECTS_DEFAULT = OBJECTS_DEFAULT,
}

local function updateCtx()
	ctx.funcs = funcs
	ctx.mimgui_funcs = mimgui_funcs
	ctx.correct_module = correct_module
	ctx.SMILive = SMILive
	ctx.samp_module = samp_module
	ctx.trim = trim
	ctx.utf8_len = utf8_len
	ctx.tolower_utf8 = tolower_utf8
	ctx.imgui_text_safe = imgui_text_safe
	ctx.imgui_text_wrapped_safe = imgui_text_wrapped_safe
	ctx.imgui_text_colored_safe = imgui_text_colored_safe
	ctx.imgui_bullet_text_safe = imgui_bullet_text_safe
	ctx.imgui_set_tooltip_safe = imgui_set_tooltip_safe
	ctx.bigFont = bigFont
end

-- Подмодули
local constructor = require("HelperByOrc.SMIHelp.constructor")
local main_frame = require("HelperByOrc.SMIHelp.main_frame")
local settings_ui = require("HelperByOrc.SMIHelp.settings_ui")

ctx.constructor = constructor

constructor.init(ctx)
main_frame.init(ctx)
settings_ui.init(ctx)

-- Подписка на OnFrame из main_frame
_imguiSubs[#_imguiSubs + 1] = main_frame.createOnFrame()

-- ========= ПУБЛИЧНЫЙ API (делегирование) =========
function SMIHelp.onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
	return constructor.onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
end

function SMIHelp.OpenEditPreview(text, nick)
	constructor.OpenEditPreview(text, nick)
end

SMIHelp.DrawSettingsUI = function()
	settings_ui.DrawSettingsUI()
end

function SMIHelp.attachModules(mod)
	syncDependencies(mod)
	updateCtx()
	config_manager_ref = mod.config_manager
	event_bus_ref = mod.event_bus
	if event_bus_ref then
		event_bus_ref.offByOwner("SMIHelp")
	end
	if config_manager_ref then
		local data = config_manager_ref.register("SMIHelp", {
			path = CONFIG_PATH_REL,
			defaults = {},
			loader = function(path, defaults)
				-- Config:load does complex normalization, use it directly
				Config:load()
				return Config.data
			end,
		})
		Config.data = data
	end
	-- ре-инициализация подмодулей с обновлённым ctx
	constructor.init(ctx)
	main_frame.init(ctx)
	settings_ui.init(ctx)
end

function SMIHelp.onTerminate(reason)
	for i = #_imguiSubs, 1, -1 do
		local sub = _imguiSubs[i]
		if sub and type(sub.Unsubscribe) == "function" then
			pcall(sub.Unsubscribe, sub)
		end
		_imguiSubs[i] = nil
	end
	if event_bus_ref then
		event_bus_ref.offByOwner("SMIHelp")
	end
	constructor.deinit()
end

return SMIHelp
