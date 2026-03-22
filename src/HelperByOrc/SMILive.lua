local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local SMILive = {}

local imgui = require("mimgui")
local paths = require("HelperByOrc.paths")
local mimgui_funcs
local new = imgui.new
local ffi = require("ffi")
local str = ffi.string
local bit = (function()
	local ok, lib = pcall(require, "bit")
	return ok and lib or nil
end)()
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local LiveWindow

local binder
local tags_module
local funcs
local correct_module
local samp_module
local my_hooks_module
local NEWS_INPUT_FLAGS
local escape_imgui_text
local trim

local NEWS_PREFIX = "/news"
local LIVE_SAVE_DEBOUNCE_SEC = 0.5
local RUNTIME_SAVE_DEBOUNCE_SEC = 0.2

local NEWS_INPUT_MAX_LENGTH = 90
local NEWS_INPUT_PANEL_HEIGHT = 120
local NEWS_INPUT_BUFFER_SIZE = 512
SMILive._round_message_buffer_size = SMILive._round_message_buffer_size or 4096
SMILive._min_news_send_interval_ms = 5074

pcall(ffi.cdef, [[
	unsigned long long __stdcall GetTickCount64(void);
]])

-- === Submodules ===

local broadcast_mod = require("HelperByOrc.SMILive.broadcast")
local capitals_editor_mod = require("HelperByOrc.SMILive.capitals_editor")
local news_popup_mod = require("HelperByOrc.SMILive.news_popup")
local math_quiz_mod = require("HelperByOrc.SMILive.math_quiz")
local capitals_quiz_mod = require("HelperByOrc.SMILive.capitals_quiz")
local live_ui_mod = require("HelperByOrc.SMILive.live_ui")

-- === Async helper ===

local function run_async(label, fn)
	if not fn then
		return
	end

	local function wrapped()
		local ok, err = xpcall(fn, debug.traceback)
		if not ok then
			broadcast_mod.update_status(L("smi_live.text.format_format"), label or L("smi_live.text.text"), err)
		end
	end

	if lua_thread and lua_thread.create then
		local ok, err = pcall(lua_thread.create, wrapped)
		if ok then
			return
		end
		broadcast_mod.update_status(L("smi_live.text.format_format_1"), label or "", err)
	end

	wrapped()
end

-- === State objects ===

local NewsInput = {
	buf = new.char[NEWS_INPUT_BUFFER_SIZE](),
	buf_size = NEWS_INPUT_BUFFER_SIZE,
	raw_text = "",
	flattened_full = "",
	body_text = "",
	processed_text = "",
	processed_len = 0,
	preview = "",
	tag_error = nil,
	over_limit = false,
	had_prefix = false,
}

SMILive._news_popup_runtime = SMILive._news_popup_runtime
	or {
		states = {},
		min_row_buffer_size = 256,
		row_buffer_pad = 64,
	}

local SCREENSHOT_MSG_BUF_SIZE = 1024
local screenshot_msg_buf = new.char[SCREENSHOT_MSG_BUF_SIZE]()
local screenshot_msg_last = ""
local live_settings_ui = {
	method_buf = new.int(0),
	interval_buf = new.int(0),
	round_msg_male_buf = new.char[SMILive._round_message_buffer_size](),
	round_msg_female_buf = new.char[SMILive._round_message_buffer_size](),
	round_msg_male_last = "",
	round_msg_female_last = "",
}

local function refreshComputedHelpers()
	NEWS_INPUT_FLAGS = funcs.flags_or(
		imgui.InputTextFlags and imgui.InputTextFlags.NoHorizontalScroll,
		imgui.InputTextFlags and imgui.InputTextFlags.AllowTabInput,
		imgui.InputTextFlags and imgui.InputTextFlags.CtrlEnterForNewLine
	)
	escape_imgui_text = mimgui_funcs.escape_imgui_text
	trim = funcs.trim
end

local function syncDependencies(modules)
	modules = modules or {}
	mimgui_funcs = modules.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
	funcs = modules.funcs or funcs or require("HelperByOrc.funcs")
	correct_module = modules.correct or correct_module or require("HelperByOrc.correct")
	binder = modules.binder or binder
	tags_module = modules.tags or tags_module
	samp_module = modules.samp or samp_module
	refreshComputedHelpers()
end

syncDependencies()

-- === Color constants ===

local COLOR_ACCENT_PRIMARY = imgui.ImVec4(0.25, 0.55, 0.9, 1)
local COLOR_ACCENT_SUCCESS = imgui.ImVec4(0.32, 0.64, 0.32, 1)
local COLOR_ACCENT_DANGER = imgui.ImVec4(0.85, 0.35, 0.35, 1)

local function push_button_palette(base)
	local hover = imgui.ImVec4(math.min(base.x + 0.1, 1), math.min(base.y + 0.1, 1), math.min(base.z + 0.1, 1), base.w)
	local active =
		imgui.ImVec4(math.max(base.x - 0.05, 0), math.max(base.y - 0.05, 0), math.max(base.z - 0.05, 0), base.w)
	imgui.PushStyleColor(imgui.Col.Button, base)
	imgui.PushStyleColor(imgui.Col.ButtonHovered, hover)
	imgui.PushStyleColor(imgui.Col.ButtonActive, active)
end

local function pop_button_palette()
	imgui.PopStyleColor(3)
end

-- === Quiz state objects ===

local MathQuiz = {
	target_scores = { 3, 5 },
	target_index = 1,
	active = false,
	round = 0,
	current_problem = nil,
	current_answer = nil,
	round_answer = nil,
	show_answer = new.bool(false),
	player_id_buf = new.char[8](),
	player_name_buf = new.char[48](),
	custom_problem_buf = new.char[256](),
	custom_error = nil,
	status_text = L("smi_live.text.text_2"),
	players = {},
	winner = nil,
	answer_start_time = nil,
	accepting_answers = false,
	current_responses = {},
	first_correct = nil,
	latest_round_stats = nil,
	awaiting_next_round = false,
	chat_method = 2,
	chat_interval_ms = 750,
}

local CapitalsQuiz = {
	target_scores = { 3, 5 },
	target_index = 1,
	active = false,
	round = 0,
	current_country = nil,
	current_capital = nil,
	current_entry = nil,
	current_answer_norms = nil,
	round_answer = nil,
	round_answer_norms = nil,
	show_answer = new.bool(false),
	player_id_buf = new.char[8](),
	player_name_buf = new.char[48](),
	status_text = L("smi_live.text.text_2"),
	players = {},
	winner = nil,
	answer_start_time = nil,
	accepting_answers = false,
	current_responses = {},
	first_correct = nil,
	latest_round_stats = nil,
	awaiting_next_round = false,
	custom_error = nil,
	entries = {},
	data_error = nil,
	last_entry = nil,
	last_country = nil,
	announced_countries = {},
}

local scoreboard_cache = {
	math = { dirty = true, list = {} },
	capitals = { dirty = true, list = {} },
}

local function mark_math_scoreboard_dirty()
	scoreboard_cache.math.dirty = true
end

local function mark_capitals_scoreboard_dirty()
	scoreboard_cache.capitals.dirty = true
end

local CapitalsEditor = {
	country_buf_size = 96,
	capital_buf_size = 96,
	facts_buf_size = 2048,
	facts_input_height = 110,
	facts_preview_height = 110,
	entries_preview_height = 170,
	fact_row_limit = 85,
	edit_country_buf_size = 96,
	edit_capital_buf_size = 96,
	edit_facts_buf_size = 2048,
	edit_facts_input_height = 140,
	edit_popup_id = "capitals_editor_edit_modal",
	edit_index = nil,
	pending_open_edit_index = nil,
	country_buf = new.char[96](),
	capital_buf = new.char[96](),
	facts_buf = new.char[2048](),
	edit_country_buf = new.char[96](),
	edit_capital_buf = new.char[96](),
	edit_facts_buf = new.char[2048](),
	error = nil,
	edit_error = nil,
}

-- === Live broadcast sections ===

local LIVE_BUFFER_MIN = 4096
local LIVE_BUFFER_PAD = 1024
local LiveBroadcast = {
	math = {
		intro = { text = "" },
		outro = { text = "" },
		reminder = { text = "" },
	},
	capitals = {
		intro = { text = "" },
		outro = { text = "" },
		reminder = { text = "" },
	},
}

local CONFIG_PATH_REL = "SMILive.json"
local _infra = {}

local DEFAULT_METROPOLIS = {
	{
		L("smi_live.text.text_3"),
		L("smi_live.text.text_4"),
		{ L("smi_live.text.text_5") },
		{ L("smi_live.text.text_6") },
		{ L("smi_live.text.text_7"), L("smi_live.text.text_8") },
	},
}

local Config = { data = {} }
local WIN_MESSAGE_MIN_BUFFER = 256
local WinMessageBuffers = { male = nil, female = nil }
local live_save_dirty = false
local live_save_last_change = 0.0
local runtime_save_dirty = false
local runtime_save_last_change = 0.0

-- === Mutable scalars shared via State ===

local State = {
	sms_listener_active = false,
	sms_target_quiz = nil,
	send_sequence_running = false,
	send_sequence_cancel = false,
	live_save_dirty = false,
	live_save_last_change = 0.0,
	runtime_save_dirty = false,
	runtime_save_last_change = 0.0,
	active_live_tab = "math",
}

-- === Dirty-save helpers ===

local function mark_live_save_dirty()
	live_save_dirty = true
	live_save_last_change = os.clock()
end

local function flush_live_save_if_due(force)
	if not live_save_dirty then
		return
	end
	if force or (os.clock() - live_save_last_change) >= LIVE_SAVE_DEBOUNCE_SEC then
		Config:save()
		live_save_dirty = false
	end
end

local function mark_runtime_save_dirty()
	runtime_save_dirty = true
	runtime_save_last_change = os.clock()
	if not (LiveWindow and LiveWindow.show and LiveWindow.show[0]) then
		if runtime_save_dirty then
			Config:saveRuntimeState()
			runtime_save_dirty = false
		end
	end
end

local function flush_runtime_save_if_due(force)
	if not runtime_save_dirty then
		return
	end
	if force or (os.clock() - runtime_save_last_change) >= RUNTIME_SAVE_DEBOUNCE_SEC then
		Config:saveRuntimeState()
		runtime_save_dirty = false
	end
end

-- === Message list sanitization ===

local function sanitize_message_list(list)
	local sanitized = {}
	if type(list) == "table" then
		for _, msg in ipairs(list) do
			if type(msg) == "string" then
				local cleaned = msg:gsub("^%s*(.-)%s*$", "%1")
				if cleaned ~= "" then
					sanitized[#sanitized + 1] = cleaned
				end
			end
		end
	end
	return sanitized
end

-- === Round message templates ===

function SMILive._get_default_round_message_template(gender)
	if gender == "female" then
		return L("smi_live.text.format_format_9")
	end
	return L("smi_live.text.format_format_10")
end

function SMILive._normalize_round_message_templates_text(value, gender)
	local lines = {}
	if type(value) == "table" then
		lines = sanitize_message_list(value)
	else
		local text = tostring(value or "")
		text = text:gsub("\r\n", "\n")
		text = text:gsub("\r", "\n")
		for line in text:gmatch("[^\n]+") do
			local cleaned = line:gsub("^%s*(.-)%s*$", "%1")
			if cleaned ~= "" then
				lines[#lines + 1] = cleaned
			end
		end
	end
	if #lines == 0 then
		return SMILive._get_default_round_message_template(gender)
	end
	return table.concat(lines, "\n")
end

function SMILive._get_round_message_templates_for_gender(gender)
	local normalized_gender = gender == "female" and "female" or "male"
	local math_cfg = Config.data and Config.data.math_quiz or {}
	local round_cfg = type(math_cfg.round_messages) == "table" and math_cfg.round_messages or {}
	local normalized_text = SMILive._normalize_round_message_templates_text(round_cfg[normalized_gender], normalized_gender)
	local templates = {}
	for line in normalized_text:gmatch("[^\r\n]+") do
		local cleaned = line:gsub("^%s*(.-)%s*$", "%1")
		if cleaned ~= "" then
			templates[#templates + 1] = cleaned
		end
	end
	if #templates == 0 then
		templates[1] = SMILive._get_default_round_message_template(normalized_gender)
	end
	return templates
end

-- === Capitals entries sanitization ===

local function sanitize_capitals_entries(entries)
	local cleaned_entries = {}
	if type(entries) ~= "table" then
		return cleaned_entries
	end

	local function safe_trim(value)
		return tostring(value or ""):gsub("^%s*(.-)%s*$", "%1")
	end

	for _, entry in ipairs(entries) do
		if type(entry) == "table" then
			local country = safe_trim(entry[1])
			local capital = safe_trim(entry[2])
			if country ~= "" and capital ~= "" then
				local facts = {}
				for i = 3, #entry do
					local fact_entry = entry[i]
					if type(fact_entry) == "string" then
						local part = safe_trim(fact_entry)
						if part ~= "" then
							facts[#facts + 1] = { part }
						end
					elseif type(fact_entry) == "table" then
						local parts = {}
						for _, part in ipairs(fact_entry) do
							local cleaned = safe_trim(part)
							if cleaned ~= "" then
								parts[#parts + 1] = cleaned
							end
						end
						if #parts > 0 then
							facts[#facts + 1] = parts
						end
					end
				end
				cleaned_entries[#cleaned_entries + 1] = { country = country, capital = capital, facts = facts }
			end
		end
	end

	return cleaned_entries
end

-- === Live buffer management (resizable text inputs) ===

local INPUTTEXT_CALLBACK_RESIZE = imgui.InputTextFlags and imgui.InputTextFlags.CallbackResize
local liveInputResizeCallbackPtr = nil
local currentLiveSection = nil

if INPUTTEXT_CALLBACK_RESIZE then
	local function liveInputResizeCallback(data)
		if not currentLiveSection then
			return 0
		end

		if data.EventFlag and data.EventFlag ~= INPUTTEXT_CALLBACK_RESIZE then
			return 0
		end

		local section = currentLiveSection
		local len = data.BufTextLen or 0
		local text = ffi.string(data.Buf, len)
		local desired = math.max(LIVE_BUFFER_MIN, len + 1 + LIVE_BUFFER_PAD)

		section.buf = imgui.new.char[desired](text)
		section.buf_size = desired
		section.buf_text = text
		data.Buf = section.buf
		data.BufSize = desired

		return 0
	end

	liveInputResizeCallbackPtr = ffi.cast("int (*)(ImGuiInputTextCallbackData*)", liveInputResizeCallback)
end

local function ensure_live_buffer(section)
	section = section or {}
	local text = section.text or ""
	local len = #text
	local desired = math.max(LIVE_BUFFER_MIN, len + LIVE_BUFFER_PAD)
	local currentSize = section.buf_size or 0

	if not section.buf or currentSize < desired then
		currentSize = desired
		section.buf = imgui.new.char[currentSize](text)
		section.buf_size = currentSize
	elseif section.buf_text ~= text then
		imgui.StrCopy(section.buf, text, currentSize)
	end

	section.buf_text = text
	return section.buf, section.buf_size
end

local function update_live_buffer_from_imgui(section, label, height)
	local buf, bufSize = ensure_live_buffer(section)
	local changed
	if liveInputResizeCallbackPtr and INPUTTEXT_CALLBACK_RESIZE then
		currentLiveSection = section
		changed = imgui.InputTextMultiline(
			label,
			buf,
			bufSize,
			imgui.ImVec2(0, height),
			INPUTTEXT_CALLBACK_RESIZE,
			liveInputResizeCallbackPtr
		)
		currentLiveSection = nil
	else
		changed = imgui.InputTextMultiline(label, buf, bufSize, imgui.ImVec2(0, height))
	end

	local activeBuf = section.buf or buf
	if changed then
		local newText = ffi.string(activeBuf)
		section.text = newText
		section.buf_text = newText
		if not (liveInputResizeCallbackPtr and INPUTTEXT_CALLBACK_RESIZE) then
			local needed = math.max(LIVE_BUFFER_MIN, #newText + LIVE_BUFFER_PAD)
			if needed > (section.buf_size or 0) then
				section.buf = imgui.new.char[needed](newText)
				section.buf_size = needed
				section.buf_text = newText
				activeBuf = section.buf
			end
		end
		section.buf_size = section.buf_size or bufSize
	end

	if not section.text then
		section.text = section.buf_text or ""
	end
	return not not changed
end

-- === Config:load / Config:save ===

function Config:load()
	local data = funcs.loadTableFromJson(CONFIG_PATH_REL, {})

	if type(data) ~= "table" then
		data = {}
	end

	local math_cfg = type(data.math_quiz) == "table" and data.math_quiz or {}
	local general_cfg = type(data.quiz) == "table" and data.quiz or {}
	local capitals_cfg = type(data.capitals_quiz) == "table" and data.capitals_quiz or {}
	if type(capitals_cfg.metropolis) ~= "table" or #capitals_cfg.metropolis == 0 then
		capitals_cfg.metropolis = DEFAULT_METROPOLIS
	end
	general_cfg.screenshot_message = tostring(general_cfg.screenshot_message or "")

	local math_live_cfg = type(math_cfg.live_broadcast) == "table" and math_cfg.live_broadcast or {}
	math_live_cfg.intro = tostring(math_live_cfg.intro or "")
	math_live_cfg.outro = tostring(math_live_cfg.outro or "")
	math_live_cfg.reminder = tostring(math_live_cfg.reminder or "")
	math_cfg.live_broadcast = math_live_cfg

	local capitals_live_cfg = type(capitals_cfg.live_broadcast) == "table" and capitals_cfg.live_broadcast or {}
	capitals_live_cfg.intro = tostring(capitals_live_cfg.intro or "")
	capitals_live_cfg.outro = tostring(capitals_live_cfg.outro or "")
	capitals_live_cfg.reminder = tostring(capitals_live_cfg.reminder or "")
	capitals_cfg.live_broadcast = capitals_live_cfg

	LiveBroadcast.math.intro.text = math_live_cfg.intro
	LiveBroadcast.math.outro.text = math_live_cfg.outro
	LiveBroadcast.math.reminder.text = math_live_cfg.reminder
	ensure_live_buffer(LiveBroadcast.math.intro)
	ensure_live_buffer(LiveBroadcast.math.outro)
	ensure_live_buffer(LiveBroadcast.math.reminder)

	LiveBroadcast.capitals.intro.text = capitals_live_cfg.intro
	LiveBroadcast.capitals.outro.text = capitals_live_cfg.outro
	LiveBroadcast.capitals.reminder.text = capitals_live_cfg.reminder
	ensure_live_buffer(LiveBroadcast.capitals.intro)
	ensure_live_buffer(LiveBroadcast.capitals.outro)
	ensure_live_buffer(LiveBroadcast.capitals.reminder)

	local win_cfg = type(math_cfg.win_messages) == "table" and math_cfg.win_messages or {}
	win_cfg.male = sanitize_message_list(win_cfg.male)
	win_cfg.female = sanitize_message_list(win_cfg.female)
	math_cfg.win_messages = win_cfg
	local round_cfg = type(math_cfg.round_messages) == "table" and math_cfg.round_messages or {}
	round_cfg.male = SMILive._normalize_round_message_templates_text(round_cfg.male, "male")
	round_cfg.female = SMILive._normalize_round_message_templates_text(round_cfg.female, "female")
	math_cfg.round_messages = round_cfg

	local saved_method = tonumber(general_cfg.chat_method)
	if saved_method then
		saved_method = math.floor(saved_method)
		if saved_method < 0 then
			saved_method = 0
		end
	else
		saved_method = MathQuiz.chat_method or 0
	end
	MathQuiz.chat_method = saved_method
	general_cfg.chat_method = saved_method

	local saved_interval = tonumber(general_cfg.chat_interval_ms)
	if saved_interval then
		saved_interval = math.floor(saved_interval + 0.5)
		if saved_interval < 0 then
			saved_interval = 0
		end
	else
		saved_interval = MathQuiz.chat_interval_ms or 0
	end
	MathQuiz.chat_interval_ms = saved_interval
	general_cfg.chat_interval_ms = saved_interval

	local saved_target = tonumber(math_cfg.target_index)
	if saved_target then
		saved_target = math.floor(saved_target)
	else
		saved_target = MathQuiz.target_index or 1
	end
	if saved_target < 1 then
		saved_target = 1
	end
	local max_target = #MathQuiz.target_scores
	if max_target > 0 and saved_target > max_target then
		saved_target = max_target
	end
	MathQuiz.target_index = saved_target
	math_cfg.target_index = saved_target

	local saved_capitals_target = tonumber(capitals_cfg.target_index)
	if saved_capitals_target then
		saved_capitals_target = math.floor(saved_capitals_target)
	else
		saved_capitals_target = CapitalsQuiz.target_index or 1
	end
	if saved_capitals_target < 1 then
		saved_capitals_target = 1
	end
	local max_capitals_target = #CapitalsQuiz.target_scores
	if max_capitals_target > 0 and saved_capitals_target > max_capitals_target then
		saved_capitals_target = max_capitals_target
	end
	CapitalsQuiz.target_index = saved_capitals_target
	capitals_cfg.target_index = saved_capitals_target

	CapitalsQuiz.entries = sanitize_capitals_entries(capitals_cfg.metropolis)
	if #CapitalsQuiz.entries == 0 then
		CapitalsQuiz.data_error = L("smi_live.text.capitals_quiz_metropolis_smilive_json")
	else
		CapitalsQuiz.data_error = nil
	end

	data.quiz = general_cfg
	data.math_quiz = math_cfg
	data.capitals_quiz = capitals_cfg
	self.data = data
	live_ui_mod.load_win_message_buffers_from_config()
end

function Config:save()
	local data = self.data or {}

	local general_cfg = type(data.quiz) == "table" and data.quiz or {}
	local method = math.floor(tonumber(MathQuiz.chat_method) or 0)
	if method < 0 then
		method = 0
	end
	general_cfg.chat_method = method

	local interval = math.floor(tonumber(MathQuiz.chat_interval_ms) or 0)
	if interval < 0 then
		interval = 0
	end
	general_cfg.chat_interval_ms = interval
	general_cfg.screenshot_message = tostring(general_cfg.screenshot_message or "")
	data.quiz = general_cfg

	local math_cfg = type(data.math_quiz) == "table" and data.math_quiz or {}
	local math_live_cfg = type(math_cfg.live_broadcast) == "table" and math_cfg.live_broadcast or {}
	math_live_cfg.intro = tostring((LiveBroadcast.math.intro and LiveBroadcast.math.intro.text) or "")
	math_live_cfg.outro = tostring((LiveBroadcast.math.outro and LiveBroadcast.math.outro.text) or "")
	math_live_cfg.reminder = tostring((LiveBroadcast.math.reminder and LiveBroadcast.math.reminder.text) or "")
	math_cfg.live_broadcast = math_live_cfg

	local target_index = math.floor(tonumber(MathQuiz.target_index) or 1)
	if target_index < 1 then
		target_index = 1
	end
	local max_target = #MathQuiz.target_scores
	if max_target > 0 and target_index > max_target then
		target_index = max_target
	end
	math_cfg.target_index = target_index

	local win_cfg = type(math_cfg.win_messages) == "table" and math_cfg.win_messages or {}
	win_cfg.male = sanitize_message_list(win_cfg.male)
	win_cfg.female = sanitize_message_list(win_cfg.female)
	math_cfg.win_messages = win_cfg
	local round_cfg = type(math_cfg.round_messages) == "table" and math_cfg.round_messages or {}
	round_cfg.male = SMILive._normalize_round_message_templates_text(round_cfg.male, "male")
	round_cfg.female = SMILive._normalize_round_message_templates_text(round_cfg.female, "female")
	math_cfg.round_messages = round_cfg

	local capitals_cfg = type(data.capitals_quiz) == "table" and data.capitals_quiz or {}
	if type(capitals_cfg.metropolis) ~= "table" or #capitals_cfg.metropolis == 0 then
		capitals_cfg.metropolis = DEFAULT_METROPOLIS
	end
	local capitals_live_cfg = type(capitals_cfg.live_broadcast) == "table" and capitals_cfg.live_broadcast or {}
	capitals_live_cfg.intro = tostring((LiveBroadcast.capitals.intro and LiveBroadcast.capitals.intro.text) or "")
	capitals_live_cfg.outro = tostring((LiveBroadcast.capitals.outro and LiveBroadcast.capitals.outro.text) or "")
	capitals_live_cfg.reminder = tostring((LiveBroadcast.capitals.reminder and LiveBroadcast.capitals.reminder.text) or "")
	capitals_cfg.live_broadcast = capitals_live_cfg
	local capitals_target = math.floor(tonumber(CapitalsQuiz.target_index) or 1)
	if capitals_target < 1 then
		capitals_target = 1
	end
	local max_capitals_target = #CapitalsQuiz.target_scores
	if max_capitals_target > 0 and capitals_target > max_capitals_target then
		capitals_target = max_capitals_target
	end
	capitals_cfg.target_index = capitals_target

	math_cfg.chat_method = nil
	math_cfg.chat_interval_ms = nil

	data.win_messages = nil
	data.live_broadcast = nil

	data.math_quiz = math_cfg
	data.capitals_quiz = capitals_cfg
	self.data = data

	if _infra.cm then
		_infra.cm.markDirty("SMILive")
	else
		funcs.saveTableToJson(data, CONFIG_PATH_REL)
	end
end

-- === Runtime state save/load ===

function Config:getRuntimePath()
	local base = funcs.resolveJsonPath(CONFIG_PATH_REL)
	local path = (base:gsub("%.json$", "")) .. ".runtime.json"
	return path
end

function Config:clearRuntimeState()
	runtime_save_dirty = false
	runtime_save_last_change = 0.0
	local path = self:getRuntimePath()
	local f = io.open(path, "rb")
	if f then
		f:close()
		pcall(os.remove, path)
	end
end

function Config:saveRuntimeState()
	local function copy_players_map(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for name, data in pairs(src) do
			if type(name) == "string" and type(data) == "table" then
				local pid = tonumber(data.player_id)
				if pid then
					pid = math.floor(pid)
					if pid < 0 or pid > 1003 then
						pid = nil
					end
				end
				local score = math.floor(tonumber(data.score) or 0)
				if score < 0 then
					score = 0
				end
				out[name] = {
					score = score,
					last_correct = data.last_correct and true or false,
					last_answer = data.last_answer,
					player_id = pid,
				}
			end
		end
		return out
	end

	local function copy_responses(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for _, item in ipairs(src) do
			if type(item) == "table" then
				local pid = tonumber(item.player_id)
				if pid then
					pid = math.floor(pid)
					if pid < 0 or pid > 1003 then
						pid = nil
					end
				end
				local rt = tonumber(item.response_time)
				if rt and rt < 0 then
					rt = 0
				end
				out[#out + 1] = {
					name = tostring(item.name or ""),
					player_id = pid,
					text = tostring(item.text or ""),
					answer_text = item.answer_text ~= nil and tostring(item.answer_text) or nil,
					numeric = tonumber(item.numeric),
					response_time = rt,
					correct = item.correct and true or false,
					outcome = tostring(item.outcome or "attempt"),
				}
			end
		end
		return out
	end

	local function copy_latest_stats(src)
		if type(src) ~= "table" then
			return nil
		end
		local pid = tonumber(src.player_id)
		if pid then
			pid = math.floor(pid)
			if pid < 0 or pid > 1003 then
				pid = nil
			end
		end
		local total = math.floor(tonumber(src.total_responses) or 0)
		if total < 0 then
			total = 0
		end
		local score = math.floor(tonumber(src.score) or 0)
		if score < 0 then
			score = 0
		end
		local awarded = math.floor(tonumber(src.points_awarded) or 0)
		if awarded < 0 then
			awarded = 0
		end

		local norms = nil
		if type(src.correct_answer_norms) == "table" then
			norms = {}
			for _, v in ipairs(src.correct_answer_norms) do
				if type(v) == "string" and v ~= "" then
					norms[#norms + 1] = v
				end
			end
		end

		return {
			winner = tostring(src.winner or ""),
			player_id = pid,
			response_time = tonumber(src.response_time),
			lead = tonumber(src.lead),
			total_responses = total,
			correct_answer = src.correct_answer,
			correct_answer_norms = norms,
			country = src.country ~= nil and tostring(src.country) or nil,
			score = score,
			points_awarded = awarded,
			game_finished = src.game_finished and true or false,
			manual = src.manual and true or false,
			recommended = src.recommended and true or false,
		}
	end

	local function copy_bool_map(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for key, value in pairs(src) do
			if value and type(key) == "string" and key ~= "" then
				out[key] = true
			end
		end
		return out
	end

	local payload = {
		version = 1,
		saved_at = os.time(),
		math = {
			players = copy_players_map(MathQuiz.players),
			current_responses = copy_responses(MathQuiz.current_responses),
			latest_round_stats = copy_latest_stats(MathQuiz.latest_round_stats),
			round = math.floor(tonumber(MathQuiz.round) or 0),
			winner = MathQuiz.winner ~= nil and tostring(MathQuiz.winner) or nil,
		},
		capitals = {
			players = copy_players_map(CapitalsQuiz.players),
			current_responses = copy_responses(CapitalsQuiz.current_responses),
			latest_round_stats = copy_latest_stats(CapitalsQuiz.latest_round_stats),
			round = math.floor(tonumber(CapitalsQuiz.round) or 0),
			winner = CapitalsQuiz.winner ~= nil and tostring(CapitalsQuiz.winner) or nil,
			announced_countries = copy_bool_map(CapitalsQuiz.announced_countries),
		},
	}

	local path = self:getRuntimePath()
	funcs.saveTableToJson(payload, path)
end

function Config:loadRuntimeState()
	local function sanitize_players(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for name, data in pairs(src) do
			if type(name) == "string" and type(data) == "table" then
				local cleaned = tostring(name):gsub("^%s*(.-)%s*$", "%1")
				if cleaned ~= "" then
					local pid = tonumber(data.player_id)
					if pid then
						pid = math.floor(pid)
						if pid < 0 or pid > 1003 then
							pid = nil
						end
					end
					local score = math.floor(tonumber(data.score) or 0)
					if score < 0 then
						score = 0
					end
					out[cleaned] = {
						score = score,
						last_correct = data.last_correct and true or false,
						last_answer = data.last_answer,
						player_id = pid,
					}
				end
			end
		end
		return out
	end

	local function sanitize_responses(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for _, item in ipairs(src) do
			if type(item) == "table" then
				local name = tostring(item.name or ""):gsub("^%s*(.-)%s*$", "%1")
				if name ~= "" then
					local pid = tonumber(item.player_id)
					if pid then
						pid = math.floor(pid)
						if pid < 0 or pid > 1003 then
							pid = nil
						end
					end
					local rt = tonumber(item.response_time)
					if rt and rt < 0 then
						rt = 0
					end
					out[#out + 1] = {
						name = name,
						player_id = pid,
						text = tostring(item.text or ""),
						answer_text = item.answer_text ~= nil and tostring(item.answer_text) or nil,
						numeric = tonumber(item.numeric),
						response_time = rt,
						correct = item.correct and true or false,
						outcome = tostring(item.outcome or "attempt"),
					}
				end
			end
		end
		return out
	end

	local function sanitize_latest_stats(src)
		if type(src) ~= "table" then
			return nil
		end
		local winner = tostring(src.winner or ""):gsub("^%s*(.-)%s*$", "%1")
		if winner == "" then
			return nil
		end
		local pid = tonumber(src.player_id)
		if pid then
			pid = math.floor(pid)
			if pid < 0 or pid > 1003 then
				pid = nil
			end
		end
		local total = math.floor(tonumber(src.total_responses) or 0)
		if total < 0 then
			total = 0
		end
		local score = math.floor(tonumber(src.score) or 0)
		if score < 0 then
			score = 0
		end
		local awarded = math.floor(tonumber(src.points_awarded) or 0)
		if awarded < 0 then
			awarded = 0
		end
		local norms = nil
		if type(src.correct_answer_norms) == "table" then
			norms = {}
			for _, v in ipairs(src.correct_answer_norms) do
				if type(v) == "string" and v ~= "" then
					norms[#norms + 1] = v
				end
			end
		end
		return {
			winner = winner,
			player_id = pid,
			response_time = tonumber(src.response_time),
			lead = tonumber(src.lead),
			total_responses = total,
			correct_answer = src.correct_answer,
			correct_answer_norms = norms,
			country = src.country ~= nil and tostring(src.country) or nil,
			score = score,
			points_awarded = awarded,
			game_finished = src.game_finished and true or false,
			manual = src.manual and true or false,
			recommended = src.recommended and true or false,
		}
	end

	local function sanitize_bool_map(src)
		local out = {}
		if type(src) ~= "table" then
			return out
		end
		for key, value in pairs(src) do
			if value and type(key) == "string" then
				local cleaned = key:gsub("^%s*(.-)%s*$", "%1")
				if cleaned ~= "" then
					out[cleaned] = true
				end
			end
		end
		return out
	end

	local path = self:getRuntimePath()
	local raw = funcs.readFile(path, "rb")
	if type(raw) ~= "string" or raw == "" then
		return false
	end
	local data = funcs.decodeJsonSafe(raw)
	if type(data) ~= "table" then
		return false
	end

	local math_state = type(data.math) == "table" and data.math or {}
	local capitals_state = type(data.capitals) == "table" and data.capitals or {}

	MathQuiz.players = sanitize_players(math_state.players)
	MathQuiz.current_responses = sanitize_responses(math_state.current_responses)
	MathQuiz.latest_round_stats = sanitize_latest_stats(math_state.latest_round_stats)
	MathQuiz.round = math.max(0, math.floor(tonumber(math_state.round) or 0))
	MathQuiz.winner = type(math_state.winner) == "string" and math_state.winner ~= "" and math_state.winner or nil

	CapitalsQuiz.players = sanitize_players(capitals_state.players)
	CapitalsQuiz.current_responses = sanitize_responses(capitals_state.current_responses)
	CapitalsQuiz.latest_round_stats = sanitize_latest_stats(capitals_state.latest_round_stats)
	CapitalsQuiz.round = math.max(0, math.floor(tonumber(capitals_state.round) or 0))
	CapitalsQuiz.winner =
		type(capitals_state.winner) == "string" and capitals_state.winner ~= "" and capitals_state.winner or nil
	CapitalsQuiz.announced_countries = sanitize_bool_map(capitals_state.announced_countries)
	mark_math_scoreboard_dirty()
	mark_capitals_scoreboard_dirty()

	return true
end

-- === Shared send targets ===

local default_send_labels = {
	L("smi_live.text.text_11"),
	L("smi_live.text.sa_mp"),
	L("smi_live.text.text_12"),
	L("smi_live.text.text_13"),
	L("smi_live.text.text_14"),
	L("smi_live.text.text_15"),
	L("smi_live.text.text_16"),
	L("smi_live.text.text_17"),
	L("smi_live.text.sf"),
	L("smi_live.text.text_18"),
}
local default_send_labels_ffi = imgui.new["const char*"][#default_send_labels](default_send_labels)

-- === Shared helpers (passed to submodules via ctx) ===

local function pluralize_points(value)
	local amount = math.floor(tonumber(value) or 0)
	local abs_amount = math.abs(amount)
	local n100 = abs_amount % 100
	local n10 = abs_amount % 10
	local suffix
	if n100 >= 11 and n100 <= 14 then
		suffix = L("smi_live.text.text_19")
	elseif n10 == 1 then
		suffix = L("smi_live.text.text_20")
	elseif n10 >= 2 and n10 <= 4 then
		suffix = L("smi_live.text.text_21")
	else
		suffix = L("smi_live.text.text_19")
	end
	return string.format("%d %s", amount, suffix)
end

local function format_score_progress(total, gained, gender)
	gender = gender == "female" and "female" or "male"
	local earned_verb = gender == "female" and L("smi_live.text.text_22") or L("smi_live.text.text_23")
	local possessive_phrase = gender == "female" and L("smi_live.text.text_24") or L("smi_live.text.text_25")

	gained = math.max(0, math.floor(tonumber(gained) or 0))
	total = math.max(0, math.floor(tonumber(total) or 0))
	if total <= gained then
		if gained > 0 then
			return string.format("%s %s!", earned_verb, pluralize_points(gained))
		end
		return string.format(L("smi_live.text.format_0"), earned_verb)
	end

	local parts = {}
	if gained > 0 then
		parts[#parts + 1] = string.format("%s %s!", earned_verb, pluralize_points(gained))
	end
	parts[#parts + 1] = string.format("%s %s!", possessive_phrase, pluralize_points(total))
	return table.concat(parts, " ")
end

function SMILive._get_quiz_target_points(quiz_type)
	local normalized = tostring(quiz_type or ""):lower()
	local quiz

	if normalized == "math" then
		quiz = MathQuiz
	elseif normalized == "capitals" then
		quiz = CapitalsQuiz
	else
		return 0
	end

	local scores = quiz and quiz.target_scores
	if type(scores) ~= "table" or #scores == 0 then
		return 0
	end

	local idx = math.floor(tonumber(quiz.target_index) or 1)
	if idx < 1 then
		idx = 1
	elseif idx > #scores then
		idx = #scores
	end

	local numeric = tonumber(scores[idx])
	if numeric == nil or numeric ~= numeric then
		return 0
	end

	return numeric
end

SMILive._live_text_variable_resolvers = {
	balls = function(context)
		return SMILive._get_quiz_target_points(context and context.quiz_kind or nil)
	end,
}

function SMILive._apply_live_broadcast_text_variables(text, quiz_kind)
	local source = tostring(text or "")
	if source == "" then
		return ""
	end

	local context = {
		quiz_kind = quiz_kind,
	}

	local replaced = source:gsub("{([%w_]+)}", function(raw_key)
		local key = tostring(raw_key or ""):lower()
		local resolver = SMILive._live_text_variable_resolvers[key]
		if type(resolver) ~= "function" then
			return "{" .. raw_key .. "}"
		end

		local ok, value = pcall(resolver, context)
		if not ok or value == nil then
			return ""
		end

		return tostring(value)
	end)

	return replaced
end

function SMILive._get_live_section_news_messages(section, quiz_kind)
	local raw_text = ""
	if type(section) == "table" then
		raw_text = section.text or section.buf_text or ""
	elseif type(section) == "string" then
		raw_text = section
	elseif section ~= nil then
		local ok, value = pcall(str, section)
		if ok and type(value) == "string" then
			raw_text = value
		else
			raw_text = tostring(section)
		end
	end

	local prepared_text = SMILive._apply_live_broadcast_text_variables(raw_text, quiz_kind)
	return news_popup_mod.create_news_messages_from_text(prepared_text)
end

local function normalize_player_name(name)
	local cleaned = trim(name)
	if cleaned == "" then
		return ""
	end
	return cleaned:gsub("_+", " ")
end

local function format_player_label(name, player_id)
	local normalized = normalize_player_name(name)
	if normalized == "" then
		normalized = trim(name)
	end
	local base = normalized ~= "" and normalized or "-"
	if player_id ~= nil and tostring(player_id) ~= "" then
		base = base .. "[" .. tostring(player_id) .. "]"
	end
	return base
end

local function format_broadcast_name(name, player_id)
	local id = tonumber(player_id)
	if id and tags_module then
		return string.format("[rpnick(%d)]", id)
	end
	return normalize_player_name(name)
end

local function format_display_name(name, player_id)
	local normalized = normalize_player_name(name)
	if normalized == "" then
		normalized = trim(name)
	end
	if normalized == "" then
		return L("smi_live.text.text_26")
	end
	local broadcast_name = format_broadcast_name(normalized, player_id)
	return broadcast_name ~= "" and broadcast_name or normalized
end

local function stop_sms_for_announcement()
	if broadcast_mod.stop_sms_listener then
		broadcast_mod.stop_sms_listener(true)
	end
end

local function strip_color_codes(text)
	return (tostring(text or ""):gsub("{%x%x%x%x%x%x}", ""))
end

local function format_seconds(value)
	if not value then
		return "-"
	end
	return string.format(L("smi_live.text.text_2f"), value)
end

local function compute_lead_time(first_response, responses)
	if not first_response or not first_response.response_time then
		return nil
	end
	if type(responses) ~= "table" or #responses <= 1 then
		return nil
	end
	local first_time = first_response.response_time
	local second_time = nil
	for _, resp in ipairs(responses) do
		if resp ~= first_response and resp.correct and resp.response_time then
			if not second_time or resp.response_time < second_time then
				second_time = resp.response_time
			end
		end
	end
	if not second_time then
		return nil
	end
	return second_time - first_time
end

local function find_response_for_player(responses, player_name, player_id, only_correct)
	if type(responses) ~= "table" then
		return nil
	end
	local normalized = normalize_player_name(player_name)
	for _, resp in ipairs(responses) do
		if not only_correct or resp.correct then
			local resp_name = normalize_player_name(resp.name or "")
			if resp_name == normalized or (player_id and resp.player_id == player_id) then
				return resp
			end
		end
	end
	return nil
end

local function parse_player_id_from_buf(id_buf)
	if not id_buf then
		return nil
	end
	local raw = trim(str(id_buf))
	if raw == "" then
		return nil
	end
	local id = tonumber(raw)
	if not id then
		return nil, L("smi_live.text.id")
	end
	id = math.floor(id)
	if id < 0 or id > 1003 then
		return nil, L("smi_live.text.id_0_1003")
	end
	return id
end

local function try_fill_name_from_id(name_buf, player_id)
	if not name_buf or not player_id or not samp_module or type(samp_module.GetNameID) ~= "function" then
		return nil
	end
	local ok, resolved_name = pcall(samp_module.GetNameID, player_id)
	resolved_name = ok and trim(tostring(resolved_name or "")) or ""
	if resolved_name ~= "" and resolved_name ~= "UNKNOWN" then
		imgui.StrCopy(name_buf, resolved_name)
		return resolved_name
	end
	return nil
end

local function try_fill_id_from_name(id_buf, name)
	if not id_buf or name == "" or not samp_module or type(samp_module.GetIDByName) ~= "function" then
		return nil
	end
	local ok, resolved_id = pcall(samp_module.GetIDByName, name)
	if not ok or resolved_id == nil then
		return nil
	end
	resolved_id = math.floor(tonumber(resolved_id) or -1)
	if resolved_id < 0 or resolved_id > 1003 then
		return nil
	end
	imgui.StrCopy(id_buf, tostring(resolved_id))
	return resolved_id
end

local function resolve_player_from_inputs(id_buf, name_buf)
	if not name_buf then
		return nil, nil, L("smi_live.text.text_27")
	end
	local name = trim(str(name_buf))
	local player_id, id_error = parse_player_id_from_buf(id_buf)
	if id_error then
		return nil, nil, id_error
	end

	if name == "" and player_id ~= nil then
		local resolved_name = try_fill_name_from_id(name_buf, player_id)
		name = trim(resolved_name or str(name_buf))
		if name == "" then
			return nil, nil, L("smi_live.text.id_28")
		end
	elseif name ~= "" and player_id == nil then
		player_id = try_fill_id_from_name(id_buf, name)
	elseif name ~= "" and player_id ~= nil then
		local resolved_id = try_fill_id_from_name(id_buf, name)
		if resolved_id ~= nil and resolved_id ~= player_id then
			return nil, nil, string.format(
				L("smi_live.text.id_number_format_number"),
				player_id,
				name,
				resolved_id
			)
		end
	end

	if name == "" then
		return nil, nil, L("smi_live.text.id_29")
	end

	return name, player_id
end

local function build_news_message_from_text(text)
	local cleaned = trim(text)
	if cleaned == "" then
		return nil, 0
	end
	local decoded = u8:decode(tostring(cleaned or ""))
	local length = #decoded
	if length > NEWS_INPUT_MAX_LENGTH then
		return nil, length
	end
	return string.format("%s %s", NEWS_PREFIX, cleaned), length
end

-- === News send timing (stored on SMILive table, shared with broadcast) ===

SMILive._news_send_timing = SMILive._news_send_timing or {}

-- === Build ctx and init submodules ===

LiveWindow = {
	show = new.bool(false),
}

local ctx = {
	-- State objects
	SMILive = SMILive,
	State = State,
	MathQuiz = MathQuiz,
	CapitalsQuiz = CapitalsQuiz,
	CapitalsEditor = CapitalsEditor,
	Config = Config,
	LiveBroadcast = LiveBroadcast,
	LiveWindow = LiveWindow,
	NewsInput = NewsInput,
	WinMessageBuffers = WinMessageBuffers,
	scoreboard_cache = scoreboard_cache,
	live_settings_ui = live_settings_ui,

	-- Constants
	NEWS_PREFIX = NEWS_PREFIX,
	NEWS_INPUT_MAX_LENGTH = NEWS_INPUT_MAX_LENGTH,
	NEWS_INPUT_PANEL_HEIGHT = NEWS_INPUT_PANEL_HEIGHT,
	NEWS_INPUT_BUFFER_SIZE = NEWS_INPUT_BUFFER_SIZE,
	NEWS_INPUT_FLAGS = NEWS_INPUT_FLAGS,
	WIN_MESSAGE_MIN_BUFFER = WIN_MESSAGE_MIN_BUFFER,
	SCREENSHOT_MSG_BUF_SIZE = SCREENSHOT_MSG_BUF_SIZE,
	screenshot_msg_buf = screenshot_msg_buf,
	screenshot_msg_last = screenshot_msg_last,
	COLOR_ACCENT_PRIMARY = COLOR_ACCENT_PRIMARY,
	COLOR_ACCENT_SUCCESS = COLOR_ACCENT_SUCCESS,
	COLOR_ACCENT_DANGER = COLOR_ACCENT_DANGER,
	default_send_labels = default_send_labels,
	default_send_labels_ffi = default_send_labels_ffi,

	-- Dependencies
	binder = binder,
	tags_module = tags_module,
	my_hooks_module = my_hooks_module,
	funcs = funcs,
	mimgui_funcs = mimgui_funcs,
	correct_module = correct_module,
	samp_module = samp_module,

	-- Helpers
	trim = trim,
	escape_imgui_text = escape_imgui_text,
	run_async = run_async,
	push_button_palette = push_button_palette,
	pop_button_palette = pop_button_palette,
	normalize_player_name = normalize_player_name,
	format_player_label = format_player_label,
	format_broadcast_name = format_broadcast_name,
	format_display_name = format_display_name,
	format_score_progress = format_score_progress,
	format_seconds = format_seconds,
	pluralize_points = pluralize_points,
	compute_lead_time = compute_lead_time,
	find_response_for_player = find_response_for_player,
	stop_sms_for_announcement = stop_sms_for_announcement,
	strip_color_codes = strip_color_codes,
	resolve_player_from_inputs = resolve_player_from_inputs,
	parse_player_id_from_buf = parse_player_id_from_buf,
	try_fill_name_from_id = try_fill_name_from_id,
	try_fill_id_from_name = try_fill_id_from_name,
	sanitize_message_list = sanitize_message_list,
	build_news_message_from_text = build_news_message_from_text,
	update_live_buffer_from_imgui = update_live_buffer_from_imgui,
	mark_live_save_dirty = mark_live_save_dirty,
	flush_live_save_if_due = flush_live_save_if_due,
	mark_runtime_save_dirty = mark_runtime_save_dirty,
	flush_runtime_save_if_due = flush_runtime_save_if_due,
	mark_math_scoreboard_dirty = mark_math_scoreboard_dirty,
	mark_capitals_scoreboard_dirty = mark_capitals_scoreboard_dirty,

	-- Submodule refs (filled after init)
	broadcast_mod = broadcast_mod,
	capitals_editor_mod = capitals_editor_mod,
	news_popup_mod = news_popup_mod,
	math_quiz_mod = math_quiz_mod,
	capitals_quiz_mod = capitals_quiz_mod,
	live_ui_mod = live_ui_mod,
}

-- Init all submodules
broadcast_mod.init(ctx)
capitals_editor_mod.init(ctx)
news_popup_mod.init(ctx)
math_quiz_mod.init(ctx)
capitals_quiz_mod.init(ctx)
live_ui_mod.init(ctx)

-- === Cross-module wiring (after all init) ===

-- broadcast needs SMS handlers from quiz modules
ctx.record_math_sms = math_quiz_mod.record_response_from_sms
ctx.record_capitals_sms = capitals_quiz_mod.record_response_from_sms

-- live_ui needs quiz/editor draw functions
ctx.DrawMathQuiz = math_quiz_mod.DrawMathQuiz
ctx.DrawCapitalsQuiz = capitals_quiz_mod.DrawCapitalsQuiz
ctx.DrawCapitalsEditor = capitals_editor_mod.draw

-- quiz modules need broadcast functions (already available via ctx.broadcast_mod)
ctx.update_status = broadcast_mod.update_status
ctx.broadcast_sequence = broadcast_mod.broadcast_sequence
ctx.start_sms_listener = broadcast_mod.start_sms_listener
ctx.stop_sms_listener = broadcast_mod.stop_sms_listener
ctx.get_selected_method = broadcast_mod.get_selected_method
ctx.broadcast_problem = broadcast_mod.broadcast_problem
ctx.broadcast_correct_answer_gender = broadcast_mod.broadcast_correct_answer_gender
ctx.broadcast_winner_gender = broadcast_mod.broadcast_winner_gender

-- live_ui needs win messages and send targets
ctx.get_win_messages_for_gender = live_ui_mod.get_win_messages_for_gender
ctx.build_send_tooltip = live_ui_mod.build_send_tooltip
ctx.combine_send_tooltip = live_ui_mod.combine_send_tooltip

-- news_popup needs broadcast functions
ctx.take_live_window_screenshot = broadcast_mod.take_live_window_screenshot
ctx.cancel_send_queue = broadcast_mod.cancel_send_queue

-- math_quiz needs capitals reset
ctx.reset_capitals_buffers = capitals_quiz_mod.reset_capitals_buffers

-- === Load config (after all submodules are initialized) ===

Config:load()
Config:loadRuntimeState()

-- === Update news input state ===

news_popup_mod.update_news_input_state()

-- === imgui.OnFrame subscription ===

SMILive._imguiSub = imgui.OnFrame(function()
	return LiveWindow.show[0]
end, function()
	imgui.SetNextWindowSize(imgui.ImVec2(520, 480), imgui.Cond.FirstUseEver)
	local opened = imgui.Begin(L("smi_live.text.smi_live"), LiveWindow.show, imgui.WindowFlags.NoCollapse)
	if opened then
		live_ui_mod._draw_live_window()
	end
	if mimgui_funcs and mimgui_funcs.clampCurrentWindowToScreen then
		mimgui_funcs.clampCurrentWindowToScreen(5)
	end
	imgui.End()
	if live_save_dirty and not LiveWindow.show[0] then
		flush_live_save_if_due(true)
	end
	if runtime_save_dirty and not LiveWindow.show[0] then
		flush_runtime_save_if_due(true)
	end
end)

-- === Lifecycle ===

function SMILive.onTerminate(reason)
	if SMILive._imguiSub and type(SMILive._imguiSub.Unsubscribe) == "function" then
		pcall(SMILive._imguiSub.Unsubscribe, SMILive._imguiSub)
		SMILive._imguiSub = nil
	end
	if _infra.eb then
		_infra.eb.offByOwner("SMILive")
	end
	if State.sms_listener_active and broadcast_mod.stop_sms_listener then
		pcall(broadcast_mod.stop_sms_listener, true)
	end
	State.sms_listener_active = false

	if LiveWindow and LiveWindow.show then
		LiveWindow.show[0] = false
	end

	flush_live_save_if_due(true)
	flush_runtime_save_if_due(true)

	if liveInputResizeCallbackPtr and type(liveInputResizeCallbackPtr.free) == "function" then
		pcall(liveInputResizeCallbackPtr.free, liveInputResizeCallbackPtr)
		liveInputResizeCallbackPtr = nil
	end
	currentLiveSection = nil
end

function SMILive.attachModules(modules)
	local resume_listener = State.sms_listener_active
	if State.sms_listener_active and broadcast_mod.can_use_sms_listener() then
		broadcast_mod.stop_sms_listener(true)
	else
		State.sms_listener_active = false
	end

	if modules then
		binder = modules.binder
		tags_module = modules.tags
		my_hooks_module = modules.my_hooks
		samp_module = modules.samp
	end
	_infra.cm = modules.config_manager
	_infra.eb = modules.event_bus
	if _infra.eb then
		_infra.eb.offByOwner("SMILive")
	end
	syncDependencies(modules)

	-- Update ctx references after dependency refresh
	ctx.binder = binder
	ctx.tags_module = tags_module
	ctx.my_hooks_module = my_hooks_module
	ctx.samp_module = samp_module
	ctx.funcs = funcs
	ctx.mimgui_funcs = mimgui_funcs
	ctx.correct_module = correct_module
	ctx.trim = trim
	ctx.escape_imgui_text = escape_imgui_text
	ctx.NEWS_INPUT_FLAGS = NEWS_INPUT_FLAGS

	if _infra.cm then
		local data = _infra.cm.register("SMILive", {
			path = CONFIG_PATH_REL,
			defaults = {},
			loader = function(path, defaults)
				Config:load()
				return Config.data
			end,
		})
		Config.data = data
	end

	news_popup_mod.update_news_input_state()

	if resume_listener then
		broadcast_mod.start_sms_listener(true)
	end
end

function SMILive.OpenWindow()
	LiveWindow.show[0] = true
end

-- === Public API delegation ===

function SMILive.DrawHelperSection()
	live_ui_mod.DrawHelperSection()
end

function SMILive.DrawWinMessageSettings()
	live_ui_mod.DrawWinMessageSettings()
end

function SMILive.DrawMathQuiz(show_tables)
	math_quiz_mod.DrawMathQuiz(show_tables)
end

function SMILive.DrawCapitalsQuiz(show_tables)
	capitals_quiz_mod.DrawCapitalsQuiz(show_tables)
end

function SMILive.draw_math_settings_section()
	live_ui_mod.draw_math_settings_section()
end

function SMILive.draw_capitals_settings_section()
	live_ui_mod.draw_capitals_settings_section()
end

function SMILive.draw_live_settings_tab()
	live_ui_mod.draw_live_settings_tab()
end

-- Keep _build functions on SMILive table (used by news_popup_mod._draw_news_input_panel via ctx.SMILive)
function SMILive._build_math_round_answer_message(gender)
	return math_quiz_mod._build_math_round_answer_message(gender)
end

function SMILive._build_math_winner_message(gender)
	return math_quiz_mod._build_math_winner_message(gender)
end

function SMILive._build_capitals_round_answer_message(gender)
	return capitals_quiz_mod._build_capitals_round_answer_message(gender)
end

function SMILive._build_capitals_winner_message(gender)
	return capitals_quiz_mod._build_capitals_winner_message(gender)
end

-- Keep _active_live_tab as alias to State
SMILive._active_live_tab = nil
setmetatable(SMILive, {
	__index = function(t, k)
		if k == "_active_live_tab" then
			return State.active_live_tab
		end
	end,
	__newindex = function(t, k, v)
		if k == "_active_live_tab" then
			State.active_live_tab = v
		else
			rawset(t, k, v)
		end
	end,
})

-- Delegate news popup functions that are referenced as SMILive._xxx
SMILive._draw_news_popup_editor_button = function(...)
	return news_popup_mod._draw_news_popup_editor_button(...)
end
SMILive._make_news_popup_scope_key = function(...)
	return news_popup_mod._make_news_popup_scope_key(...)
end
SMILive._draw_news_send_cooldown_timer = function(...)
	return news_popup_mod._draw_news_send_cooldown_timer(...)
end
SMILive._draw_news_input_panel = function(...)
	return news_popup_mod._draw_news_input_panel(...)
end
SMILive._send_custom_news_message = function(...)
	return news_popup_mod._send_custom_news_message(...)
end
SMILive._append_to_news_input = function(...)
	return news_popup_mod._append_to_news_input(...)
end
SMILive._open_tags_help_window = function(...)
	return news_popup_mod._open_tags_help_window(...)
end
SMILive._get_news_send_cooldown_remaining_ms = function(...)
	return broadcast_mod._get_news_send_cooldown_remaining_ms(...)
end
SMILive._draw_live_broadcast_controls = function(...)
	return live_ui_mod._draw_live_broadcast_controls(...)
end
SMILive._draw_sms_listener_controls = function(...)
	return live_ui_mod._draw_sms_listener_controls(...)
end
SMILive._draw_live_text_inputs = function(...)
	return live_ui_mod._draw_live_text_inputs(...)
end
SMILive._draw_live_window_content = function(...)
	return live_ui_mod._draw_live_window_content(...)
end
SMILive._draw_live_window = function(...)
	return live_ui_mod._draw_live_window(...)
end

return SMILive
