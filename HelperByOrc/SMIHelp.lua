--[[
	СМИ Хелпер — редактор объявлений для SA:MP (MoonLoader + mimgui)
	Рефакторинг: архитектура, производительность, надёжность, расширяемость, UX (без хоткеев/хинтов)
	Центрированный фильтр над инпутом. Компактные категории.
	Таймер блокировки отправки с отображением оставшегося времени.
	Память по никнейму: если от того же отправителя пришёл тот же исходный текст,
	автоматически подставляется ранее отправленный (отредактированный) вариант.
	Лимит памяти — последние 100 записей (MRU), с автоочисткой.

	Хук для таймера (пример):
		if string.match(text2, '^%[VIP%] Объявление:.') or string.match(text2, '^{FCAA4D}%[VIP%] Объявление:.') then
			lua_thread.create(function()
				SMIHelp.timer_send_clock = os.clock()
				SMIHelp.timer_send = false
				repeat wait(0) until (os.clock() - SMIHelp.timer_send_clock >= SMIHelp.timer_send_delay)
				SMIHelp.timer_send = true
			end)
		end
]]

local SMIHelp = {}

-- ========= ЗАВИСИМОСТИ =========
local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8

local ffi = require 'ffi'
local imgui = require 'mimgui'
local new = imgui.new

local str = ffi.string
local sizeof = ffi.sizeof

local ok_effil, effil = pcall(require, 'effil')	  -- асинхрон для спеллера
local ok_https, _ = pcall(require, 'ssl.https')
local bit = require 'bit'						  -- для UTF-8 разборки и флагов
local vk = require 'vkeys'

-- опциональные зависимости (совместимость/сейв конфига)
local mimgui_funcs
local funcs

function SMIHelp.attachModules(mod)
		mimgui_funcs = mod.mimgui_funcs
		funcs = mod.funcs
end
local trim = (funcs and funcs.trim) and funcs.trim or function(s) return (s or ""):gsub("^%s*(.-)%s*$", "%1") end

-- ========= КОНСТАНТЫ/НАСТРОЙКИ =========
local CONFIG_PATH = getWorkingDirectory().."\\HelperByOrc\\SMIHelp.json"

local INPUT_MAX = 80					 -- жёсткий лимит символов UTF-8
local LIMIT_WARN_RATIO = 0.90			-- порог предупреждения 90%

local SECTION_H = 0					-- высота секции конструктора
local BTN_H = 24						 -- высота кнопок сеток
local TYPEW_BASE, TYPEW_MIN	  = 150, 110
local PRICEW_BASE, PRICEW_MIN = 165, 120
local OBJ_BTN_MIN_W	 = 88
local KBD_BTN_MIN_W	 = 56
local NUMPAD = {"1","2","3","4","5","6","7","8","9",".","0"}

local SPELLER_DEBOUNCE_SEC = 0.5		 -- дребезг запуска автокорректора

-- ========= УТИЛИТЫ =========
local function safe(s) return s or "" end

-- атомарная запись файла (насколько возможно в Windows)
local function atomic_write(path, data)
	local tmp = path .. ".tmp"
	local f = io.open(tmp, 'wb'); if not f then return false end
	f:write(data or ""); f:close()
	pcall(os.remove, path)
	return os.rename(tmp, path)
end

-- ========= UTF-8: длина и обрезка по символам =========
local function utf8_len(s)
	s = tostring(s or "")
	local len, i, n = #s, 1, 0
	while i <= len do
		n = n + 1
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
	end
	return n
end

local function utf8_truncate(s, max_chars)
	s = tostring(s or "")
	if max_chars <= 0 then return "" end
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

local function clamp80(s) return utf8_truncate(tostring(s or ""), INPUT_MAX) end

-- ========= КОНФИГ =========
local Config = { data = {} }

function Config:load()
	local t
	if doesFileExist(CONFIG_PATH) then
		local ok, data = pcall(function()
			local f = io.open(CONFIG_PATH, 'rb'); if not f then return end
			local s = f:read('*a'); f:close()
			local ok2, t2 = pcall(decodeJson, s)
			if ok2 and type(t2)=='table' then return t2 end
		end)
		t = (ok and data) or {}
	end
	if type(t) ~= 'table' then t = {} end

	local function ensure_table(key, def)
		if type(t[key]) ~= 'table' then t[key] = def end
	end

	t.type_buttons = (type(t.type_buttons)=='table' and t.type_buttons) or {'Куплю','Продам','Арендую','Сдам в аренду','Обменяю','Ищу','Предоставляю','Нуждаюсь'}
	t.objects = (type(t.objects)=='table' and t.objects) or {'а/м','а/с','р/с','б/з','о/д','с/б','н/з','о/п','т/с','м/ц','в/с','в/т','м/с','л/д','г/ф','д/т','к/т','с/о','т/ф','с/м'}
	t.prices = (type(t.prices)=='table' and t.prices) or {"Цена:","Бюджет:","Цена за шт:","Бюджет за шт:","Цена за час:","Бюджет за час:"}
	t.currencies = (type(t.currencies)=='table' and t.currencies) or {"$","тыс.$","млн.$","млрд.$","Свободный","Договорная"}
	t.addons = (type(t.addons)=='table' and t.addons) or {'с гравировкой +','с вышивкой ','с биркой ','в кол-ве '}

	ensure_table('templates', {
		{ category = "Прочее",  text = "Предоставляю услуги охраны, все подробности по телефону" },
		{ category = "Реклама", texts = {
			{ "Работает СМИ г. Сан-Фиерро. Мы ждём ваши объявлений!",
			  "СМИ г. Сан-Фиерро в режиме ожидания ваших объявлений",
			  "Здесь могла быть ваша реклама! Работает СМИ г. Сан-Фиерро!" },
			{ "Ищу семью. О себе при встрече. Просьба связаться" },
		} }
	})

	ensure_table('history', {})
	t.history_limit = (type(t.history_limit)=='number' and t.history_limit) or 100

	ensure_table('autocorrect', {
		{"!тт","TwinTurbo"}, {"!см","Санта-Мария"}, {"!лс","г. Лос-Сантос"}
	})

	-- память по никнеймам (MRU)
	ensure_table('nick_memory', {})	 -- формат: nick -> { last_incoming=string, last_sent=string, ts=number }
	t.nick_memory_limit = (type(t.nick_memory_limit)=='number' and t.nick_memory_limit) or 100
	if type(t.nick_memory._order) ~= 'table' then t.nick_memory._order = {} end	 -- порядок MRU

        -- настройки таймеров
        if type(t.vip_timer_enabled) ~= "boolean" then t.vip_timer_enabled = true end
        t.vip_timer_delay = (type(t.vip_timer_delay) == "number" and t.vip_timer_delay) or 10

        if type(t.btn_timer_enabled) ~= "boolean" then t.btn_timer_enabled = true end
        t.btn_timer_delay = (type(t.btn_timer_delay) == "number" and t.btn_timer_delay) or 3

	self.data = t
end

function Config:save()
	local s
	if funcs and funcs.saveTableToJson then
		funcs.saveTableToJson(self.data, CONFIG_PATH)
		return
	else
		s = encodeJson(self.data or {})
	end
	atomic_write(CONFIG_PATH, s or "{}")
end

local function add_to_history(s)
	local t = Config.data
	if type(s) ~= 'string' then return end
	s = trim(s)
	if s == '' then return end
	for i=#t.history,1,-1 do
		if t.history[i] == s then table.remove(t.history, i) end
	end
	table.insert(t.history, 1, s)
	while #t.history > (t.history_limit or 100) do table.remove(t.history) end
	Config:save()
end

Config:load()

-- ========= Память по никнеймам: MRU 100 =========
local function nickmem_get_mem()
	local mem = Config.data.nick_memory or {}
	if type(mem._order) ~= 'table' then mem._order = {} end
	Config.data.nick_memory = mem
	return mem
end

local function nickmem_touch(mem, nick)
	local order = mem._order
	for i = #order, 1, -1 do
		if order[i] == nick then table.remove(order, i) break end
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
	if not nick or nick == "" then return end
	local mem = nickmem_get_mem()
	mem[nick] = {
		last_incoming = trim(incoming or ""),
		last_sent	 = clamp80(sent or ""),
		ts			= os.time()
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
SMIHelp.pasr_find = 'ALL'
SMIHelp.filter_SMI = imgui.ImGuiTextFilter()

-- ========= ГЛОБАЛЬНЫЙ UI-ФОНТ =========
local bigFont = nil
imgui.OnInitialize(function()
	local io = imgui.GetIO()
	bigFont = io.Fonts:AddFontFromFileTTF('C:\\Windows\\Fonts\\arial.ttf', 18.0, nil, io.Fonts:GetGlyphRangesCyrillic())
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

	selected_category = "Все",

	want_place_cursor = false,
	want_focus_input = false,
	collapse_selection_after_focus = false,
	cursor_action = nil,			 -- 'to_next_quote' | 'to_end' | 'to_first_empty_quotes' | 'to_addon_end'
	cursor_action_data = nil,
	hist_index = nil,

	-- спеллер
	last_speller_call = 0,
		corr_cache = {},
		corr_in_progress = false,
		corr_error = nil,

		win_pos = imgui.ImVec2(100, 100),
		win_size = imgui.ImVec2(1280, 650),
}

local function history_reset_index()
	State.hist_index = (#(Config.data.history or {})) + 1
end

-- ========= ТЕКСТ/КОНСТРУКТОР ОБЪЯВЛЕНИЯ =========
local AD = {
	type = nil,
	object = nil,
	object_value = nil,
	price_label = nil,
	value = nil,
	currency = nil,
	addon = nil
}
function AD:reset()
	self.type = nil
	self.object = nil
	self.object_value = nil
	self.price_label = nil
	self.value = nil
	self.currency = nil
	self.addon = nil
end

local function ad_build()
	local s = AD.type or ""
	if AD.object then
		s = s .. " " .. AD.object
		s = s .. ' "' .. (AD.object_value or "") .. '"'
		if AD.addon then s = s .. " " .. AD.addon end
		s = s .. "."
	end
	if AD.price_label then
		s = s .. " " .. AD.price_label
		if AD.value or AD.currency then
			s = s .. " " .. (AD.value or "")
			if AD.currency then s = s .. " " .. AD.currency end
		end
	end
	s = s:gsub("  +", " ")
	return trim(s)
end

local function refresh_object_value_from_editbuf()
	local txt = str(State.edit_buf)
	local q = txt:match('%b""')
	if q then AD.object_value = q:sub(2, -2) end
end

local function ad_commit_to_editbuf()
	refresh_object_value_from_editbuf()
	local built = ad_build()
	built = clamp80(built)
	imgui.StrCopy(State.edit_buf, built)
end

-- ========= ПОМОЩНИКИ ПО ТЕКСТУ/ПОИСКУ =========
local utf8_lower_map = {
	["А"]="а",["Б"]="б",["В"]="в",["Г"]="г",["Д"]="д",["Е"]="е",["Ё"]="ё",["Ж"]="ж",["З"]="з",["И"]="и",
	["Й"]="й",["К"]="к",["Л"]="л",["М"]="м",["Н"]="н",["О"]="о",["П"]="п",["Р"]="р",["С"]="с",["Т"]="т",
	["У"]="у",["Ф"]="ф",["Х"]="х",["Ц"]="ц",["Ч"]="ч",["Ш"]="ш",["Щ"]="щ",["Ъ"]="ъ",["Ы"]="ы",["Ь"]="ь",
	["Э"]="э",["Ю"]="ю",["Я"]="я"
}
local function tolower_utf8(s)
	return (s or ""):gsub("[%z\1-\127\194-\244][\128-\191]*", function(c)
		return utf8_lower_map[c] or c:lower()
	end)
end

local function passFilter(line, raw)
	local target = tolower_utf8(line or "")
	raw = tolower_utf8(raw or "")
	if raw == "" then return true end
	for word in raw:gmatch("[^,]+") do
		word = trim(word)
		local ex = word:sub(1,1) == "-"
		local val = ex and word:sub(2) or word
		local found = target:find(val, 1, true)
		if ex and found then return false
		elseif not ex and found then return true end
	end
	return false
end

-- ========= МЯГКАЯ ЛОКАЛЬНАЯ АВТОЗАМЕНА =========
local function apply_autocorrect_local(text)
	local ac = Config.data.autocorrect
	if type(ac) ~= 'table' then return text end
	for _, pair in ipairs(ac) do
		local find, repl = pair[1], pair[2]
		if find and repl then text = text:gsub(find, repl) end
	end
	return text
end

-- ========= ПАРСИНГ BODY =========
local function parse_dialog_body(body)
	-- убираем цветовые коды {FFFFFF} и т.п.
	local clean = body:gsub("{.-}", "")
	
	-- ищем ник после "Объявление от "
	local nick = clean:match("Объявление от%s+([%w_]+)")
	
	-- ищем текст после "Сообщение:"
	local msg = clean:match("Сообщение:%s*(.-)\n")
	
	return nick or "", msg
	-- local nick = body:match("Объявление от%s+([^,%\r\n]+),")
	-- -- local msg = body:match("Сообщение:%s*(.-)\r?\n\r?\n")
	-- local msg = body:match("{33AA33}(.-)%s-{FFFFFF}")
	-- if not msg then msg = body:match("Сообщение:%s*(.+)") end
	-- msg = msg and trim(msg) or ""
	-- return nick or "", msg
end

-- ========= ДИАЛОГИ (SAMP) =========
local function extract_ad_text_from_dialog_colored(dialog_text)
	local clean = dialog_text:gsub("{.-}", "")
	local msg = clean:match("Сообщение:%s*(.-)$")
	-- local dialog_text = dialog_text:match("{33AA33}(.-)%s-{FFFFFF}")
	return '232131'..msg 
	-- return dialog_text:match("{33AA33}(.-)%s-{FFFFFF}") or ""
end

function SMIHelp.onShowDialog(dialogid, style, title, button1, button2, text, placeholder)
        local t = u8(title)
        local body = u8(text)
	if t:find("Редактирование") and (body:find("Объявление от") ~= nil) then
		State.show_dialog[0]   = true
		State.last_dialog_id   = dialogid
		State.last_dialog_title= t
		State.last_dialog_text = body

		local nick, msg = parse_dialog_body(body)
		if msg == "" then msg = extract_ad_text_from_dialog_colored(body) end
		State.sender_nick = nick or ""
		State.original_ad_text = msg or ""

		State.auto_memory_used = false
		local mem = nickmem_get_mem()
		local rec = mem[State.sender_nick]
		local incoming = trim(State.original_ad_text or "")
		if rec and rec.last_incoming and trim(rec.last_incoming) == incoming and rec.last_sent and rec.last_sent ~= "" then
			local paste = clamp80(rec.last_sent)
			imgui.StrCopy(State.edit_buf, paste)
			State.auto_memory_used = true
			State.cursor_action = 'to_end'
			State.cursor_action_data = nil
			State.want_focus_input = true
			State.collapse_selection_after_focus = true
		else
			imgui.StrCopy(State.edit_buf, apply_autocorrect_local(State.original_ad_text))
			State.cursor_action = nil
		end

		history_reset_index()
		AD:reset()
		return false
	end
end

-- Открытие окна редактирования в режиме эмуляции
function SMIHelp.OpenEditPreview(text, nick)
		State.show_dialog[0] = true
		State.last_dialog_id = nil
		State.last_dialog_title = "Редактирование объявления"
		State.last_dialog_text = ""
		State.sender_nick = nick or "Пример"
		State.original_ad_text = text or ""
		State.auto_memory_used = false
		State.cursor_action = nil
		imgui.StrCopy(State.edit_buf, clamp80(text or ""))
		State.want_focus_input = true
		State.collapse_selection_after_focus = true
		history_reset_index()
		AD:reset()
end

-- ========= КЭШИ ШАБЛОНОВ/КАТЕГОРИЙ =========
local Cache = { cats = nil, cats_key_len = 0 }
local function rebuild_cats_if_needed()
	local tpls = Config.data.templates or {}
	local key_len = #tpls
	if Cache.cats and Cache.cats_key_len == key_len then return end
	local cats_set, cats = {}, {}
	for _, t in ipairs(tpls) do
		local c = t.category or "Прочее"
		if not cats_set[c] then cats_set[c] = true; table.insert(cats, c) end
	end
	table.sort(cats)
	table.insert(cats, 1, "Все")
	Cache.cats, Cache.cats_key_len = cats, key_len
end

-- ========= UI УТИЛИТЫ =========
local function LabelSeparator(text)
	local label = string.format("-- %s ", safe(text))
	local draw	= imgui.GetWindowDrawList()
	local pos	= imgui.GetCursorScreenPos()
	local avail = imgui.GetContentRegionAvail().x
	local style = imgui.GetStyle()
	local txtsz = imgui.CalcTextSize(label)
	local center_x = pos.x + avail * 0.5
	local line_y   = pos.y + imgui.GetTextLineHeight() * 0.5
	local pad	  = style.ItemSpacing.x
	local left_x1  = pos.x
	local left_x2  = center_x - txtsz.x * 0.5 - pad
	local right_x1 = center_x + txtsz.x * 0.5 + pad
	local right_x2 = pos.x + avail
	local col = imgui.GetColorU32(imgui.Col.Separator)

	if left_x2 > left_x1 then
		draw:AddLine(imgui.ImVec2(left_x1,	line_y), imgui.ImVec2(left_x2,	line_y), col, 1.0)
	end
	if right_x2 > right_x1 then
		draw:AddLine(imgui.ImVec2(right_x1, line_y), imgui.ImVec2(right_x2, line_y), col, 1.0)
	end

	imgui.SetCursorScreenPos(imgui.ImVec2(center_x - txtsz.x * 0.5, pos.y))
	imgui.Text(label)
	imgui.SetCursorScreenPos(imgui.ImVec2(pos.x, pos.y + imgui.GetTextLineHeight() + style.ItemSpacing.y))
end

local function ButtonGrid(id, items, btnH, columns, onClick)
	local spacing = imgui.GetStyle().ItemSpacing.x
	local start_x = imgui.GetCursorPosX()
	local availW  = imgui.GetContentRegionAvail().x
	columns = math.max(1, columns or 3)

	local btnW = math.floor((availW - spacing * (columns - 1)))
	if columns > 1 then btnW = math.floor(btnW / columns) end

	local col = 0
	for i, val in ipairs(items) do
		local label = tostring(val) .. "##" .. id .. "_" .. i
		if imgui.Button(label, imgui.ImVec2(btnW, btnH)) then
			if onClick then onClick(val, i) end
		end
		col = col + 1
		if col < columns and i ~= #items then
			imgui.SameLine()
		else
			col = 0
		end
	end
	imgui.SetCursorPosX(start_x)
end

-- ========= АВТОКОРРЕКТОР (Яндекс, асинхрон + дребезг) =========
local function urlencode(text)
	text = tostring(text or "")
	text = text:gsub('{......}', ''):gsub('\r', '')
	text = text:gsub('\n', '%%0A')
	text = text:gsub(' ', '+'):gsub('&', '%%26'):gsub('%%', '%%25')
	text = text:gsub('<', '%%3C'):gsub('>', '%%3E'):gsub('"', '%%22')
	return text
end

local function asyncRequest(url, resolve, reject)
	if not ok_effil then if reject then reject('effil not found') end return end
	reject = reject or function() end
	lua_thread.create(function()
		local thread = effil.thread(function(u)
			local https = require 'ssl.https'
			local ok, result = pcall(https.request, u)
			return {ok, result}
		end)(url)

		local timeout = os.clock() + 30
		while true do
			local res = thread:get(0)
			if res then
				local ok, response = res[1], res[2]
				if ok then resolve(response) else reject(response) end
				break
			end
			if os.clock() > timeout then reject('Request timed out'); break end
			wait(0)
		end
		pcall(thread.cancel, thread)
		collectgarbage()
	end)
end

local function handleCorrectionLite()
	if not ok_effil or not ok_https then
		State.corr_error = "Нет effil или ssl.https"; return
	end
	local now = os.clock()
	if (now - (State.last_speller_call or 0)) < SPELLER_DEBOUNCE_SEC then
		return
	end
	State.last_speller_call = now

	local message = str(State.edit_buf)
	if message == '' then return end
	if State.corr_cache[message] then
		imgui.StrCopy(State.edit_buf, State.corr_cache[message])
		return
	end
	local url = "https://speller.yandex.net/services/spellservice.json/checkText?text=" .. (urlencode(message))
	State.corr_in_progress, State.corr_error = true, nil

	asyncRequest(url,
		function(response)
			State.corr_in_progress = false
			local ok, words = pcall(decodeJson, response)
			if not ok or type(words) ~= 'table' then return end
			if #words == 0 then return end

			local used = {}
			local corrected = message
			for _, wd in ipairs(words) do
				local incorrect = (wd.word)
				local suggestion = wd.s and wd.s[1] and (wd.s[1]) or incorrect
				if incorrect and suggestion and not used[incorrect] then
					corrected = corrected:gsub(incorrect, suggestion, 1)
					used[incorrect] = true
				end
			end
			corrected = corrected:gsub('//', '/')
			State.corr_cache[message] = corrected
			imgui.StrCopy(State.edit_buf, (corrected))
		end,
		function(err)
			State.corr_in_progress = false
			State.corr_error = tostring(err or 'Ошибка запроса')
		end
	)
end

-- ========= ШАБЛОНЫ: ГРУППЫ =========
local seeded = false
local function seed_once() if not seeded then math.randomseed(os.clock()*100000 % 1 * 1e9); seeded = true end end

local function tpl_groups(tpl)
	if type(tpl) ~= 'table' then return {} end
	if type(tpl.text) == 'string' then
		return { { tpl.text } }
	end
	if type(tpl.texts) == 'table' then
		local flat = true
		for _, v in ipairs(tpl.texts) do
			if type(v) == 'table' then flat = false break end
		end
		if flat then
			local group = {}
			for _, s in ipairs(tpl.texts) do
				if type(s) == 'string' then table.insert(group, s) end
			end
			if #group > 0 then return { group } else return {} end
		else
			local groups = {}
			for _, g in ipairs(tpl.texts) do
				if type(g) == 'table' then
					local group = {}
					for _, s in ipairs(g) do
						if type(s) == 'string' then table.insert(group, s) end
					end
					if #group > 0 then table.insert(groups, group) end
				elseif type(g) == 'string' then
					table.insert(groups, { g })
				end
			end
			return groups
		end
	end
	return {}
end

-- ========= ЧИЛДЫ/ПАНЕЛИ =========
local function DrawTemplatesPanel(width, height)
	imgui.BeginChild("tmpl_panel", imgui.ImVec2(width, height), true)

	rebuild_cats_if_needed()

	-- Компактные категории: выпадающий список
	imgui.Text("Категория:")
	imgui.SameLine()
	local cur_label = State.selected_category or "Все"
	if imgui.BeginCombo("##cat_combo", cur_label) then
		for _, cat in ipairs(Cache.cats or {"Все"}) do
			local sel = (State.selected_category == cat)
			if imgui.Selectable(cat, sel) then
				State.selected_category = cat
			end
		end
		imgui.EndCombo()
	end

	imgui.Spacing()
	LabelSeparator("Шаблоны")

	imgui.BeginChild("templates_list", imgui.ImVec2(0, 0), true)
	local filter_str = str(State.filter_buf)

	for _, tpl in ipairs(Config.data.templates or {}) do
		local cat = tpl.category or "Прочее"
		if State.selected_category == "Все" or State.selected_category == cat then
			for _, group in ipairs(tpl_groups(tpl)) do
				local display = group[1] or ""
				local combined = table.concat(group, " ")
				local line	 = ((cat ~= '' and (cat .. ': ') or '') .. (display or ''))
				local filter_line = ((cat ~= '' and (cat .. ': ') or '') .. combined)

				if passFilter(filter_line, filter_str) then
										if imgui.Selectable(line, false) then
												seed_once()
												local pick = group[math.random(1, #group)] or ""
												pick = clamp80(pick)
												imgui.StrCopy(State.edit_buf, pick)
												State.cursor_action = 'to_first_empty_quotes'
												State.cursor_action_data = nil
												State.want_place_cursor = false
												history_reset_index()
										end
										if imgui.IsItemClicked(1) then
												imgui.SetClipboardText(line)
										end
										if imgui.IsItemHovered() then
												imgui.SetTooltip(line)
										end
								end
						end
				end
		end

	imgui.EndChild()
	imgui.EndChild()
end

local function DrawHistoryPanel(width, height)
	imgui.BeginChild("hist_panel", imgui.ImVec2(width, height), true)
	LabelSeparator("История")
	imgui.BeginChild("history_list", imgui.ImVec2(0, 0), true)
	local filter_str = str(State.filter_buf)
	for _, v in ipairs(Config.data.history or {}) do
		if passFilter(v, filter_str) then
						if imgui.Selectable(v, false) then
								local txt = clamp80(v)
								imgui.StrCopy(State.edit_buf, txt)
								State.cursor_action = 'to_end'
								State.cursor_action_data = nil
								history_reset_index()
						end
						if imgui.IsItemClicked(1) then
								imgui.SetClipboardText(v)
						end
						if imgui.IsItemHovered() then
								imgui.SetTooltip(v)
						end
				end
		end
		imgui.EndChild()
		imgui.EndChild()
end

-- ========= КУРСОР: поиск позиций с циклическим обходом =========
local function find_next_quote_pos_cyclic(s, from_pos0)
	local from = (from_pos0 or 0) + 1
	local p = s:find('"', from + 1, true)
	if p then return p end
	return s:find('"', 1, true)
end

local function find_first_empty_quotes_pos_cyclic(s, from_pos0)
	local from = (from_pos0 or 0) + 1
	local p = s:find('""', from, true)
	if p then return p end
	return s:find('""', 1, true)
end

local function find_addon_end_pos_cyclic(s, addon_text, from_pos0)
	if not addon_text or addon_text == "" then return #s end
	local from = (from_pos0 or 0) + 1
	local p = s:find(addon_text, from, true)
	if not p then p = s:find(addon_text, 1, true) end
	if p then return p + #addon_text end
	return #s
end

-- ========= INPUTTEXT CALLBACK =========
local function EditBufCallback(data)
	local flag = data.EventFlag

	if flag == imgui.InputTextFlags.CallbackHistory then
		local up, down = 3, 4
		local H = Config.data.history or {}
		if data.EventKey == up then
			if State.hist_index == nil then history_reset_index() end
			if State.hist_index > 1 then
				State.hist_index = State.hist_index - 1
				local s = H[State.hist_index] or ""
				s = clamp80(s)
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, s)
			end
			return 1
		elseif data.EventKey == down then
			if State.hist_index == nil then history_reset_index() end
			local max_pos = #H + 1
			if State.hist_index < max_pos then
				State.hist_index = State.hist_index + 1
				local s = (State.hist_index <= #H) and (H[State.hist_index] or "") or ""
				s = clamp80(s)
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, s)
			else
				data:DeleteChars(0, data.BufTextLen)
				data:InsertChars(0, "")
			end
			return 1
		end
	end

	if flag == imgui.InputTextFlags.CallbackCharFilter then
		return 0
	end

	if flag == imgui.InputTextFlags.CallbackAlways then
		if State.collapse_selection_after_focus then
			if data.SelectionStart == 0 and data.SelectionEnd == data.BufTextLen and data.BufTextLen > 0 then
				data.CursorPos = data.BufTextLen
				data.SelectionStart = data.CursorPos
				data.SelectionEnd	= data.CursorPos
			end
			State.collapse_selection_after_focus = false
		end

		local cur = str(data.Buf)
		local chars = utf8_len(cur)
		if chars > INPUT_MAX then
			local truncated = utf8_truncate(cur, INPUT_MAX)
			data:DeleteChars(0, data.BufTextLen)
			data:InsertChars(0, truncated)
			data.CursorPos = #truncated
			return 1
		end

		local replaced = cur
		local ac = Config.data.autocorrect
		if type(ac) == 'table' then
			for _, pair in ipairs(ac) do
				local find, repl = pair[1], pair[2]
				if find and repl and replaced:find(find, 1, true) then
					replaced = replaced:gsub(find, repl)
				end
			end
		end
		if replaced ~= cur then
			replaced = clamp80(replaced)
			data:DeleteChars(0, data.BufTextLen)
			data:InsertChars(0, replaced)
			data.CursorPos = #replaced
			return 1
		end

		if State.want_place_cursor or State.cursor_action ~= nil then
			local cur2 = str(data.Buf)

			if State.want_place_cursor then
				local p = find_next_quote_pos_cyclic(cur2, data.CursorPos or 0)
				if p then data.CursorPos = p end
				State.want_place_cursor = false
				return 1
			end

			if State.cursor_action == 'to_next_quote' then
				local nxt = find_next_quote_pos_cyclic(cur2, data.CursorPos or 0)
				if nxt then data.CursorPos = nxt else data.CursorPos = data.BufTextLen end
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			elseif State.cursor_action == 'to_end' then
				data.CursorPos = data.BufTextLen
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			elseif State.cursor_action == 'to_first_empty_quotes' then
				local p = find_first_empty_quotes_pos_cyclic(cur2, data.CursorPos or 0)
				if p then data.CursorPos = p else data.CursorPos = data.BufTextLen end
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			elseif State.cursor_action == 'to_addon_end' then
				local pos = find_addon_end_pos_cyclic(cur2, State.cursor_action_data or "", data.CursorPos or 0)
				data.CursorPos = pos - 1
				State.cursor_action, State.cursor_action_data = nil, nil
				return 1
			end
		end
	end

	return 0
end
local EditBufCallbackPtr = ffi.cast('int (*)(ImGuiInputTextCallbackData* data)', EditBufCallback)

-- ========= CHAR LIMIT BAR =========
local function DrawCharLimitBar(current_chars, max_chars)
	local percent = 0.0
	if max_chars > 0 then percent = math.min(1.0, current_chars / max_chars) end
	if percent >= LIMIT_WARN_RATIO then
		imgui.PushStyleColor(imgui.Col.PlotHistogram, imgui.ImVec4(1, 0.3, 0.3, 1))
	end
	imgui.ProgressBar(percent, imgui.ImVec2(-1, 8), "")
	if percent >= LIMIT_WARN_RATIO then
		imgui.PopStyleColor()
	end
end

-- ========= СБРОС UI-СОСТОЯНИЯ =========
local function reset_ui_state()
	State.selected_category = "Все"
	imgui.StrCopy(State.filter_buf, "")
end

-- ========= Таймеры отправки =========
local function vip_timer_remaining()
        if not SMIHelp.timer_send_enabled or SMIHelp.timer_send then
                return 0
        end
        local elapsed = os.clock() - (SMIHelp.timer_send_clock or 0)
        local rem = (SMIHelp.timer_send_delay or 0) - elapsed
        if rem <= 0 then
                SMIHelp.timer_send = true
                rem = 0
        end
        return rem
end

local function btn_timer_remaining()
        if not SMIHelp.btn_timer_enabled then
                SMIHelp.btn_timer = true
                return 0
        end
        local elapsed = os.clock() - (SMIHelp.btn_timer_clock or 0)
        local rem = (SMIHelp.btn_timer_delay or 0) - elapsed
        if rem <= 0 then
                SMIHelp.btn_timer = true
                rem = 0
        else
                SMIHelp.btn_timer = false
        end
        return rem
end

-- ========= Центрированный фильтр =========
local function DrawCenteredFilter()
		local style  = imgui.GetStyle()
		local availX = imgui.GetContentRegionAvail().x
		local inputW = math.floor(availX * 0.48)
		local clearW = 70
		local show_clear = str(State.filter_buf) ~= ""
		local totalW = inputW + (show_clear and (style.ItemSpacing.x + clearW) or 0)

		local x0 = imgui.GetCursorPosX()
		imgui.SetCursorPosX(x0 + (availX - totalW) / 2)

		imgui.PushItemWidth(inputW)
		local _ = imgui.InputText("##filter", State.filter_buf, sizeof(State.filter_buf))
		imgui.PopItemWidth()

		if show_clear then
				imgui.SameLine()
				if imgui.Button("Clear", imgui.ImVec2(clearW, 0)) then
						imgui.StrCopy(State.filter_buf, "")
				end
		end
end

-- ========= Блок «От кого и что прислано» =========
local function DrawMetaPanel()
	imgui.BeginChild("meta_panel", imgui.ImVec2(0, 92), true)
		imgui.Text("Отправитель:")
		imgui.SameLine()
		imgui.TextColored(imgui.ImVec4(0.8, 1.0, 0.8, 1), State.sender_nick ~= "" and State.sender_nick or "-")
		imgui.SameLine()
		if imgui.SmallButton("Скопировать ник") then
			imgui.SetClipboardText(State.sender_nick or "")
		end
		if State.auto_memory_used then
			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.4, 0.95, 0.4, 1), "[Автовставка из памяти]")
		end

		imgui.Text("Исходное сообщение:")
		local startX = imgui.GetCursorPosX()
		imgui.SetCursorPosX(startX + 4)
		imgui.BeginChild("orig_box", imgui.ImVec2(0, 25), true)
			imgui.TextWrapped(State.original_ad_text ~= "" and State.original_ad_text or "-")
		imgui.EndChild()
		if imgui.SmallButton("Скопировать исходник") then
			imgui.SetClipboardText(State.original_ad_text or "")
		end
	imgui.EndChild()
end

-- ========= ОСНОВНОЙ FRAME =========
imgui.OnFrame(
	function() return State.show_dialog[0] end,
	function()
				imgui.PushStyleVarFloat(imgui.StyleVar.FrameRounding, 6.0)
				imgui.PushStyleVarFloat(imgui.StyleVar.GrabRounding,  6.0)
				imgui.PushStyleVarFloat(imgui.StyleVar.WindowRounding,6.0)
				imgui.PushStyleVarFloat(imgui.StyleVar.ScrollbarRounding,6.0)

				if mimgui_funcs and mimgui_funcs.clampWindowToScreen then
						State.win_pos, State.win_size = mimgui_funcs.clampWindowToScreen(State.win_pos, State.win_size, 5)
				end
				imgui.SetNextWindowPos(State.win_pos, imgui.Cond.Always)
				imgui.SetNextWindowSize(State.win_size, imgui.Cond.Always)
				local opened = imgui.Begin("СМИ Хелпер", State.show_dialog, imgui.WindowFlags.NoCollapse)
				State.win_pos = imgui.GetWindowPos()
				State.win_size = imgui.GetWindowSize()

		if not State.show_dialog[0] then
			reset_ui_state()
		end

		imgui.TextColored(imgui.ImVec4(1, 0.95, 0.2, 1),
			(State.last_dialog_title ~= "" and State.last_dialog_title or "Редактирование объявления"))

		imgui.Separator()
		DrawMetaPanel()
		imgui.Separator()
		DrawCenteredFilter()
		imgui.Separator()

		local availX = imgui.GetContentRegionAvail().x
		local availY = imgui.GetContentRegionAvail().y
		local leftW	 = math.floor(availX * 0.26)
		local rightW = math.floor(availX * 0.26)
		local midW	 = availX - leftW - rightW - imgui.GetStyle().ItemSpacing.x * 2

		-- LEFT
		imgui.BeginGroup()
			DrawTemplatesPanel(leftW, availY)
                imgui.EndGroup()
		imgui.SameLine()

		-- CENTER
		imgui.BeginGroup()
			imgui.BeginChild("center", imgui.ImVec2(midW, availY), true)

			imgui.BeginChild("##centered_input_zone", imgui.ImVec2(0, 105), true)
			if bigFont then imgui.PushFont(bigFont) end
			imgui.PushItemWidth(-1)

			if State.want_focus_input then
				imgui.SetKeyboardFocusHere(0)
				State.want_focus_input = false
				State.collapse_selection_after_focus = true
			end

			local flags = bit.bor(
				imgui.InputTextFlags.CallbackHistory,
				imgui.InputTextFlags.CallbackAlways,
				imgui.InputTextFlags.CallbackCharFilter
			)
			local changed = imgui.InputText("##editad_center", State.edit_buf, sizeof(State.edit_buf), flags, EditBufCallbackPtr)

						imgui.Spacing()
						if imgui.SmallButton("Копировать текст") then
								imgui.SetClipboardText(str(State.edit_buf))
						end
						imgui.SameLine()
						if imgui.SmallButton(State.corr_in_progress and "Идёт проверка..." or "Автокоррекция") then
								if not State.corr_in_progress then handleCorrectionLite() end
						end
						imgui.SameLine()
						if imgui.SmallButton("К следующей кавычке") then
								State.cursor_action = 'to_next_quote'
								State.cursor_action_data = nil
								State.want_focus_input = true
								State.collapse_selection_after_focus = true
						end
						imgui.SameLine()
						if imgui.SmallButton("Курсор в конец") then
								State.cursor_action = 'to_end'
								State.cursor_action_data = nil
								State.want_focus_input = true
								State.collapse_selection_after_focus = true
						end
						if State.corr_error then
								imgui.SameLine()
								imgui.TextColored(imgui.ImVec4(1,0.4,0.4,1), "[Ошибка: "..State.corr_error.."]")
						end

						local cur_text = str(State.edit_buf)
						local char_count = utf8_len(cur_text)
						imgui.Spacing()
						DrawCharLimitBar(char_count, INPUT_MAX)

						imgui.PopItemWidth()
						if bigFont then imgui.PopFont() end

						imgui.EndChild()

			if changed then
				local old = str(State.edit_buf)
				local newtxt = apply_autocorrect_local(old)
				if newtxt ~= old then
					newtxt = clamp80(newtxt)
					imgui.StrCopy(State.edit_buf, newtxt)
				end
			end

-- Кнопки действий + таймер блокировки и сохранение памяти по нику
do
local vip_rem = vip_timer_remaining()
local btn_rem = btn_timer_remaining()
local can_send = SMIHelp.timer_send and (not SMIHelp.btn_timer_enabled or SMIHelp.btn_timer)
local btn_send_clicked = false
                                                        local avail = imgui.GetContentRegionAvail().x
                                                        local btnW = math.floor((avail - imgui.GetStyle().ItemSpacing.x) / 2)
                                                        local enter_pressed = wasKeyPressed(vk.VK_RETURN) or wasKeyPressed(vk.VK_NUMPADENTER)
                                                        if imgui.Button("Отправить", imgui.ImVec2(btnW, 0)) or enter_pressed then
                                                                btn_send_clicked = true
                                                        end
                                                        imgui.SameLine()
                                                        if imgui.Button("Отклонить", imgui.ImVec2(btnW, 0)) then
                                                                if State.last_dialog_id then
                                                                        local to_send_utf8 = str(State.edit_buf)
                                                                        local to_send_cp = u8:decode(to_send_utf8)
                                                                        sampSendDialogResponse(State.last_dialog_id, 0, 0, to_send_cp)
                                                                        State.show_dialog[0] = false
                                                                        AD:reset()
                                                                        reset_ui_state()
                                                                end
                                                        end
                                                        if imgui.Button("Сбросить к оригиналу", imgui.ImVec2(btnW, 0)) then
							local orig = clamp80(State.original_ad_text or "")
							imgui.StrCopy(State.edit_buf, orig)
							AD:reset()
							history_reset_index()
							State.want_focus_input = true
                                                        State.collapse_selection_after_focus = true
                                                        end
                                                        imgui.SameLine()
                                                                                                        imgui.Text(string.format("Симв.: %d/%d", char_count, INPUT_MAX))
                                                if not SMIHelp.timer_send then
                                                        imgui.Spacing()
                                                        imgui.TextColored(imgui.ImVec4(1,0.45,0.45,1), string.format("Таймер VIP активен. Осталось: %.1f c", vip_rem))
                                                elseif SMIHelp.btn_timer_enabled and not SMIHelp.btn_timer then
                                                        imgui.Spacing()
                                                        imgui.TextColored(imgui.ImVec4(1,0.45,0.45,1), string.format("Таймер отправки активен. Осталось: %.1f c", btn_rem))
                                                end
						if btn_send_clicked then
						if not can_send then
						-- блокируем отправку
						else
						if State.last_dialog_id then
						local to_send_utf8 = str(State.edit_buf)
						local to_send_cp = u8:decode(to_send_utf8)
						sampSendDialogResponse(State.last_dialog_id, 1, 0, to_send_cp)
						add_to_history(to_send_utf8)
						-- сохраняем память по никнейму (перезапись + MRU-трим)
						nickmem_save(State.sender_nick, State.original_ad_text, to_send_utf8)
						State.show_dialog[0] = false
						AD:reset()
						reset_ui_state()
                                                if SMIHelp.btn_timer_enabled then
                                                SMIHelp.btn_timer = false
                                                SMIHelp.btn_timer_clock = os.clock()
                                                end
						end
						end
						end
                                                end

			imgui.Spacing()
			LabelSeparator("Конструктор")

			-- Конструктор: вычисление ширин секций
			local c_availX = imgui.GetContentRegionAvail().x
			local spacing  = imgui.GetStyle().ItemSpacing.x

			local NEED_OBJ_W3 = 3 * OBJ_BTN_MIN_W + 2 * spacing
			local NEED_KBD_W3 = 3 * KBD_BTN_MIN_W + 2 * spacing

			local typeW	 = TYPEW_BASE
			local priceW = PRICEW_BASE
			local remain = c_availX - typeW - priceW - spacing * 3

			if remain < (NEED_OBJ_W3 + NEED_KBD_W3) then
				local deficit = (NEED_OBJ_W3 + NEED_KBD_W3) - remain
				local cutType  = math.min(deficit * 0.5, typeW - TYPEW_MIN)
				typeW  = typeW	- cutType
				deficit = deficit - cutType
				local cutPrice = math.min(deficit, priceW - PRICEW_MIN)
				priceW = priceW - cutPrice
				deficit = deficit - cutPrice
				remain	= c_availX - typeW - priceW - spacing * 3
			end

			local objW = math.max(NEED_OBJ_W3, math.floor(remain * 0.58))
			local kbdW = math.max(NEED_KBD_W3, remain - objW)

			local over = objW + kbdW - remain
			if over > 0 then
				local cutO = math.min(over * 0.5, objW - NEED_OBJ_W3)
				objW = objW - cutO
				over = over - cutO
				local cutK = math.min(over, kbdW - NEED_KBD_W3)
				kbdW = kbdW - cutK
			end

			local type_btns	 = Config.data.type_buttons
			local obj_btns	 = Config.data.objects
			local price_btns = Config.data.prices
			local numpad	 = NUMPAD
			local currencies = Config.data.currencies
			local addons	 = Config.data.addons

			-- Тип
			imgui.BeginChild('##type', imgui.ImVec2(typeW + 4, SECTION_H), true)
				ButtonGrid("type", type_btns, BTN_H, 1, function(val)
					AD:reset(); AD.type = val; ad_commit_to_editbuf()
					history_reset_index()
					State.want_focus_input = true
					State.collapse_selection_after_focus = true
				end)
			imgui.EndChild()
			imgui.SameLine()

			-- Объект (3 колонки)
			imgui.BeginChild('##object', imgui.ImVec2(objW - 115, SECTION_H), true)
				ButtonGrid("object", obj_btns, BTN_H, 3, function(val)
					refresh_object_value_from_editbuf()
					AD.object = val
					AD.addon  = nil
					ad_commit_to_editbuf()

					local txt = str(State.edit_buf)
					if not txt:find('%b""') then
						txt = clamp80(txt .. ' ""')
						imgui.StrCopy(State.edit_buf, txt)
					end

					State.want_place_cursor = true
					history_reset_index()
					State.want_focus_input = true
					State.collapse_selection_after_focus = true
				end)
			imgui.EndChild()
			imgui.SameLine()

			-- Цена
			imgui.BeginChild('##price', imgui.ImVec2(priceW + 4, SECTION_H), true)
				ButtonGrid("price", price_btns, BTN_H, 1, function(val)
					refresh_object_value_from_editbuf()
					AD.price_label = val
					ad_commit_to_editbuf()
					history_reset_index()
					State.want_focus_input = true
					State.collapse_selection_after_focus = true
				end)
			imgui.EndChild()
			imgui.SameLine()

			-- Numpad + валюта + дополнения (3 колонки)
			imgui.BeginChild('##kbd', imgui.ImVec2(kbdW, SECTION_H), true)
				ButtonGrid("numpad", numpad, BTN_H, 3, function(key)
					refresh_object_value_from_editbuf()
					AD.value = (AD.value or "") .. key
					ad_commit_to_editbuf()
					history_reset_index()
					State.want_focus_input = true
					State.collapse_selection_after_focus = true
				end)

                                imgui.Spacing(); imgui.Separator(); imgui.Spacing()
                                local cur_label = AD.currency or (currencies[1] or "-")
                                local addon_label = AD.addon or "- выбрать дополнение -"
                                imgui.BeginChild('##currency_addon', imgui.ImVec2(0, 0), false, imgui.WindowFlags.MenuBar)
                                        if imgui.BeginMenuBar() then
                                               -- currency menu
                                               local cur_menu_hover, cur_popup_hover = false, false
                                               local cur_open = imgui.BeginMenu(cur_label)
                                               if cur_open then
                                                       for _, item in ipairs(currencies) do
                                                               local sel = (AD.currency == item)
                                                               if imgui.MenuItemBool(item, nil, sel) then
                                                                       refresh_object_value_from_editbuf()
                                                                       AD.currency = item
                                                                       ad_commit_to_editbuf()
                                                                       State.cursor_action = 'to_end'; State.cursor_action_data = nil
                                                                       history_reset_index()
                                                                       State.want_focus_input = true
                                                                       State.collapse_selection_after_focus = true
                                                               end
                                                       end
                                                       cur_popup_hover = imgui.IsWindowHovered(imgui.HoveredFlags.RootAndChildWindows)
                                                       imgui.EndMenu()
                                                       cur_menu_hover = imgui.IsItemHovered()
                                               else
                                                       cur_menu_hover = imgui.IsItemHovered()
                                                       if cur_menu_hover then imgui.OpenPopup(cur_label) end
                                               end
                                               if cur_open and not cur_menu_hover and not cur_popup_hover then
                                                       imgui.CloseCurrentPopup()
                                               end

                                               -- addon menu
                                               local addon_menu_hover, addon_popup_hover = false, false
                                               local addon_open = imgui.BeginMenu(addon_label)
                                               if addon_open then
                                                       for _, item in ipairs(addons) do
                                                               local sel = (AD.addon == item)
                                                               if imgui.MenuItemBool(item, nil, sel) then
                                                                       refresh_object_value_from_editbuf()
                                                                       AD.addon = item
                                                                       ad_commit_to_editbuf()
                                                                       State.cursor_action = 'to_addon_end'
                                                                       State.cursor_action_data = item
                                                                       history_reset_index()
                                                                       State.want_focus_input = true
                                                                       State.collapse_selection_after_focus = true
                                                               end
                                                       end
                                                       addon_popup_hover = imgui.IsWindowHovered(imgui.HoveredFlags.RootAndChildWindows)
                                                       imgui.EndMenu()
                                                       addon_menu_hover = imgui.IsItemHovered()
                                               else
                                                       addon_menu_hover = imgui.IsItemHovered()
                                                       if addon_menu_hover then imgui.OpenPopup(addon_label) end
                                               end
                                               if addon_open and not addon_menu_hover and not addon_popup_hover then
                                                       imgui.CloseCurrentPopup()
                                               end

                                               imgui.EndMenuBar()
                                        end
                                imgui.EndChild()
                        imgui.EndChild()

			imgui.EndChild()
		imgui.EndGroup()

		imgui.SameLine()

		-- RIGHT
		imgui.BeginGroup()
			DrawHistoryPanel(rightW, availY)
		imgui.EndGroup()

		imgui.End()
                imgui.PopStyleVar(4)
                end
)

-- ========= СЕРИАЛИЗАЦИЯ НАСТРОЕК =========
local function autocorrect_to_string(list)
    local lines = {}
    if type(list) == 'table' then
        for _, pair in ipairs(list) do
            local find = pair[1] or ''
            local repl = pair[2] or ''
            table.insert(lines, find .. '=' .. repl)
        end
    end
    return table.concat(lines, '\n')
end

local function parse_autocorrect(text)
    local res = {}
    text = tostring(text or '')
    for line in text:gmatch('[^\n]+') do
        local find, repl = line:match('^(.-)=(.*)$')
        if find and find ~= '' then
            table.insert(res, { find, repl or '' })
        end
    end
    return res
end

function SMIHelp.DrawSettingsUI()
               if not SMIHelp._settings then
                                               SMIHelp._settings = {
                                               type_buttons = new.char[512](),
                                               objects = new.char[512](),
                                               prices = new.char[512](),
                                               currencies = new.char[512](),
                                               addons = new.char[512](),
                                               autocorrect = new.char[1024](),
                                               history_limit = new.int(Config.data.history_limit or 100),
                                               nick_memory_limit = new.int(Config.data.nick_memory_limit or 100),
                                               vip_timer_enabled = new.bool(SMIHelp.timer_send_enabled),
                                               vip_timer_delay = new.int(SMIHelp.timer_send_delay or 10),
                                               btn_timer_enabled = new.bool(SMIHelp.btn_timer_enabled),
                                               btn_timer_delay = new.int(SMIHelp.btn_timer_delay or 3),
                                               timer_news_delay = new.int(SMIHelp.timer_news_delay or 4),
                                               templates_list = {},
                                               tpl_input = {},
                                               tpl_multiline = {}
                                               }
                               imgui.StrCopy(SMIHelp._settings.type_buttons, table.concat(Config.data.type_buttons or {}, ','))
                               imgui.StrCopy(SMIHelp._settings.objects, table.concat(Config.data.objects or {}, ','))
                               imgui.StrCopy(SMIHelp._settings.prices, table.concat(Config.data.prices or {}, ','))
                               imgui.StrCopy(SMIHelp._settings.currencies, table.concat(Config.data.currencies or {}, ','))
                               imgui.StrCopy(SMIHelp._settings.addons, table.concat(Config.data.addons or {}, ','))
                               imgui.StrCopy(SMIHelp._settings.autocorrect, autocorrect_to_string(Config.data.autocorrect))
                               for _, tpl in ipairs(Config.data.templates or {}) do
                                               local copy = { category = tpl.category, texts = {} }
                                               if type(tpl.texts) == 'table' then
                                                               for _, s in ipairs(tpl.texts) do table.insert(copy.texts, s) end
                                               elseif type(tpl.text) == 'string' then
                                                               table.insert(copy.texts, tpl.text)
                                               end
                                               table.insert(SMIHelp._settings.templates_list, copy)
                               end
               end
               local S = SMIHelp._settings
               imgui.BeginChild('smi_settings', imgui.ImVec2(0,0), true)
                                                imgui.InputInt('Лимит истории', S.history_limit, 1, 1000)
                                                imgui.InputInt('Лимит памяти ников', S.nick_memory_limit, 1, 1000)
                                                imgui.Checkbox('Таймер VIP объявлений', S.vip_timer_enabled)
                                                imgui.InputInt('Задержка VIP таймера', S.vip_timer_delay, 1, 60)
                                                imgui.Checkbox('Таймер кнопки \"Отправить\"', S.btn_timer_enabled)
                                                imgui.InputInt('Задержка отправки', S.btn_timer_delay, 1, 60)
                                                imgui.InputInt('Задержка новостей', S.timer_news_delay, 1, 60)
				imgui.InputTextMultiline('Типы', S.type_buttons, 512, imgui.ImVec2(0,60))
				imgui.InputTextMultiline('Объекты', S.objects, 512, imgui.ImVec2(0,60))
                                imgui.InputTextMultiline('Цены', S.prices, 512, imgui.ImVec2(0,60))
                                imgui.InputTextMultiline('Валюты', S.currencies, 512, imgui.ImVec2(0,60))
                               imgui.InputTextMultiline('Дополнения', S.addons, 512, imgui.ImVec2(0,60))
                               imgui.InputTextMultiline('Автокоррекция', S.autocorrect, 1024, imgui.ImVec2(0,60))
                               if imgui.CollapsingHeader('Шаблоны') then
                                               if imgui.BeginTabBar('tpl_tabs') then
                                                               for idx, tpl in ipairs(S.templates_list) do
                                                                               local cat = tpl.category or ('Категория '..idx)
                                                                               if imgui.BeginTabItem(cat) then
                                                                                               for _, txt in ipairs(tpl.texts or {}) do
                                                                                                               if type(txt) == 'table' then
                                                                                                                               local pieces = {}
                                                                                                                               for _, s in ipairs(txt) do table.insert(pieces, tostring(s)) end
                                                                                                                               imgui.BulletText(table.concat(pieces, ', '))
                                                                                                               else
                                                                                                                               imgui.BulletText(tostring(txt))
                                                                                                               end
                                                                                               end
                                                                                               S.tpl_input[idx] = S.tpl_input[idx] or new.char[256]()
                                                                                               S.tpl_multiline[idx] = S.tpl_multiline[idx] or new.bool(false)
                                                                                               if S.tpl_multiline[idx][0] then
                                                                                                               imgui.InputTextMultiline('##tplinput'..idx, S.tpl_input[idx], 256, imgui.ImVec2(0,60))
                                                                                                               if imgui.Button('Одна строка##toggle'..idx) then
                                                                                                                               S.tpl_multiline[idx][0] = false
                                                                                                               end
                                                                                               else
                                                                                                               imgui.InputText('##tplinput'..idx, S.tpl_input[idx], 256)
                                                                                                               if imgui.Button('Много строк##toggle'..idx) then
                                                                                                                               S.tpl_multiline[idx][0] = true
                                                                                                               end
                                                                                               end
                                                                                               imgui.SameLine()
                                                                                               if imgui.Button('Добавить##tpl'..idx) then
                                                                                                               local val = str(S.tpl_input[idx])
                                                                                                               if val ~= '' then
                                                                                                                               tpl.texts = tpl.texts or {}
                                                                                                                               if S.tpl_multiline[idx][0] then
                                                                                                                                               for line in val:gmatch('[^\n]+') do table.insert(tpl.texts, line) end
                                                                                                                               else
                                                                                                                                               table.insert(tpl.texts, val)
                                                                                                                               end
                                                                                                                               imgui.StrCopy(S.tpl_input[idx], '')
                                                                                                               end
                                                                                               end
                                                                                               imgui.EndTabItem()
                                                                               end
                                                               end
                                                               imgui.EndTabBar()
                                               end
                               end
                               if imgui.Button('Сохранить') then
                                                                                                Config.data.type_buttons = funcs.parseList(str(S.type_buttons))
                                                                                                Config.data.objects = funcs.parseList(str(S.objects))
                                                                                                Config.data.prices = funcs.parseList(str(S.prices))
                                                                                                Config.data.currencies = funcs.parseList(str(S.currencies))
                                                                                                Config.data.addons = funcs.parseList(str(S.addons))
                                                                                                Config.data.autocorrect = parse_autocorrect(str(S.autocorrect))
                               Config.data.templates = S.templates_list
                                                                                                Config.data.history_limit = S.history_limit[0]
                                                                                                Config.data.nick_memory_limit = S.nick_memory_limit[0]
                                                                                                Config.data.vip_timer_enabled = S.vip_timer_enabled[0]
                                                                                                Config.data.vip_timer_delay = S.vip_timer_delay[0]
                                                                                                Config.data.btn_timer_enabled = S.btn_timer_enabled[0]
                                                                                                Config.data.btn_timer_delay = S.btn_timer_delay[0]
                                                                                                SMIHelp.timer_send_enabled = S.vip_timer_enabled[0]
                                                                                                SMIHelp.timer_send_delay = S.vip_timer_delay[0]
                                                                                                SMIHelp.btn_timer_enabled = S.btn_timer_enabled[0]
                                                                                                SMIHelp.btn_timer_delay = S.btn_timer_delay[0]
                                                                                                SMIHelp.timer_news_delay = S.timer_news_delay[0]
                                                                                                Config:save()
                                                                                                end
                imgui.EndChild()
end

return SMIHelp
