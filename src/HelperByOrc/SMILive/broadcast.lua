local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local str = ffi.string
local ctx

local default_send_labels = {
	"Локально",
	"Клиенту SA-MP",
	"Серверу",
	"Без отправки",
	"Написать в чат и закрыть его",
	"Написать в чат",
	"В активное диалоговое окно",
	"Скопировать в буфер обмена",
	"В консоль SF и биндера",
	"В уведомления",
}
local default_send_labels_ffi = imgui.new["const char*"][#default_send_labels](default_send_labels)

function M.init(c)
	ctx = c
end

-- === Send targets ===

function M.get_send_targets()
	local binder = ctx.binder
	if binder then
		if type(binder.getSendTargets) == "function" then
			local labels, labels_ffi = binder.getSendTargets()
			if type(labels) == "table" and labels_ffi then
				return labels, labels_ffi
			end
		end
	end
	return default_send_labels, default_send_labels_ffi
end

function M.get_selected_method()
	local method = tonumber(ctx.MathQuiz.chat_method) or 0
	local send_labels = M.get_send_targets()
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

-- === Status ===

local function format_status(fmt, ...)
	local ok, msg = pcall(string.format, fmt, ...)
	if ok then
		return msg
	end
	return fmt
end

function M.update_status(text, ...)
	local msg = format_status(text, ...)
	ctx.MathQuiz.status_text = msg
	if ctx.CapitalsQuiz then
		ctx.CapitalsQuiz.status_text = msg
	end
end

-- === Send infrastructure ===

local function get_send_fn()
	local binder = ctx.binder
	local send_fn = binder and binder.doSend
	if type(send_fn) ~= "function" then
		M.update_status("Отправка недоступна: функция binder.doSend не найдена.")
		return nil
	end
	return send_fn
end

function M.cancel_send_queue()
	local State = ctx.State
	if not State.send_sequence_running then
		M.update_status("Активной отправки нет.")
		return false
	end
	State.send_sequence_cancel = true
	M.update_status("Отправка сообщений отменена.")
	return true
end

-- === News send timing ===

function M._get_now_ms_monotonic()
	local SMILive = ctx.SMILive
	local timing = SMILive._news_send_timing or {}
	local provider = timing.time_provider or 0
	if provider ~= 2 then
		local ok, tick = pcall(function()
			return ffi.C.GetTickCount64()
		end)
		if ok and tick ~= nil then
			timing.time_provider = 1
			SMILive._news_send_timing = timing
			return math.floor(tonumber(tick) or 0)
		end
		timing.time_provider = 2
		SMILive._news_send_timing = timing
	end
	return math.floor((os.clock() * 1000) + 0.5)
end

function M._is_news_command_message(message)
	local trim = ctx.trim
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local text = trim and trim(message) or tostring(message or ""):gsub("^%s*(.-)%s*$", "%1")
	if text == "" then
		return false
	end
	local lower = text:lower()
	if lower:sub(1, #NEWS_PREFIX) ~= NEWS_PREFIX then
		return false
	end
	local next_char = text:sub(#NEWS_PREFIX + 1, #NEWS_PREFIX + 1)
	return next_char == "" or next_char:match("%s") ~= nil
end

function M._get_news_send_cooldown_remaining_ms()
	local SMILive = ctx.SMILive
	local timing = SMILive._news_send_timing or {}
	local last_send_ms = tonumber(timing.last_send_ms) or 0
	if last_send_ms <= 0 then
		return 0
	end
	local now_ms = M._get_now_ms_monotonic()
	local elapsed = now_ms - last_send_ms
	if elapsed < 0 then
		timing.last_send_ms = now_ms
		SMILive._news_send_timing = timing
		return tonumber(SMILive._min_news_send_interval_ms) or 5074
	end
	local min_interval = tonumber(SMILive._min_news_send_interval_ms) or 5074
	local remaining = min_interval - elapsed
	if remaining <= 0 then
		return 0
	end
	return math.floor(remaining + 0.5)
end

function M._mark_news_send_timestamp()
	local SMILive = ctx.SMILive
	local timing = SMILive._news_send_timing or {}
	timing.last_send_ms = M._get_now_ms_monotonic()
	SMILive._news_send_timing = timing
end

-- === Send sequence ===

local function send_sequence(messages, method, interval, key)
	if type(messages) ~= "table" or #messages == 0 then
		return false
	end

	local safe_messages = {}
	for _, msg in ipairs(messages) do
		if type(msg) == "string" and msg ~= "" then
			safe_messages[#safe_messages + 1] = msg
		end
	end

	if #safe_messages == 0 then
		return false
	end

	local delay = math.max(0, tonumber(interval) or 0)
	delay = math.floor(delay + 0.5)
	local target = method or M.get_selected_method()

	local State = ctx.State
	if State.send_sequence_running then
		M.update_status("Отправка уже выполняется. Дождитесь завершения или отмените.")
		return false
	end

	local send_fn = get_send_fn()
	if not send_fn then
		return false
	end

	State.send_sequence_running = true

	ctx.run_async("отправки сообщений", function()
		if State.send_sequence_cancel then
			State.send_sequence_cancel = false
			State.send_sequence_running = false
			return
		end

		local function wait_with_cancel(delay_ms)
			if not wait or delay_ms <= 0 then
				return true
			end
			local remaining = delay_ms
			local step = 100
			while remaining > 0 do
				if State.send_sequence_cancel then
					return false
				end
				local chunk = remaining > step and step or remaining
				wait(chunk)
				remaining = remaining - chunk
			end
			return not State.send_sequence_cancel
		end

		for idx, msg in ipairs(safe_messages) do
			if State.send_sequence_cancel then
				State.send_sequence_cancel = false
				break
			end
			local is_news_msg = M._is_news_command_message(msg)
			if is_news_msg then
				local wait_before_send = M._get_news_send_cooldown_remaining_ms()
				if wait_before_send > 0 and wait then
					if not wait_with_cancel(wait_before_send) then
						State.send_sequence_cancel = false
						break
					end
				end
			end
			local ok, err = pcall(send_fn, msg, target)
			if not ok then
				M.update_status("Не удалось отправить сообщение #%d: %s", idx, err)
				break
			end

			if is_news_msg then
				M._mark_news_send_timestamp()
			end

			if idx < #safe_messages and delay > 0 and wait then
				if not wait_with_cancel(delay) then
					State.send_sequence_cancel = false
					break
				end
			end
		end

		State.send_sequence_running = false
	end)

	return true
end

function M.broadcast_sequence(messages, key)
	if M.get_selected_method() == 3 then
		return false
	end
	local interval = tonumber(ctx.MathQuiz.chat_interval_ms) or 0
	if interval < 0 then
		interval = 0
	end
	interval = math.floor(interval + 0.5)
	return send_sequence(messages, M.get_selected_method(), interval, key)
end

-- === Broadcast helpers ===

function M.send_live_sequence_from_section(section, section_name, key, quiz_kind)
	local messages = ctx.SMILive._get_live_section_news_messages(section, quiz_kind)
	if not messages then
		M.update_status('Добавьте текст для раздела "%s".', section_name)
		return false
	end

	if M.get_selected_method() == 3 then
		M.update_status(
			'Сообщения для раздела "%s" не отправлены: выбран режим "В пустоту".',
			section_name
		)
		return false
	end

	local ok = M.broadcast_sequence(messages, key)
	if not ok then
		return false
	end
	M.update_status("%s отправлено в эфир.", section_name)
	return true
end

function M.broadcast_problem(problem)
	if type(problem) ~= "string" or problem == "" then
		return
	end
	local messages = {
		string.format("%s %s", ctx.NEWS_PREFIX, problem),
	}
	if M.broadcast_sequence(messages, "math_problem") then
		M.start_sms_listener(true)
	end
end

function M.broadcast_correct_answer_gender(player_name, answer, score, is_final, player_id, gender, key)
	local normalized_gender = gender == "female" and "female" or "male"
	local submit_verb = normalized_gender == "female" and "прислала" or "прислал"
	local normalized = ctx.normalize_player_name(player_name)
	if normalized == "" then
		normalized = ctx.trim(player_name)
	end
	if type(normalized) ~= "string" or normalized == "" then
		return
	end
	local answer_text = answer ~= nil and tostring(answer) or "-"
	local gained = 1
	local score_phrase = ctx.format_score_progress(score or 0, gained, normalized_gender)
	local broadcast_name = ctx.format_broadcast_name(normalized, player_id)
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local messages = {
		string.format("%s Стоп!", NEWS_PREFIX),
		string.format("%s У нас есть правильный ответ!", NEWS_PREFIX),
		string.format("%s Правильный ответ был: %s", NEWS_PREFIX, answer_text),
		string.format("%s Верный ответ %s..", NEWS_PREFIX, submit_verb),
		string.format("%s %s! %s", NEWS_PREFIX, broadcast_name, score_phrase),
	}
	if is_final then
		messages[#messages + 1] = string.format(
			"%s Викторина завершена! %s набирает %s и побеждает!",
			NEWS_PREFIX,
			broadcast_name,
			ctx.pluralize_points(score or 0)
		)
	end
	return M.broadcast_sequence(messages, key)
end

function M.broadcast_winner_gender(player_name, score, player_id, gender, answer, points_awarded, key)
	local normalized_gender = gender == "female" and "female" or "male"
	local normalized = ctx.normalize_player_name(player_name)
	if normalized == "" then
		normalized = ctx.trim(player_name)
	end
	if type(normalized) ~= "string" or normalized == "" then
		return
	end

	local broadcast_name = ctx.format_broadcast_name(normalized, player_id)
	local score_text = ctx.pluralize_points(score or 0)
	local answer_text = ctx.trim(answer)
	local submit_verb = normalized_gender == "female" and "прислала" or "прислал"
	local gained = math.max(0, math.floor(tonumber(points_awarded) or 1))
	local score_phrase = ctx.format_score_progress(score or 0, gained, normalized_gender)

	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local messages = {
		string.format("%s Стоп!", NEWS_PREFIX),
		string.format("%s У нас есть правильный ответ!", NEWS_PREFIX),
	}

	if answer_text ~= "" then
		messages[#messages + 1] =
			string.format("%s Правильный ответ был: %s", NEWS_PREFIX, answer_text)
	end

	messages[#messages + 1] = string.format("%s Верный ответ %s..", NEWS_PREFIX, submit_verb)
	messages[#messages + 1] = string.format("%s %s! %s", NEWS_PREFIX, broadcast_name, score_phrase)

	local gendered_messages = ctx.get_win_messages_for_gender(normalized_gender)
	local template = gendered_messages
			and #gendered_messages > 0
			and gendered_messages[math.random(1, #gendered_messages)]
		or "Викторина завершена! %s набирает %s и побеждает!"
	local ok_template, formatted_template = pcall(string.format, template, broadcast_name, score_text)
	if not ok_template then
		formatted_template = string.format(
			"Викторина завершена! %s набирает %s и побеждает!",
			broadcast_name,
			score_text
		)
	end
	local victory_message = string.format("%s %s", NEWS_PREFIX, formatted_template)

	messages[#messages + 1] = victory_message

	return M.broadcast_sequence(messages, key)
end

-- === Screenshot ===

function M.send_screenshot_message()
	local cfg = ctx.Config.data and ctx.Config.data.quiz or {}
	local text = tostring(cfg.screenshot_message or "")
	if text == "" then
		return true
	end

	local send_fn = get_send_fn()
	if not send_fn then
		return false
	end

	local trim = ctx.trim
	local method = M.get_selected_method()
	local single = trim((text:gsub("[\r\n]+", " ")))
	if single == "" then
		return true
	end
	local ok, err = pcall(send_fn, single, method)
	if not ok then
		M.update_status("Не удалось отправить сообщение: %s", err)
		return false
	end
	return true
end

function M.schedule_live_window_restore(delay_ms)
	local delay = math.max(0, tonumber(delay_ms) or 0)
	local LiveWindow = ctx.LiveWindow
	if lua_thread and lua_thread.create and type(wait) == "function" then
		local ok = pcall(lua_thread.create, function()
			wait(delay)
			if LiveWindow and LiveWindow.show then
				LiveWindow.show[0] = true
			end
		end)
		if ok then
			return
		end
	end
	if LiveWindow and LiveWindow.show then
		LiveWindow.show[0] = true
	end
end

function M.take_live_window_screenshot()
	local funcs = ctx.funcs
	if not funcs or type(funcs.Take_Screenshot) ~= "function" then
		M.update_status("Скриншот недоступен: funcs.Take_Screenshot не найден.")
		return
	end

	local LiveWindow = ctx.LiveWindow
	if LiveWindow and LiveWindow.show then
		LiveWindow.show[0] = false
	end

	if lua_thread and lua_thread.create and type(wait) == "function" then
		local ok = pcall(lua_thread.create, function()
			M.send_screenshot_message()
			wait(150)
			local ok_shot, err = pcall(funcs.Take_Screenshot)
			if not ok_shot then
				M.update_status("Не удалось сделать скриншот: %s", err)
			end
			wait(80)
			if LiveWindow and LiveWindow.show then
				LiveWindow.show[0] = true
			end
		end)
		if ok then
			return
		end
	end

	M.send_screenshot_message()
	if type(wait) == "function" then
		wait(50)
	end
	local ok, err = pcall(funcs.Take_Screenshot)
	if not ok then
		M.update_status("Не удалось сделать скриншот: %s", err)
	end
	M.schedule_live_window_restore(80)
end

-- === SMS listener ===

local function strip_color_codes(text)
	return (tostring(text or ""):gsub("{%x%x%x%x%x%x}", ""))
end

function M.parse_sms_message(text)
	if type(text) ~= "string" then
		return nil
	end

	local trim = ctx.trim
	local cleaned = strip_color_codes(text)
	local pattern =
		"%[[^%]]-на студию%]%s*Слушатель%s*:?-?%s*([%w_]+)%[(%d+)%]%s*:%s*(.+)"
	local name, id, message = cleaned:match(pattern)

	if not name then
		if cleaned:find("Слушатель") or cleaned:find("слушатель") then
			name, id, message = cleaned:match("([%w_]+)%[(%d+)%]%s*:%s*(.+)")
		end
	end

	if not name then
		return nil
	end
	return trim(name), tonumber(id), trim(message)
end

function M.can_use_sms_listener()
	local my_hooks_module = ctx.my_hooks_module
	return my_hooks_module
		and type(my_hooks_module.addServerMessageListener) == "function"
		and type(my_hooks_module.removeServerMessageListener) == "function"
end

local function handle_server_sms(color, text)
	local name, player_id, message = M.parse_sms_message(text)
	if not name then
		return
	end
	local State = ctx.State
	local sms_target_quiz = State.sms_target_quiz
	local MathQuiz = ctx.MathQuiz
	local CapitalsQuiz = ctx.CapitalsQuiz
	if sms_target_quiz == "capitals" then
		if CapitalsQuiz.active or CapitalsQuiz.awaiting_next_round then
			ctx.record_capitals_sms(name, player_id, message)
		elseif MathQuiz.active or MathQuiz.awaiting_next_round then
			ctx.record_math_sms(name, player_id, message)
		end
	elseif sms_target_quiz == "math" then
		if MathQuiz.active or MathQuiz.awaiting_next_round then
			ctx.record_math_sms(name, player_id, message)
		elseif CapitalsQuiz.active or CapitalsQuiz.awaiting_next_round then
			ctx.record_capitals_sms(name, player_id, message)
		end
	else
		if CapitalsQuiz.active or CapitalsQuiz.awaiting_next_round then
			ctx.record_capitals_sms(name, player_id, message)
		else
			ctx.record_math_sms(name, player_id, message)
		end
	end
end

function M.start_sms_listener(silent)
	local State = ctx.State
	if State.sms_listener_active then
		return true
	end
	if not M.can_use_sms_listener() then
		if not silent then
			M.update_status("Модуль приёма SMS недоступен.")
		end
		return false
	end
	ctx.my_hooks_module.addServerMessageListener(handle_server_sms)
	State.sms_listener_active = true
	if not silent then
		M.update_status(
			"Приём SMS-сообщений активирован. Ждите ответы слушателей."
		)
	end
	return true
end

function M.stop_sms_listener(silent)
	local State = ctx.State
	if not State.sms_listener_active then
		return true
	end
	if not M.can_use_sms_listener() then
		State.sms_listener_active = false
		if not silent then
			M.update_status("Приём SMS-сообщений недоступен.")
		end
		return false
	end
	ctx.my_hooks_module.removeServerMessageListener(handle_server_sms)
	State.sms_listener_active = false
	if not silent then
		M.update_status("Приём SMS-сообщений остановлен.")
	end
	return true
end

return M
