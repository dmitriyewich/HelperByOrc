local SMILive = {}

local imgui = require("mimgui")
local new = imgui.new
local ffi = require("ffi")
local str = ffi.string
local ok_bit, bit = pcall(require, "bit")
local encoding = require "encoding"
encoding.default = "CP1251"
local u8 = encoding.UTF8

local binder
local tags_module
local start_sms_listener
local stop_sms_listener
local funcs
local update_status

local math_random = math.random
local os_clock = os.clock

local NEWS_PREFIX = "/news"

local function flags_or(...)
		local sum = 0
		for i = 1, select("#", ...) do
				local flag = select(i, ...)
				if flag then
						if ok_bit and bit and bit.bor then
								if sum == 0 then
										sum = flag
								else
										sum = bit.bor(sum, flag)
								end
						else
								sum = sum + flag
						end
				end
		end
		return sum
end

local NEWS_INPUT_MAX_LENGTH = 90
local NEWS_INPUT_PANEL_HEIGHT = 150
local NEWS_INPUT_BUFFER_SIZE = 512

local function run_async(label, fn)
                if not fn then
                                return
                end

                local function wrapped()
                                local ok, err = xpcall(fn, debug.traceback)
                                if not ok then
                                                update_status("Ошибка %s: %s", label or "в фоновом задании", err)
                                end
                end

                if lua_thread and lua_thread.create then
                                local ok, err = pcall(lua_thread.create, wrapped)
                                if ok then
                                                return
                                end
                                update_status("Не удалось создать поток %s: %s", label or "", err)
                end

                wrapped()
end

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

local NEWS_INPUT_FLAGS = flags_or(
		imgui.InputTextFlags and imgui.InputTextFlags.NoHorizontalScroll,
		imgui.InputTextFlags and imgui.InputTextFlags.AllowTabInput,
		imgui.InputTextFlags and imgui.InputTextFlags.CtrlEnterForNewLine
)

local MathQuiz = {
		target_scores = { 3, 5 },
		target_index = 1,
		active = false,
		round = 0,
		current_problem = nil,
		current_answer = nil,
		round_answer = nil,
		show_answer = new.bool(false),
		player_name_buf = new.char[48](),
		player_answer_buf = new.char[32](),
		custom_problem_buf = new.char[128](),
		custom_error = nil,
		status_text = "Нажмите \"Начать новую игру\"",
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

local LIVE_BUFFER_MIN = 4096
local LIVE_BUFFER_PAD = 1024
local LiveBroadcast = {
		intro = {text = ""},
		outro = {text = ""},
		reminder = {text = ""},
}

local CONFIG_PATH = getWorkingDirectory() .. "\\HelperByOrc\\SMILive.json"

local Config = { data = {} }

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
				changed =
						imgui.InputTextMultiline(
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

function Config:load()
		local data
		if funcs and funcs.loadTableFromJson then
				data = funcs.loadTableFromJson(CONFIG_PATH, {})
		else
				if doesFileExist(CONFIG_PATH) then
						local ok, loaded = pcall(function()
								local f = io.open(CONFIG_PATH, "rb")
								if not f then
										return
								end
								local content = f:read("*a")
								f:close()
								local ok_decode, parsed = pcall(decodeJson, content)
								if ok_decode and type(parsed) == "table" then
										return parsed
								end
						end)
						if ok and type(loaded) == "table" then
								data = loaded
						end
				end
				if type(data) ~= "table" then
						data = {}
				end
		end

		if type(data) ~= "table" then
				data = {}
		end

		local live_cfg = type(data.live_broadcast) == "table" and data.live_broadcast or {}
		live_cfg.intro = tostring(live_cfg.intro or "")
		live_cfg.outro = tostring(live_cfg.outro or "")
		live_cfg.reminder = tostring(live_cfg.reminder or "")
		data.live_broadcast = live_cfg

		LiveBroadcast.intro.text = live_cfg.intro
		LiveBroadcast.outro.text = live_cfg.outro
		LiveBroadcast.reminder.text = live_cfg.reminder
		ensure_live_buffer(LiveBroadcast.intro)
		ensure_live_buffer(LiveBroadcast.outro)
		ensure_live_buffer(LiveBroadcast.reminder)

		local quiz_cfg = type(data.math_quiz) == "table" and data.math_quiz or {}
		local saved_method = tonumber(quiz_cfg.chat_method)
		if saved_method then
				saved_method = math.floor(saved_method)
				if saved_method < 0 then
						saved_method = 0
				end
		else
				saved_method = MathQuiz.chat_method or 0
		end
		MathQuiz.chat_method = saved_method
		quiz_cfg.chat_method = saved_method

		local saved_interval = tonumber(quiz_cfg.chat_interval_ms)
		if saved_interval then
				saved_interval = math.floor(saved_interval + 0.5)
				if saved_interval < 0 then
						saved_interval = 0
				end
		else
				saved_interval = MathQuiz.chat_interval_ms or 0
		end
		MathQuiz.chat_interval_ms = saved_interval
		quiz_cfg.chat_interval_ms = saved_interval

		local saved_target = tonumber(quiz_cfg.target_index)
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
		quiz_cfg.target_index = saved_target

		data.math_quiz = quiz_cfg
		self.data = data
end

function Config:save()
		local data = self.data or {}

		local live_cfg = type(data.live_broadcast) == "table" and data.live_broadcast or {}
		live_cfg.intro = tostring((LiveBroadcast.intro and LiveBroadcast.intro.text) or "")
		live_cfg.outro = tostring((LiveBroadcast.outro and LiveBroadcast.outro.text) or "")
		live_cfg.reminder = tostring((LiveBroadcast.reminder and LiveBroadcast.reminder.text) or "")
		data.live_broadcast = live_cfg

		local quiz_cfg = type(data.math_quiz) == "table" and data.math_quiz or {}
		local method = math.floor(tonumber(MathQuiz.chat_method) or 0)
		if method < 0 then
				method = 0
		end
		quiz_cfg.chat_method = method

		local interval = math.floor(tonumber(MathQuiz.chat_interval_ms) or 0)
		if interval < 0 then
				interval = 0
		end
		quiz_cfg.chat_interval_ms = interval

		local target_index = math.floor(tonumber(MathQuiz.target_index) or 1)
		if target_index < 1 then
				target_index = 1
		end
		local max_target = #MathQuiz.target_scores
		if max_target > 0 and target_index > max_target then
				target_index = max_target
		end
		quiz_cfg.target_index = target_index

		data.math_quiz = quiz_cfg
		self.data = data

		if funcs and funcs.saveTableToJson then
				funcs.saveTableToJson(data, CONFIG_PATH)
		else
				local ok, encoded = pcall(encodeJson, data)
				local content = ok and encoded or "{}"
				local f = io.open(CONFIG_PATH, "w+b")
				if f then
						f:write(content)
						f:close()
				end
		end
end

Config:load()

local default_send_labels = {"В чат", "Клиенту", "Серверу", "В пустоту"}
local default_send_labels_ffi = imgui.new["const char*"][#default_send_labels](default_send_labels)

local function get_send_targets()
		if binder then
				if type(binder.getSendTargets) == "function" then
						local labels, labels_ffi = binder.getSendTargets()
						if type(labels) == "table" and labels_ffi then
								return labels, labels_ffi
						end
				elseif type(binder.send_labels) == "table" and binder.send_labels_ffi then
						return binder.send_labels, binder.send_labels_ffi
				end
		end
		return default_send_labels, default_send_labels_ffi
end

local function pluralize_points(value)
		local amount = math.floor(tonumber(value) or 0)
		local abs_amount = math.abs(amount)
		local n100 = abs_amount % 100
		local n10 = abs_amount % 10
		local suffix
		if n100 >= 11 and n100 <= 14 then
				suffix = "баллов"
		elseif n10 == 1 then
				suffix = "балл"
		elseif n10 >= 2 and n10 <= 4 then
				suffix = "балла"
		else
				suffix = "баллов"
		end
		return string.format("%d %s", amount, suffix)
end

local function format_score_progress(total, gained)
		gained = math.max(0, math.floor(tonumber(gained) or 0))
		total = math.max(0, math.floor(tonumber(total) or 0))
		if total <= gained then
				if gained > 0 then
						return string.format("Заработал %s!", pluralize_points(gained))
				end
				return "Заработал 0 баллов!"
		end

		local parts = {}
		if gained > 0 then
				parts[#parts + 1] = string.format("Заработал %s!", pluralize_points(gained))
		end
		parts[#parts + 1] = string.format("У него уже %s!", pluralize_points(total))
		return table.concat(parts, " ")
end

local function get_selected_method()
		local method = tonumber(MathQuiz.chat_method) or 0
		local send_labels = get_send_targets()
		local max_index = 0
		if type(send_labels) == "table" then
				max_index = math.max(0, #send_labels - 1)
		end
		if method < 0 then
				method = 0
		elseif method > max_index then
				method = max_index
		end
		return method
end

local function get_interval_ms()
		local value = tonumber(MathQuiz.chat_interval_ms) or 0
		if value < 0 then
				value = 0
		end
		return math.floor(value + 0.5)
end

local function format_status(fmt, ...)
                local ok, msg = pcall(string.format, fmt, ...)
                if ok then
                                return msg
		end
		return fmt
end

local function update_status(text, ...)
		MathQuiz.status_text = format_status(text, ...)
end

local send_sequence_running = false

local function send_sequence(messages, method, interval)
                if type(messages) ~= "table" or #messages == 0 then
                                return
                end

                if send_sequence_running then
                                update_status("Уже выполняется отправка сообщений. Дождитесь завершения текущей очереди.")
                                return
                end

                local safe_messages = {}
                for _, msg in ipairs(messages) do
                                if type(msg) == "string" and msg ~= "" then
                                                safe_messages[#safe_messages + 1] = msg
                                end
                end

                if #safe_messages == 0 then
                                return
                end

                local delay = math.max(0, tonumber(interval) or 0)
                delay = math.floor(delay + 0.5)
                local target = method or get_selected_method()
                local send_fn = binder and binder.doSend

                if type(send_fn) ~= "function" then
                                update_status("Отправка недоступна: функция binder.doSend не найдена.")
                                return
                end

                send_sequence_running = true

                run_async("отправки сообщений", function()
                                for idx, msg in ipairs(safe_messages) do
                                                local ok, err = pcall(send_fn, msg, target)
                                                if not ok then
                                                                update_status("Не удалось отправить сообщение #%d: %s", idx, err)
                                                                break
                                                end

                                                if idx < #safe_messages and delay > 0 and wait then
                                                                wait(delay)
                                                end
                                end

                                send_sequence_running = false
                end)
end

local function broadcast_sequence(messages)
		if get_selected_method() == 3 then
				return
		end
		send_sequence(messages, get_selected_method(), get_interval_ms())
end

local function trim(s)
		return (s or ""):gsub("^%s*(.-)%s*$", "%1")
end

local function flatten_news_text(text)
		text = tostring(text or "")
		text = text:gsub("\t", " ")
		text = text:gsub("\r\n", "\n")
		text = text:gsub("\r", "\n")
		text = text:gsub("\n+", " ")
		return trim(text)
end

local function strip_news_prefix(text)
		local cleaned = trim(text or "")
		local lower = cleaned:lower()
		if lower:sub(1, #NEWS_PREFIX) == NEWS_PREFIX then
				local next_char = cleaned:sub(#NEWS_PREFIX + 1, #NEWS_PREFIX + 1)
				if next_char == "" or next_char:match("%s") then
						return trim(cleaned:sub(#NEWS_PREFIX + 1)), true
				end
		end
		return cleaned, false
end

local function apply_tags_for_news(text)
		if not text or text == "" then
				return "", nil
		end
		if tags_module and type(tags_module.change_tags) == "function" then
				local ok, result = pcall(tags_module.change_tags, text)
				if ok and type(result) == "string" then
						return result, nil
				elseif not ok then
						return text, tostring(result)
				end
		end
		return text, nil
end

local function create_news_messages_from_text(text)
		local cleaned = trim(text)
		if cleaned == "" then
				return nil
		end

		local messages = {}
		for line in text:gmatch("[^\r\n]+") do
				local stripped = trim(line)
				if stripped ~= "" then
						if stripped:sub(1, #NEWS_PREFIX) == NEWS_PREFIX then
								messages[#messages + 1] = stripped
						else
								messages[#messages + 1] = string.format("%s %s", NEWS_PREFIX, stripped)
						end
				end
		end

		if #messages == 0 then
				return nil
		end

		return messages
end

local function update_news_input_state()
		local buf = NewsInput.buf
		local raw = buf and str(buf) or ""
		NewsInput.raw_text = raw

		local flattened = flatten_news_text(raw)
		NewsInput.flattened_full = flattened

		local body, had_prefix = strip_news_prefix(flattened)
		NewsInput.body_text = body
		NewsInput.had_prefix = had_prefix

		local processed, tag_error = apply_tags_for_news(body)
		local processed_222 = u8:decode(processed)
		NewsInput.processed_text = processed
		NewsInput.tag_error = tag_error
		NewsInput.processed_len = #processed_222
		NewsInput.over_limit = NewsInput.processed_len > NEWS_INPUT_MAX_LENGTH

		if processed ~= "" then
				NewsInput.preview = string.format("%s %s", NEWS_PREFIX, processed)
		else
				NewsInput.preview = ""
		end
end

local function send_live_sequence_from_section(section, section_name)
		local text
		if type(section) == "table" then
				text = section.text or section.buf_text or ""
		else
				text = str(section)
		end
		local messages = create_news_messages_from_text(text)
		if not messages then
				update_status("Добавьте текст для раздела \"%s\".", section_name)
				return false
		end

		if get_selected_method() == 3 then
				update_status(
						"Сообщения для раздела \"%s\" не отправлены: выбран режим \"В пустоту\".",
						section_name
				)
				return false
		end

		broadcast_sequence(messages)
		update_status("%s отправлено в эфир.", section_name)
		return true
end

local function normalize_player_name(name)
		local cleaned = trim(name)
		if cleaned == "" then
				return ""
		end
		return cleaned:gsub("_+", " ")
end

local function format_broadcast_name(name, player_id)
		local id = tonumber(player_id)
		if id and tags_module then
				return string.format("[rpnick(%d)]", id)
		end
		return normalize_player_name(name)
end

local function broadcast_problem(problem)
		if type(problem) ~= "string" or problem == "" then
				return
		end
		local messages = {
				string.format("%s %s", NEWS_PREFIX, problem)
		}
		broadcast_sequence(messages)
		start_sms_listener(true)
end

local function broadcast_correct_answer(player_name, answer, score, is_final, player_id)
		local normalized = normalize_player_name(player_name)
		if normalized == "" then
				normalized = trim(player_name)
		end
		if type(normalized) ~= "string" or normalized == "" then
				return
		end
		local answer_text = answer ~= nil and tostring(answer) or "-"
		local gained = 1
		local score_phrase = format_score_progress(score or 0, gained)
		local broadcast_name = format_broadcast_name(normalized, player_id)
		local messages = {
				string.format("%s Стоп!", NEWS_PREFIX),
				string.format("%s У нас есть правильный ответ!", NEWS_PREFIX),
				string.format("%s Правильный ответ был: %s", NEWS_PREFIX, answer_text),
				string.format("%s Верный ответ прислал..", NEWS_PREFIX),
				string.format("%s %s! %s", NEWS_PREFIX, broadcast_name, score_phrase)
		}
		if is_final then
				messages[#messages + 1] = string.format(
						"%s Викторина завершена! %s набирает %s и побеждает!",
						NEWS_PREFIX,
						broadcast_name,
						pluralize_points(score or 0)
				)
		end
		broadcast_sequence(messages)
end

local function parse_sms_message(text)
		if type(text) ~= "string" then
				return nil
		end
		local pattern = "%[SМS на студию%]%s*{FFFFFF}%s*Слушатель:%s*{%x%x%x%x%x%x}([%w_]+)%[(%d+)%]%s*{FFFFFF}:%s*(.+)"
		local name, id, message = text:match(pattern)

		if not name then
				return nil
		end
		return trim(name), tonumber(id), trim(message)
end

local function extract_numeric_answer(text)
		if type(text) ~= "string" then
				return nil
		end
		local candidate = text:match("[-+]?%d+")
		if candidate then
				return tonumber(candidate)
		end
		return nil
end

local function format_seconds(value)
		if not value then
				return "-"
		end
		return string.format("%.2f с", value)
end

local function pick_divisor(n)
		local divisors = {}
		for i = 2, n - 1 do
				if n % i == 0 then
						divisors[#divisors + 1] = i
				end
		end
		if #divisors == 0 then
				return 1
		end
		return divisors[math_random(1, #divisors)]
end

local function generate_two_operand_problem()
		local ops = {"+", "-", "*", "/"}
		local op = ops[math_random(1, #ops)]
		local a, b, answer
		if op == "+" then
				a = math_random(2, 30)
				b = math_random(2, 30)
				answer = a + b
		elseif op == "-" then
				b = math_random(2, 25)
				a = math_random(b + 1, 35)
				answer = a - b
		elseif op == "*" then
				a = math_random(2, 12)
				b = math_random(2, 12)
				answer = a * b
		else
				b = math_random(2, 12)
				local quotient = math_random(2, 12)
				a = b * quotient
				answer = quotient
		end
		local text = string.format("%d %s %d", a, op, b)
		return text, answer
end

local function generate_multi_step_problem()
		local builders = {
				function()
						local a = math_random(2, 20)
						local b = math_random(2, 10)
						local c = math_random(2, 10)
						local answer = a + b * c
						return string.format("%d + %d * %d", a, b, c), answer
				end,
				function()
						local a = math_random(2, 10)
						local b = math_random(2, 10)
						local c = math_random(2, 8)
						local answer = (a + b) * c
						return string.format("(%d + %d) * %d", a, b, c), answer
				end,
				function()
						local a = math_random(2, 9)
						local b = math_random(2, 9)
						local product = a * b
						local c = pick_divisor(product)
						if c == 1 then
								c = 2
						end
						local answer = product / c
						return string.format("(%d * %d) / %d", a, b, c), answer
				end,
				function()
						local a = math_random(2, 10)
						local b = math_random(2, 10)
						local product = a * b
						local c = math_random(2, product - 1)
						local answer = product - c
						return string.format("(%d * %d) - %d", a, b, c), answer
				end,
				function()
						local c = math_random(2, 9)
						local quotient = math_random(2, 9)
						local total = c * quotient
						local a = math_random(2, total - 2)
						local b = total - a
						if b <= 1 then
								b = 2
								a = total - b
						end
						local answer = quotient
						return string.format("(%d + %d) / %d", a, b, c), answer
				end,
		}
		local builder = builders[math_random(1, #builders)]
		local text, answer = builder()
		if answer <= 0 or answer ~= math.floor(answer) then
				return generate_multi_step_problem()
		end
		return text, answer
end

local function generate_math_problem()
		if math_random() < 0.5 then
				return generate_two_operand_problem()
		end
		return generate_multi_step_problem()
end

local sms_listener_active = false
local my_hooks_module = nil

local function can_use_sms_listener()
		return my_hooks_module
				and type(my_hooks_module.addServerMessageListener) == "function"
				and type(my_hooks_module.removeServerMessageListener) == "function"
end

local function reset_buffers()
		imgui.StrCopy(MathQuiz.player_name_buf, "")
		imgui.StrCopy(MathQuiz.player_answer_buf, "")
end

local function reset_scoreboard()
		MathQuiz.players = {}
		MathQuiz.round = 0
		MathQuiz.winner = nil
		MathQuiz.current_problem = nil
		MathQuiz.current_answer = nil
		MathQuiz.round_answer = nil
		MathQuiz.show_answer[0] = false
		MathQuiz.answer_start_time = nil
		MathQuiz.accepting_answers = false
		MathQuiz.current_responses = {}
		MathQuiz.first_correct = nil
		MathQuiz.latest_round_stats = nil
		MathQuiz.awaiting_next_round = false
		MathQuiz.custom_error = nil
		update_status("Игра сброшена. Сгенерируйте пример, чтобы начать раунд.")
		reset_buffers()
end

local function start_new_game()
		MathQuiz.active = true
		reset_scoreboard()
		update_status("Игра началась. Цель - %d очка(ов).", MathQuiz.target_scores[MathQuiz.target_index])
end

local function end_game(winner)
		MathQuiz.active = false
		MathQuiz.winner = winner
		update_status("%s достигает %d очков и побеждает!", winner, MathQuiz.target_scores[MathQuiz.target_index])
		MathQuiz.current_problem = nil
		MathQuiz.current_answer = nil
		MathQuiz.round_answer = nil
		MathQuiz.answer_start_time = nil
		MathQuiz.accepting_answers = false
		MathQuiz.awaiting_next_round = false
		MathQuiz.custom_error = nil
end

local function ensure_player(name)
		local normalized = normalize_player_name(name)
		if normalized == "" then
				return nil, normalized
		end
		MathQuiz.players[normalized] = MathQuiz.players[normalized] or { score = 0, last_answer = nil, last_correct = false }
		return MathQuiz.players[normalized], normalized
end

local function start_round(problem, answer)
		MathQuiz.current_problem = problem
		MathQuiz.current_answer = answer
		MathQuiz.round_answer = answer
		MathQuiz.show_answer[0] = false
		MathQuiz.answer_start_time = os_clock()
		MathQuiz.accepting_answers = true
		MathQuiz.current_responses = {}
		MathQuiz.first_correct = nil
		MathQuiz.latest_round_stats = nil
		MathQuiz.awaiting_next_round = false
		MathQuiz.custom_error = nil
		update_status("Раунд %d: огласите пример и ждите ответы.", MathQuiz.round + 1)
		reset_buffers()
end

local function begin_round()
		local problem, answer = generate_math_problem()
		start_round(problem, answer)
end

local function evaluate_custom_problem(problem_text)
		local trimmed = trim(problem_text or "")
		if trimmed == "" then
				return nil, "Введите текст примера."
		end

		if not trimmed:match("^[%d%+%-%*/%(%)%s]+$") then
				local invalid = trimmed:match("[^%d%+%-%*/%(%)%s]") or "?"
				return nil, string.format("Недопустимый символ: %s", invalid)
		end

		local chunk, err = load("return " .. trimmed, "MathQuizCustom", "t", {})
		if not chunk then
				return nil, string.format("Не удалось разобрать пример: %s", err or "ошибка")
		end

		local ok, result = pcall(chunk)
		if not ok then
				return nil, string.format("Ошибка при вычислении: %s", result)
		end

		if type(result) ~= "number" or result ~= result or result == math.huge or result == -math.huge then
				return nil, "Результат должен быть числом."
		end

		if result <= 0 or math.floor(result) ~= result then
				return nil, "Ответ должен быть положительным целым числом."
		end

		return trimmed, result
end

local function begin_custom_round(problem_text)
		local cleaned_problem = trim(problem_text)
		if cleaned_problem == "" then
				return false, "Введите текст примера."
		end

		local parsed_problem, numeric_answer = evaluate_custom_problem(cleaned_problem)
		if not parsed_problem then
				return false, numeric_answer
		end

		start_round(parsed_problem, numeric_answer)
		return true
end

local function handle_correct_answer(player_name)
		local entry, normalized = ensure_player(player_name)
		if not entry then
				return
		end
		player_name = normalized
		entry.score = entry.score + 1
		entry.last_correct = true
		entry.last_answer = MathQuiz.current_answer

		MathQuiz.round = MathQuiz.round + 1
		MathQuiz.round_answer = MathQuiz.round_answer or MathQuiz.current_answer
		MathQuiz.current_problem = nil
		MathQuiz.current_answer = nil
		MathQuiz.show_answer[0] = false
		MathQuiz.accepting_answers = false

		local target = MathQuiz.target_scores[MathQuiz.target_index]
		if entry.score >= target then
				MathQuiz.awaiting_next_round = false
				end_game(player_name)
		else
				MathQuiz.awaiting_next_round = true
				update_status("%s получает балл (%d/%d). Запустите следующий пример.", player_name, entry.score, target)
		end
end

local function handle_wrong_answer(player_name, provided)
		local entry, normalized = ensure_player(player_name)
		if not entry then
				return
		end
		player_name = normalized
		entry.last_correct = false
		entry.last_answer = provided
		update_status("Ответ %s неверный (%s).", player_name, tostring(provided))
end

local function iterate_players_sorted()
		local list = {}
		for name, data in pairs(MathQuiz.players) do
				table.insert(list, { name = name, score = data.score, last_correct = data.last_correct, last_answer = data.last_answer })
		end
		table.sort(list, function(a, b)
				if a.score == b.score then
						return a.name:lower() < b.name:lower()
				end
				return a.score > b.score
		end)
		return list
end

local function has_players()
		return next(MathQuiz.players) ~= nil
end

local function update_player_last_answer(name, provided, is_correct)
		local entry, normalized = ensure_player(name)
		if not entry then
				return nil, normalized
		end
		entry.last_correct = is_correct and true or false
		entry.last_answer = provided
		return entry, normalized
end

local function compute_lead_time(first_response)
		local best
		for _, resp in ipairs(MathQuiz.current_responses) do
				if resp ~= first_response and resp.response_time and first_response.response_time and resp.response_time > first_response.response_time then
						local diff = resp.response_time - first_response.response_time
						if diff > 0 and (not best or diff < best) then
								best = diff
						end
				end
		end
		return best
end

local function record_response_from_sms(player_name, player_id, message)
		if not MathQuiz.active then
				return
		end

		if not MathQuiz.answer_start_time then
				return
		end

		if not (MathQuiz.accepting_answers or MathQuiz.awaiting_next_round) then
				return
		end

		local correct_value_reference = MathQuiz.current_answer or MathQuiz.round_answer
		if not correct_value_reference and MathQuiz.latest_round_stats then
				correct_value_reference = MathQuiz.latest_round_stats.correct_answer
		end

		if not correct_value_reference then
				return
		end

		local normalized_name = normalize_player_name(player_name)
		if normalized_name ~= "" then
				player_name = normalized_name
		else
				player_name = trim(player_name)
		end

		local response_time = os_clock() - MathQuiz.answer_start_time
		if response_time < 0 then
				response_time = 0
		end

		local numeric_answer = extract_numeric_answer(message)
		local is_correct = numeric_answer ~= nil and correct_value_reference == numeric_answer
		local stored_correct_answer = correct_value_reference
		local entry = {
				name = player_name,
				player_id = player_id,
				text = message,
				numeric = numeric_answer,
				response_time = response_time,
				correct = is_correct,
				outcome = "attempt",
		}
		table.insert(MathQuiz.current_responses, entry)

		if is_correct and not MathQuiz.first_correct then
				entry.outcome = "first"
				MathQuiz.first_correct = entry
				local correct_value = correct_value_reference
				handle_correct_answer(player_name)

				local player_entry = ensure_player(player_name)
				local lead = compute_lead_time(entry)
				MathQuiz.latest_round_stats = {
						winner = player_name,
						player_id = player_id,
						response_time = entry.response_time,
						lead = lead,
						total_responses = #MathQuiz.current_responses,
						correct_answer = correct_value,
						score = player_entry and player_entry.score or 0,
						points_awarded = 1,
						game_finished = not MathQuiz.active,
				}

				local target = MathQuiz.target_scores[MathQuiz.target_index]
				if MathQuiz.active then
						if lead then
								update_status("%s отвечает верно за %.2f с и опережает соперников на %.2f с. Счёт: %d/%d.", player_name, entry.response_time, lead, player_entry.score, target)
						else
								update_status("%s отвечает верно за %.2f с. Других ответов пока нет. Счёт: %d/%d.", player_name, entry.response_time, player_entry.score, target)
						end
				else
						if lead then
								update_status("%s завершает игру, ответив верно за %.2f с и опередив соперников на %.2f с. Итог: %d очков.", player_name, entry.response_time, lead, player_entry.score)
						else
								update_status("%s завершает игру, ответив верно за %.2f с. Итог: %d очков.", player_name, entry.response_time, player_entry.score)
						end
				end

				return
		end

		if is_correct then
				entry.outcome = "late"
				update_player_last_answer(player_name, stored_correct_answer, false)
				update_status("%s прислал верный ответ через %.2f с, но уже после завершения раунда.", player_name, entry.response_time)
		else
				entry.outcome = "wrong"
				local provided = message ~= "" and message or "-"
				update_player_last_answer(player_name, provided, false)
				update_status("%s отвечает через %.2f с: %s (неверно).", player_name, entry.response_time, provided)
		end
end

local function handle_server_sms(color, text)
		local name, player_id, message = parse_sms_message(text)
		if not name then
				return
		end
		record_response_from_sms(name, player_id, message)
end

start_sms_listener = function(silent)
		if sms_listener_active then
				return true
		end
		if not can_use_sms_listener() then
				if not silent then
						update_status("Модуль приёма SMS недоступен.")
				end
				return false
		end
		my_hooks_module.addServerMessageListener(handle_server_sms)
		sms_listener_active = true
		if not silent then
				update_status("Приём SMS-сообщений активирован. Ждите ответы слушателей.")
		end
		return true
end

stop_sms_listener = function(silent)
                if not sms_listener_active then
                                return true
                end
		if not can_use_sms_listener() then
				sms_listener_active = false
				if not silent then
						update_status("Приём SMS-сообщений недоступен.")
				end
				return false
		end
		my_hooks_module.removeServerMessageListener(handle_server_sms)
		sms_listener_active = false
		if not silent then
				update_status("Приём SMS-сообщений остановлен.")
                end
                return true
end


local SCOREBOARD_HEIGHT = 170
local RESPONSES_HEIGHT = 150

local function draw_scoreboard_table(height)
        if imgui.BeginChild("math_quiz_scoreboard", imgui.ImVec2(0, height), true, imgui.WindowFlags.HorizontalScrollbar) then
        imgui.Columns(3, "math_quiz_scoreboard_cols", true)
        imgui.Text("Игрок")
        imgui.NextColumn()
        imgui.Text("Очки")
        imgui.NextColumn()
        imgui.Text("Последний ответ")
        imgui.NextColumn()
        imgui.Separator()

        local has_rows = false
        for _, row in ipairs(iterate_players_sorted()) do
                has_rows = true
                imgui.Text(row.name)
                if MathQuiz.winner and MathQuiz.winner == row.name then
                        imgui.SameLine()
                        imgui.TextColored(imgui.ImVec4(0.9, 0.8, 0.2, 1), "★")
                end
                imgui.NextColumn()
                imgui.Text(tostring(row.score))
                imgui.NextColumn()
                if row.last_answer ~= nil then
                        local color = row.last_correct and imgui.ImVec4(0.4, 1.0, 0.4, 1) or imgui.ImVec4(1.0, 0.4, 0.4, 1)
                        imgui.TextColored(color, tostring(row.last_answer))
                else
                        imgui.Text("-")
                end
                imgui.NextColumn()
        end
        imgui.Columns(1)

        if not has_rows then
                imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Данных пока нет.")
        end
        end
        imgui.EndChild()
end

local function draw_responses_table(height)
        if imgui.BeginChild("math_quiz_responses", imgui.ImVec2(0, height), true, imgui.WindowFlags.HorizontalScrollbar) then
        imgui.Columns(4, "math_quiz_responses_cols", true)
        imgui.Text("Игрок")
        imgui.NextColumn()
        imgui.Text("ID")
        imgui.NextColumn()
        imgui.Text("Ответ")
        imgui.NextColumn()
        imgui.Text("Время")
        imgui.NextColumn()
        imgui.Separator()

        local has_rows = false
        for _, resp in ipairs(MathQuiz.current_responses) do
                has_rows = true
                imgui.Text(resp.name)
                if resp.outcome == "first" then
                        imgui.SameLine()
                        imgui.TextColored(imgui.ImVec4(0.9, 0.8, 0.2, 1), "★")
                end
                imgui.NextColumn()
                imgui.Text(resp.player_id and tostring(resp.player_id) or "-")
                imgui.NextColumn()
                local display_answer = resp.text ~= "" and resp.text or "-"
                local color
                if resp.outcome == "first" then
                        color = imgui.ImVec4(0.4, 1.0, 0.4, 1)
                elseif resp.outcome == "late" then
                        color = imgui.ImVec4(0.6, 0.8, 0.6, 1)
                elseif resp.correct then
                        color = imgui.ImVec4(0.6, 0.8, 0.6, 1)
                else
                        color = imgui.ImVec4(1.0, 0.4, 0.4, 1)
                end
                imgui.TextColored(color, display_answer)
                imgui.NextColumn()
                if resp.response_time then
                        imgui.Text(format_seconds(resp.response_time))
                else
                        imgui.Text("-")
                end
                imgui.NextColumn()
        end
        imgui.Columns(1)

        if not has_rows then
                imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Ответов пока нет.")
        end
        end
        imgui.EndChild()
end

local function draw_latest_round_stats()
local stats = MathQuiz.latest_round_stats
if not stats or not stats.winner then
return
end

local winner = stats.winner or "-"
local player_id = stats.player_id and tostring(stats.player_id) or "-"
local response_time = stats.response_time and string.format("%.2fс", stats.response_time) or "-"
local total_responses = stats.total_responses or 0
local summary = string.format("Первый верный: %s[%s] - %s | Всего ответов: %d", winner, player_id, response_time, total_responses)
imgui.Text(summary)
end

local function submit_answer_from_fields()
local name = trim(ffi.string(MathQuiz.player_name_buf))
local answer_text = trim(ffi.string(MathQuiz.player_answer_buf))

if name == "" then
update_status("Введите ник игрока.")
return
end

if answer_text == "" then
update_status("Введите ответ.")
return
end

if not MathQuiz.active then
update_status("Игра не активна. Сначала начните раунд.")
return
end

if not MathQuiz.answer_start_time then
update_status("Нет активного примера для проверки.")
return
end

record_response_from_sms(name, nil, answer_text)
reset_buffers()
end

local function draw_math_quiz_tables_section()
imgui.Text("Таблица очков")
draw_scoreboard_table(SCOREBOARD_HEIGHT)

imgui.Spacing()
imgui.Text("Ответы текущего раунда")
draw_responses_table(RESPONSES_HEIGHT)

imgui.Spacing()
draw_latest_round_stats()

imgui.Spacing()
imgui.PushItemWidth(180)
imgui.InputText("Ник игрока", MathQuiz.player_name_buf, 48)
imgui.InputText("Ответ", MathQuiz.player_answer_buf, 32)
imgui.PopItemWidth()

if imgui.Button("Проверить ответ") then
submit_answer_from_fields()
end

imgui.SameLine()
if MathQuiz.active then
if imgui.Button("Сбросить игру") then
start_new_game()
end
else
if imgui.Button("Сбросить таблицу") then
reset_scoreboard()
end
end
end


function SMILive.DrawMathQuiz(show_tables)
	if not MathQuiz.active then
		for idx, target in ipairs(MathQuiz.target_scores) do
			if idx > 1 then
				imgui.SameLine()
			end
			imgui.PushIDInt(idx)
			if imgui.RadioButtonBool(string.format("%d очка", target), MathQuiz.target_index == idx) then
				MathQuiz.target_index = idx
				Config:save()
			end
			imgui.PopID()
		end

		imgui.SameLine()
		if imgui.Button("Начать игру") then
			start_new_game()
		end
	else
		if imgui.Button("Сгенерировать пример") then
			if MathQuiz.awaiting_next_round then
				update_status("Следующий раунд начнётся после объявления победителя. Нажмите \"Следующий пример\".")
			else
				begin_round()
			end
		end

		if MathQuiz.awaiting_next_round then
			imgui.SameLine()
			if imgui.Button("Следующий пример") then
				begin_round()
			end
		end

		imgui.SameLine()
		if imgui.Button("Завершить игру") then
			MathQuiz.active = false
			update_status("Игра завершена вручную.")
			MathQuiz.current_problem = nil
			MathQuiz.current_answer = nil
			MathQuiz.round_answer = nil
			MathQuiz.answer_start_time = nil
			MathQuiz.accepting_answers = false
			MathQuiz.awaiting_next_round = false
			MathQuiz.custom_error = nil
		end

		if MathQuiz.current_problem then
			imgui.Text(string.format("Текущий пример: %s", MathQuiz.current_problem))
			imgui.SameLine()
			imgui.Checkbox("Показать ответ", MathQuiz.show_answer)
			if MathQuiz.show_answer[0] and MathQuiz.current_answer then
				imgui.SameLine()
				imgui.TextColored(imgui.ImVec4(0.4, 1.0, 0.4, 1), string.format("= %s", tostring(MathQuiz.current_answer)))
			end
		end

		imgui.Text("или задайте свой пример:")
		imgui.PushItemWidth(250)
		imgui.InputText("##MathQuizCustom", MathQuiz.custom_problem_buf, 128)
		imgui.PopItemWidth()
		imgui.SameLine()
		if imgui.Button("Использовать пример") then
			if MathQuiz.awaiting_next_round then
				local message = "Следующий раунд начнётся после объявления победителя. Нажмите \"Следующий пример\"."
				update_status(message)
				MathQuiz.custom_error = message
			else
				local ok, err = begin_custom_round(str(MathQuiz.custom_problem_buf))
				if ok then
					MathQuiz.custom_error = nil
					imgui.StrCopy(MathQuiz.custom_problem_buf, "")
				else
					MathQuiz.custom_error = err or "Не удалось установить пример."
				end
			end
		end
		if MathQuiz.custom_error then
			imgui.TextColored(imgui.ImVec4(1.0, 0.4, 0.4, 1), MathQuiz.custom_error)
		end
	end

	if MathQuiz.current_problem then
		if imgui.Button("Отправить пример в чат") then
			broadcast_problem(MathQuiz.current_problem)
		end
		imgui.SameLine()
	end

	if MathQuiz.latest_round_stats and MathQuiz.latest_round_stats.winner then
		if imgui.Button("Объявить ответ в чат") then
			local stats = MathQuiz.latest_round_stats
			broadcast_correct_answer(stats.winner, stats.correct_answer, stats.score, stats.game_finished, stats.player_id)
		end
	end

if show_tables ~= false then
draw_math_quiz_tables_section()
end
end

local LiveWindow = {
		show = new.bool(false),
}

local function draw_live_broadcast_controls()
                if imgui.Button("Начать эфир") then
                                send_live_sequence_from_section(LiveBroadcast.intro, "Вступление")
                end

                imgui.SameLine()
                if imgui.Button("Закончить эфир") then
                                send_live_sequence_from_section(LiveBroadcast.outro, "Завершение эфира")
                end

                imgui.SameLine()
                if imgui.Button("Напоминание") then
                                send_live_sequence_from_section(LiveBroadcast.reminder, "Напоминание")
                end

                imgui.SetNextItemOpen(false, imgui.Cond.Once)
                if imgui.CollapsingHeader("Настройки сообщений##live_message_settings") then
                                imgui.PushItemWidth(200)
                                local method_buf = ffi.new("int[1]", MathQuiz.chat_method)
                                local send_labels, send_labels_ffi = get_send_targets()
                                local send_count = 0
                                if type(send_labels) == "table" then
                                                send_count = #send_labels
                                end
                                if send_labels_ffi and send_count > 0 then
                                                if imgui.Combo("Метод отправки", method_buf, send_labels_ffi, send_count) then
                                                                local max_index = math.max(0, send_count - 1)
                                                                local new_method = method_buf[0]
                                                                if new_method < 0 then
                                                                                new_method = 0
                                                                elseif new_method > max_index then
                                                                                new_method = max_index
                                                                end
                                                                MathQuiz.chat_method = new_method
                                                                Config:save()
                                                end
                                else
                                                imgui.TextDisabled("Методы отправки недоступны")
                                end
                                imgui.PopItemWidth()

                                imgui.PushItemWidth(200)
                                local interval_buf = ffi.new("int[1]", MathQuiz.chat_interval_ms)
                                if imgui.InputInt("Интервал между сообщениями (мс)", interval_buf) then
                                                MathQuiz.chat_interval_ms = math.max(0, interval_buf[0])
                                                Config:save()
                                end
                                imgui.PopItemWidth()

                                local intro_changed = update_live_buffer_from_imgui(LiveBroadcast.intro, "Вступление##live_intro_text", 80)
                                if intro_changed then
                                                Config:save()
                                end

                                local outro_changed = update_live_buffer_from_imgui(LiveBroadcast.outro, "Завершение##live_outro_text", 80)
                                if outro_changed then
                                                Config:save()
                                end

                                local reminder_changed = update_live_buffer_from_imgui(LiveBroadcast.reminder, "Напоминание##live_reminder_text", 80)
                                if reminder_changed then
                                                Config:save()
                                end
                end
end

local function draw_sms_listener_controls()
                local controls_available = can_use_sms_listener()
                local button_label
                local action

                if sms_listener_active then
                                button_label = "Закончить прием сообщений"
                                action = function()
                                                stop_sms_listener(false)
                                end
                else
                                button_label = "Начать прием сообщений"
                                action = function()
                                                start_sms_listener(false)
                                end
                end

		local button_disabled = (not controls_available and not sms_listener_active)
		if button_disabled then
			local alpha = imgui.GetStyle().Alpha
			imgui.PushStyleVar(imgui.StyleVar_Alpha, alpha * 0.5)
		end

		if imgui.Button(button_label) and not button_disabled then
			if controls_available or sms_listener_active then
				action()
			else
				update_status("Модуль my_hooks не поддерживает приём SMS-сообщений.")
			end
		end

		if button_disabled then
			imgui.PopStyleVar()
		end

                imgui.SameLine()
                local status_color = sms_listener_active and imgui.ImVec4(0.4, 1.0, 0.4, 1) or imgui.ImVec4(1.0, 0.6, 0.4, 1)
                local status_label = sms_listener_active and "активен" or "не активен"
                imgui.TextColored(status_color, status_label)
end

local function send_custom_news_message()
		update_news_input_state()

		local body = NewsInput.body_text or ""
		if body == "" then
				update_status("Введите текст объявления.")
				return
		end

		if NewsInput.over_limit then
				update_status("Объявление не отправлено: превышен лимит %d символов.", NEWS_INPUT_MAX_LENGTH)
				return
		end

		local method = get_selected_method()
		if method == 3 then
				update_status("Объявление не отправлено: выбран режим \"В пустоту\".")
				return
		end

		local send_fn = binder and binder.doSend
		if type(send_fn) ~= "function" then
				update_status("Отправка недоступна: функция binder.doSend не найдена.")
				return
		end

		local message = string.format("%s %s", NEWS_PREFIX, body)
		send_fn(message, method)
		update_status("Объявление отправлено.")
end

local function draw_news_input_panel()
		imgui.Text("Отправить /news")
		imgui.Spacing()

		local avail = imgui.GetContentRegionAvail()
		local input_height = math.max(40, avail.y - 70)

		imgui.InputTextMultiline(
				"##live_news_input",
				NewsInput.buf,
				NewsInput.buf_size,
				imgui.ImVec2(0, input_height),
				NEWS_INPUT_FLAGS
		)

		update_news_input_state()

		if NewsInput.had_prefix then
				imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Префикс /news добавляется автоматически.")
		end

		local len_color = NewsInput.over_limit and imgui.ImVec4(1.0, 0.4, 0.4, 1) or imgui.ImVec4(0.7, 0.9, 1.0, 1)
		imgui.TextColored(
				len_color,
				string.format("Длина после тегов: %d / %d", NewsInput.processed_len, NEWS_INPUT_MAX_LENGTH)
		)

		if NewsInput.over_limit then
				imgui.TextColored(imgui.ImVec4(1.0, 0.4, 0.4, 1), "Сократите текст объявления.")
		end

		if NewsInput.tag_error then
				imgui.TextColored(imgui.ImVec4(1.0, 0.6, 0.3, 1), "Ошибка обработки тегов, используется исходный текст.")
		end

		if NewsInput.preview ~= "" then
				imgui.TextWrapped("Предпросмотр: " .. NewsInput.preview)
		else
				imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Введите текст объявления.")
		end

		imgui.Spacing()
		if imgui.Button("Отправить /news") then
				send_custom_news_message()
		end
end

local function draw_live_window_content()
                if imgui.BeginTabBar("smilive_tabs") then
                                if imgui.BeginTabItem("Эфир") then
                                                draw_live_broadcast_controls()
                                                imgui.Dummy(imgui.ImVec2(0, 4))
                                                draw_sms_listener_controls()
                                                imgui.EndTabItem()
                                end

if imgui.BeginTabItem("Викторина") then
SMILive.DrawMathQuiz(false)
imgui.EndTabItem()
end

if imgui.BeginTabItem("Таблица") then
draw_math_quiz_tables_section()
imgui.EndTabItem()
end

                                imgui.EndTabBar()
                end
end

local function draw_live_window()
		local bottom_height = NEWS_INPUT_PANEL_HEIGHT

		if imgui.BeginChild("smilive_main", imgui.ImVec2(0, -bottom_height), true) then
				draw_live_window_content()
		end
		imgui.EndChild()

		imgui.Spacing()

		if imgui.BeginChild("smilive_news_input", imgui.ImVec2(0, bottom_height), true) then
				draw_news_input_panel()
		end
		imgui.EndChild()
end

function SMILive.OpenWindow()
		LiveWindow.show[0] = true
end

function SMILive.DrawHelperSection()
		imgui.TextWrapped("Эфир-викторина доступна в отдельном окне. Нажмите кнопку, чтобы открыть помощника эфира.")
		if imgui.Button("Открыть эфир-викторину") then
				SMILive.OpenWindow()
		end
		imgui.Spacing()
		imgui.Separator()
		imgui.TextWrapped(MathQuiz.status_text)
		if has_players() or MathQuiz.winner then
				imgui.Spacing()
				imgui.Text("Краткая сводка игроков")
				local preview = {}
				for _, row in ipairs(iterate_players_sorted()) do
						table.insert(preview, format_status("%s - %d", row.name, row.score))
						if #preview == 3 then
								break
						end
				end
				if #preview == 0 then
						imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Данных пока нет.")
				else
						for _, line in ipairs(preview) do
								imgui.Text(line)
						end
						local total = 0
						for _ in pairs(MathQuiz.players) do
								total = total + 1
						end
						if total > #preview then
								imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), format_status("…и ещё %d участник(ов)", total - #preview))
						end
				end
		end
end

imgui.OnFrame(function()
		return LiveWindow.show[0]
end, function()
		imgui.SetNextWindowSize(imgui.ImVec2(520, 480), imgui.Cond.FirstUseEver)
		local opened = imgui.Begin("SMI Live - эфир-викторина", LiveWindow.show, imgui.WindowFlags.NoCollapse)
		if opened then
				draw_live_window()
		end
		imgui.End()
end)

function SMILive.attachModules(modules)
		local resume_listener = sms_listener_active
		if sms_listener_active and can_use_sms_listener() then
				stop_sms_listener(true)
		else
				sms_listener_active = false
		end

		binder = modules and modules.binder or nil
		tags_module = modules and modules.tags or nil
		my_hooks_module = modules and modules.my_hooks or nil
		funcs = modules and modules.funcs or funcs

		update_news_input_state()

		if resume_listener then
				start_sms_listener(true)
		end
end

return SMILive
