local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local str = ffi.string
local ctx

local SCOREBOARD_HEIGHT = 170
local RESPONSES_HEIGHT = 150

function M.init(c)
	ctx = c
end

-- === Problem generation ===

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
	return divisors[math.random(1, #divisors)]
end

local function generate_two_operand_problem()
	local ops = { "+", "-", "*", "/" }
	local op = ops[math.random(1, #ops)]
	local a, b, answer
	if op == "+" then
		a = math.random(2, 30)
		b = math.random(2, 30)
		answer = a + b
	elseif op == "-" then
		b = math.random(2, 25)
		a = math.random(b + 1, 35)
		answer = a - b
	elseif op == "*" then
		a = math.random(2, 12)
		b = math.random(2, 12)
		answer = a * b
	else
		b = math.random(2, 12)
		local quotient = math.random(2, 12)
		a = b * quotient
		answer = quotient
	end
	local text = string.format("%d %s %d", a, op, b)
	return text, answer
end

local function generate_multi_step_problem()
	local builders = {
		function()
			local a = math.random(2, 20)
			local b = math.random(2, 10)
			local c = math.random(2, 10)
			local answer = a + b * c
			return string.format("%d + %d * %d", a, b, c), answer
		end,
		function()
			local a = math.random(2, 10)
			local b = math.random(2, 10)
			local c = math.random(2, 8)
			local answer = (a + b) * c
			return string.format("(%d + %d) * %d", a, b, c), answer
		end,
		function()
			local a = math.random(2, 9)
			local b = math.random(2, 9)
			local product = a * b
			local c = pick_divisor(product)
			if c == 1 then
				c = 2
			end
			local answer = product / c
			return string.format("(%d * %d) / %d", a, b, c), answer
		end,
		function()
			local a = math.random(2, 10)
			local b = math.random(2, 10)
			local product = a * b
			local c = math.random(2, product - 1)
			local answer = product - c
			return string.format("(%d * %d) - %d", a, b, c), answer
		end,
		function()
			local c = math.random(2, 9)
			local quotient = math.random(2, 9)
			local total = c * quotient
			local a = math.random(2, total - 2)
			local b = total - a
			if b <= 1 then
				b = 2
				a = total - b
			end
			local answer = quotient
			return string.format("(%d + %d) / %d", a, b, c), answer
		end,
	}
	local builder = builders[math.random(1, #builders)]
	local text, answer = builder()
	if answer <= 0 or answer ~= math.floor(answer) then
		return generate_multi_step_problem()
	end
	return text, answer
end

local function generate_math_problem()
	if math.random() < 0.5 then
		return generate_two_operand_problem()
	end
	return generate_multi_step_problem()
end

-- === Extract numeric answer from text ===

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

-- === Game state management ===

local function reset_buffers()
	local MathQuiz = ctx.MathQuiz
	imgui.StrCopy(MathQuiz.player_id_buf, "")
	imgui.StrCopy(MathQuiz.player_name_buf, "")
end

local function reset_scoreboard()
	local MathQuiz = ctx.MathQuiz
	local State = ctx.State
	MathQuiz.players = {}
	ctx.mark_math_scoreboard_dirty()
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
	State.sms_target_quiz = "math"
	ctx.update_status(
		"Игра сброшена. Сгенерируйте пример, чтобы начать раунд."
	)
	reset_buffers()
	ctx.Config:clearRuntimeState()
end

local function start_new_game()
	local MathQuiz = ctx.MathQuiz
	MathQuiz.active = true
	reset_scoreboard()
	ctx.update_status(
		"Игра началась. Цель - %d очка(ов).",
		MathQuiz.target_scores[MathQuiz.target_index]
	)
end

local function end_game(winner)
	local MathQuiz = ctx.MathQuiz
	MathQuiz.active = false
	MathQuiz.winner = winner
	ctx.update_status(
		"%s достигает %d очков и побеждает!",
		winner,
		MathQuiz.target_scores[MathQuiz.target_index]
	)
	MathQuiz.current_problem = nil
	MathQuiz.current_answer = nil
	MathQuiz.round_answer = nil
	MathQuiz.answer_start_time = nil
	MathQuiz.accepting_answers = false
	MathQuiz.awaiting_next_round = false
	MathQuiz.custom_error = nil
	ctx.Config:clearRuntimeState()
end

local function stop_math_game_manually()
	local MathQuiz = ctx.MathQuiz
	MathQuiz.active = false
	ctx.update_status("Игра завершена вручную.")
	MathQuiz.current_problem = nil
	MathQuiz.current_answer = nil
	MathQuiz.round_answer = nil
	MathQuiz.answer_start_time = nil
	MathQuiz.accepting_answers = false
	MathQuiz.awaiting_next_round = false
	MathQuiz.custom_error = nil
	ctx.Config:clearRuntimeState()
end

-- === Player management ===

local function ensure_player(name, player_id)
	local MathQuiz = ctx.MathQuiz
	local normalized = ctx.normalize_player_name(name)
	if normalized == "" then
		return nil, normalized
	end
	local entry = MathQuiz.players[normalized]
	if not entry then
		entry = { score = 0, last_answer = nil, last_correct = false }
		MathQuiz.players[normalized] = entry
		ctx.mark_math_scoreboard_dirty()
	end
	if player_id ~= nil then
		local prev_id = entry.player_id
		entry.player_id = player_id
		if prev_id ~= player_id then
			ctx.mark_math_scoreboard_dirty()
		end
	end
	return entry, normalized
end

local function iterate_players_sorted()
	local scoreboard_cache = ctx.scoreboard_cache
	local cache = scoreboard_cache.math
	if not cache.dirty then
		return cache.list
	end
	local MathQuiz = ctx.MathQuiz
	local list = {}
	for name, data in pairs(MathQuiz.players) do
		table.insert(
			list,
			{
				name = name,
				player_id = data.player_id,
				score = data.score,
				last_correct = data.last_correct,
				last_answer = data.last_answer,
			}
		)
	end
	table.sort(list, function(a, b)
		if a.score == b.score then
			return a.name:lower() < b.name:lower()
		end
		return a.score > b.score
	end)
	cache.list = list
	cache.dirty = false
	return list
end

local function update_player_last_answer(name, provided, is_correct, player_id)
	local entry, normalized = ensure_player(name, player_id)
	if not entry then
		return nil, normalized
	end
	entry.last_correct = is_correct and true or false
	entry.last_answer = provided
	ctx.mark_math_scoreboard_dirty()
	return entry, normalized
end

-- === Round management ===

local function start_round(problem, answer)
	local MathQuiz = ctx.MathQuiz
	MathQuiz.current_problem = problem
	MathQuiz.current_answer = answer
	MathQuiz.round_answer = answer
	MathQuiz.show_answer[0] = false
	MathQuiz.answer_start_time = os.clock()
	MathQuiz.accepting_answers = true
	MathQuiz.current_responses = {}
	MathQuiz.first_correct = nil
	MathQuiz.latest_round_stats = nil
	MathQuiz.awaiting_next_round = false
	MathQuiz.custom_error = nil
	imgui.StrCopy(MathQuiz.custom_problem_buf, tostring(problem or ""), ffi.sizeof(MathQuiz.custom_problem_buf))
	ctx.update_status("Раунд %d: огласите пример и ждите ответы.", MathQuiz.round + 1)
	reset_buffers()
	ctx.mark_runtime_save_dirty()
end

local function begin_round()
	local problem, answer = generate_math_problem()
	start_round(problem, answer)
end

local function evaluate_custom_problem(problem_text)
	local trim = ctx.trim
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

function M._resolve_math_custom_problem_answer(problem_text)
	local trim = ctx.trim
	local prepared_problem = trim(problem_text or "")
	if prepared_problem == "" then
		return nil, "Введите текст перед отправкой."
	end

	local _, direct_result = evaluate_custom_problem(prepared_problem)
	if type(direct_result) == "number" then
		return direct_result, nil
	end
	local direct_error = type(direct_result) == "string" and direct_result or nil

	local extracted_expression = nil
	for part in prepared_problem:gmatch("[%d%+%-%*/%(%)%s]+") do
		local candidate = trim(part)
		if candidate ~= "" and candidate:find("%d") and candidate:find("[%+%-%*/]") then
			if extracted_expression == nil or #candidate > #extracted_expression then
				extracted_expression = candidate
			end
		end
	end
	if extracted_expression then
		local _, extracted_result = evaluate_custom_problem(extracted_expression)
		if type(extracted_result) == "number" then
			return extracted_result, nil
		end
		if type(extracted_result) == "string" and extracted_result ~= "" then
			return nil, extracted_result
		end
	end

	if direct_error and direct_error ~= "" then
		return nil, direct_error
	end

	return nil, "Не удалось определить ответ для примера."
end

local function begin_custom_round(problem_text)
	local trim = ctx.trim
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

-- === Answer handling ===

local function handle_correct_answer(player_name, player_id)
	local MathQuiz = ctx.MathQuiz
	local entry, normalized = ensure_player(player_name, player_id)
	if not entry then
		return
	end
	player_name = normalized
	entry.score = entry.score + 1
	entry.last_correct = true
	entry.last_answer = MathQuiz.current_answer
	ctx.mark_math_scoreboard_dirty()

	MathQuiz.round = MathQuiz.round + 1
	MathQuiz.round_answer = MathQuiz.round_answer or MathQuiz.current_answer
	MathQuiz.current_problem = nil
	MathQuiz.current_answer = nil
	MathQuiz.show_answer[0] = false
	MathQuiz.answer_start_time = nil
	MathQuiz.accepting_answers = false

	local target = MathQuiz.target_scores[MathQuiz.target_index]
	if entry.score >= target then
		MathQuiz.awaiting_next_round = false
		end_game(player_name)
	else
		MathQuiz.awaiting_next_round = true
		ctx.update_status(
			"%s получает балл (%d/%d). Запустите следующий пример.",
			player_name,
			entry.score,
			target
		)
	end
end

local function finalize_manual_math_round_winner(player_name, player_id)
	local MathQuiz = ctx.MathQuiz
	local answer_value = MathQuiz.current_answer or MathQuiz.round_answer
	if answer_value == nil then
		return nil, "Не найден текущий ответ примера."
	end

	local reference_response = ctx.find_response_for_player(MathQuiz.current_responses, player_name, player_id, true)
	local response_time = reference_response and reference_response.response_time or nil
	local lead = reference_response and ctx.compute_lead_time(reference_response, MathQuiz.current_responses) or nil

	handle_correct_answer(player_name, player_id)

	local player_entry, normalized_name = ensure_player(player_name, player_id)
	if not player_entry then
		return nil, "Не удалось обновить счёт игрока."
	end

	MathQuiz.latest_round_stats = {
		winner = normalized_name,
		player_id = player_entry.player_id,
		response_time = response_time,
		lead = lead,
		total_responses = #MathQuiz.current_responses,
		correct_answer = answer_value,
		score = player_entry.score or 0,
		points_awarded = 1,
		game_finished = not MathQuiz.active,
		manual = true,
	}
	if MathQuiz.active then
		ctx.mark_runtime_save_dirty()
	else
		ctx.Config:clearRuntimeState()
	end

	return MathQuiz.latest_round_stats
end

-- === SMS response recording (called from broadcast via ctx.record_math_sms) ===

function M.record_response_from_sms(player_name, player_id, message)
	local MathQuiz = ctx.MathQuiz
	if not MathQuiz.active then
		return
	end

	if not MathQuiz.answer_start_time then
		return
	end

	if not MathQuiz.accepting_answers then
		return
	end

	local correct_value_reference = MathQuiz.current_answer or MathQuiz.round_answer
	if not correct_value_reference and MathQuiz.latest_round_stats then
		correct_value_reference = MathQuiz.latest_round_stats.correct_answer
	end

	if not correct_value_reference then
		return
	end

	local normalized_name = ctx.normalize_player_name(player_name)
	if normalized_name ~= "" then
		player_name = normalized_name
	else
		player_name = ctx.trim(player_name)
	end

	local response_time = os.clock() - MathQuiz.answer_start_time
	if response_time < 0 then
		response_time = 0
	end

	local numeric_answer = extract_numeric_answer(message)
	local is_correct = numeric_answer ~= nil and correct_value_reference == numeric_answer
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
		local player_entry = ensure_player(player_name, player_id)
		local lead = ctx.compute_lead_time(entry)
		MathQuiz.latest_round_stats = {
			winner = player_name,
			player_id = player_id,
			response_time = entry.response_time,
			lead = lead,
			total_responses = #MathQuiz.current_responses,
			correct_answer = correct_value_reference,
			score = player_entry and player_entry.score or 0,
			points_awarded = 0,
			game_finished = false,
			recommended = true,
		}

		if lead then
			ctx.update_status(
				"Первый верный ответ: %s за %.2f с (опережение %.2f с). Балл начислите вручную.",
				player_name,
				entry.response_time,
				lead
			)
		else
			ctx.update_status(
				"Первый верный ответ: %s за %.2f с. Балл начислите вручную.",
				player_name,
				entry.response_time
			)
		end
		ctx.mark_runtime_save_dirty()

		return
	end

	if is_correct then
		entry.outcome = "correct"
		ctx.update_status(
			"%s прислал верный ответ через %.2f с.",
			player_name,
			entry.response_time
		)
	else
		entry.outcome = "wrong"
		local provided = message ~= "" and message or "-"
		update_player_last_answer(player_name, provided, false, player_id)
		ctx.update_status(
			"%s отвечает через %.2f с: %s (неверно).",
			player_name,
			entry.response_time,
			provided
		)
	end
	ctx.mark_runtime_save_dirty()
end

-- === Announcement ===

local function announce_latest_stats(gender)
	local MathQuiz = ctx.MathQuiz
	local stats = MathQuiz.latest_round_stats
	if not stats or not stats.winner then
		ctx.update_status("Нет данных для объявления ответа.")
		return
	end

	local normalized_gender = gender == "female" and "female" or "male"
	local has_winner = MathQuiz.winner ~= nil
	local display_name = ctx.format_display_name(stats.winner, stats.player_id)
	local subject_label = has_winner and "победителе" or "ответе"
	local send_key = "math_announce_" .. normalized_gender

	ctx.stop_sms_for_announcement()
	ctx.update_status(
		"Отправляем сообщение об %s для %s (%s)...",
		subject_label,
		display_name,
		normalized_gender == "female" and "ж" or "м"
	)

	local sent
	if has_winner then
		sent = ctx.broadcast_winner_gender(
			stats.winner,
			stats.score,
			stats.player_id,
			normalized_gender,
			stats.correct_answer,
			stats.points_awarded,
			send_key
		)
	else
		sent = ctx.broadcast_correct_answer_gender(
			stats.winner,
			stats.correct_answer,
			stats.score,
			stats.game_finished,
			stats.player_id,
			normalized_gender,
			send_key
		)
	end

	if sent then
		ctx.update_status("Сообщение об %s отправлено для %s.", subject_label, display_name)
	end
end

local function announce_math_answer_from_fields(gender)
	local MathQuiz = ctx.MathQuiz
	if not MathQuiz.active then
		ctx.update_status("Игра не активна. Сначала начните раунд.")
		return
	end

	if not MathQuiz.answer_start_time then
		ctx.update_status("Нет активного примера для проверки.")
		return
	end
	if not MathQuiz.accepting_answers then
		ctx.update_status("Раунд уже закрыт. Запустите следующий пример.")
		return
	end

	local name, player_id, err = ctx.resolve_player_from_inputs(MathQuiz.player_id_buf, MathQuiz.player_name_buf)
	if err then
		ctx.update_status(err)
		return
	end

	local _, finalize_err = finalize_manual_math_round_winner(name, player_id)
	if finalize_err then
		ctx.update_status(finalize_err)
		return
	end

	announce_latest_stats(gender)
	reset_buffers()
end

-- === Message building for news input panel ===

function M._build_math_round_answer_message(gender)
	local MathQuiz = ctx.MathQuiz
	local trim = ctx.trim
	local normalized_gender = gender == "female" and "female" or "male"
	local name, player_id, err = ctx.resolve_player_from_inputs(MathQuiz.player_id_buf, MathQuiz.player_name_buf)
	if err then
		return nil, err
	end

	local display_name = ctx.format_broadcast_name(name, player_id)
	if type(display_name) ~= "string" or display_name == "" then
		display_name = ctx.normalize_player_name(name)
	end
	if display_name == "" then
		display_name = trim(name)
	end

	local normalized_name = ctx.normalize_player_name(name)
	local entry = normalized_name ~= "" and MathQuiz.players[normalized_name] or nil
	local current_score = type(entry) == "table" and tonumber(entry.score) or 0
	current_score = math.max(0, math.floor(current_score or 0))
	local projected_score = current_score
	if MathQuiz.accepting_answers and MathQuiz.answer_start_time then
		projected_score = projected_score + 1
	end
	if projected_score <= 0 then
		projected_score = 1
	end
	local score_phrase = ctx.format_score_progress(projected_score, 1, normalized_gender)

	local SMILive = ctx.SMILive
	local templates = SMILive._get_round_message_templates_for_gender(normalized_gender)
	local template = templates[math.random(1, #templates)] or SMILive._get_default_round_message_template(normalized_gender)

	local ok, formatted = pcall(string.format, template, display_name, score_phrase, projected_score)
	if not ok or type(formatted) ~= "string" or trim(formatted) == "" then
		formatted = string.format("%s. %s", display_name, score_phrase)
	end

	return formatted, nil
end

function M._build_math_winner_message(gender)
	local MathQuiz = ctx.MathQuiz
	local trim = ctx.trim
	local SMILive = ctx.SMILive
	local normalized_gender = gender == "female" and "female" or "male"
	local winner_name = trim(tostring(MathQuiz.winner or ""))
	if winner_name == "" then
		return nil, "Победитель ещё не определён."
	end

	local normalized_winner = ctx.normalize_player_name(winner_name)
	if normalized_winner == "" then
		normalized_winner = winner_name
	end

	local entry = MathQuiz.players and MathQuiz.players[normalized_winner] or nil
	local player_id = type(entry) == "table" and entry.player_id or nil
	local score = type(entry) == "table" and tonumber(entry.score) or nil
	local stats = MathQuiz.latest_round_stats
	if (score == nil or score <= 0) and type(stats) == "table" then
		local stats_winner = ctx.normalize_player_name(stats.winner or "")
		if stats_winner == normalized_winner then
			score = tonumber(stats.score)
			if player_id == nil then
				player_id = stats.player_id
			end
		end
	end
	score = math.max(0, math.floor(score or 0))
	if score <= 0 then
		local target_score = tonumber(MathQuiz.target_scores[MathQuiz.target_index]) or 1
		score = math.max(1, math.floor(target_score))
	end

	local broadcast_name = ctx.format_broadcast_name(normalized_winner, player_id)
	if type(broadcast_name) ~= "string" or broadcast_name == "" then
		broadcast_name = normalized_winner
	end
	local score_text = ctx.pluralize_points(score)
	local gendered_messages = ctx.get_win_messages_for_gender(normalized_gender)
	local template = gendered_messages
			and #gendered_messages > 0
			and gendered_messages[math.random(1, #gendered_messages)]
		or "Викторина завершена! %s набирает %s и побеждает!"
	local ok_template, formatted_template = pcall(string.format, template, broadcast_name, score_text)
	if not ok_template or type(formatted_template) ~= "string" or trim(formatted_template) == "" then
		formatted_template = string.format(
			"Викторина завершена! %s набирает %s и побеждает!",
			broadcast_name,
			score_text
		)
	end
	return formatted_template, nil
end

-- === UI ===

local function draw_scoreboard_table(height)
	local MathQuiz = ctx.MathQuiz
	local escape_imgui_text = ctx.escape_imgui_text
	if
		imgui.BeginChild("math_quiz_scoreboard", imgui.ImVec2(0, height), true, imgui.WindowFlags.HorizontalScrollbar)
	then
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
			imgui.Text(escape_imgui_text(ctx.format_player_label(row.name, row.player_id)))
			if MathQuiz.winner and MathQuiz.winner == row.name then
				imgui.SameLine()
				imgui.TextColored(imgui.ImVec4(0.9, 0.8, 0.2, 1), "*")
			end
			imgui.NextColumn()
			imgui.Text(tostring(row.score))
			imgui.NextColumn()
			if row.last_answer ~= nil then
				local color = row.last_correct and imgui.ImVec4(0.4, 1.0, 0.4, 1) or imgui.ImVec4(1.0, 0.4, 0.4, 1)
				imgui.TextColored(color, escape_imgui_text(tostring(row.last_answer)))
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
	local MathQuiz = ctx.MathQuiz
	local escape_imgui_text = ctx.escape_imgui_text
	if
		imgui.BeginChild("math_quiz_responses", imgui.ImVec2(0, height), true, imgui.WindowFlags.HorizontalScrollbar)
	then
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
			imgui.Text(escape_imgui_text(ctx.format_player_label(resp.name, resp.player_id)))
			if resp.outcome == "first" then
				imgui.SameLine()
				imgui.TextColored(imgui.ImVec4(0.9, 0.8, 0.2, 1), "*")
			end
			imgui.NextColumn()
			imgui.Text(resp.player_id and tostring(resp.player_id) or "-")
			imgui.NextColumn()
			local display_answer = resp.answer_text or resp.text
			if not display_answer or display_answer == "" then
				display_answer = "-"
			end
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
			imgui.TextColored(color, escape_imgui_text(display_answer))
			imgui.NextColumn()
			if resp.response_time then
				imgui.Text(ctx.format_seconds(resp.response_time))
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
	local MathQuiz = ctx.MathQuiz
	local escape_imgui_text = ctx.escape_imgui_text
	local stats = MathQuiz.latest_round_stats
	if not stats or not stats.winner then
		return
	end

	local winner = stats.winner or "-"
	local player_id = stats.player_id and tostring(stats.player_id) or "-"
	local response_time = stats.response_time and string.format("%.2fс", stats.response_time) or "-"
	local total_responses = stats.total_responses or 0
	local summary
	if stats.points_awarded and stats.points_awarded > 0 then
		summary = string.format(
			"Победитель раунда: %s[%s] - %s | Всего ответов: %d",
			winner,
			player_id,
			response_time,
			total_responses
		)
	else
		summary = string.format(
			"Рекомендация (первый верный): %s[%s] - %s | Всего ответов: %d",
			winner,
			player_id,
			response_time,
			total_responses
		)
	end
	imgui.Text(escape_imgui_text(summary))
end

local function draw_math_quiz_tables_section()
	local MathQuiz = ctx.MathQuiz
	local trim = ctx.trim
	local escape_imgui_text = ctx.escape_imgui_text
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local State = ctx.State

	imgui.Text("Таблица очков")
	draw_scoreboard_table(SCOREBOARD_HEIGHT)

	imgui.Spacing()
	imgui.Text("Ответы текущего раунда")
	draw_responses_table(RESPONSES_HEIGHT)

	imgui.Spacing()
	draw_latest_round_stats()

	imgui.Spacing()
	imgui.PushItemWidth(70)
	if imgui.InputText("ID##math_player_id", MathQuiz.player_id_buf, 8) then
		local parsed_id = ctx.parse_player_id_from_buf(MathQuiz.player_id_buf)
		if parsed_id then
			ctx.try_fill_name_from_id(MathQuiz.player_name_buf, parsed_id)
		end
	end
	imgui.PopItemWidth()

	imgui.SameLine()
	imgui.PushItemWidth(240)
	if imgui.InputText("Ник игрока##math_player_name", MathQuiz.player_name_buf, 48) then
		local name = trim(str(MathQuiz.player_name_buf))
		if name ~= "" then
			ctx.try_fill_id_from_name(MathQuiz.player_id_buf, name)
		end
	end
	imgui.PopItemWidth()

	local has_manual_target = trim(str(MathQuiz.player_id_buf)) ~= "" or trim(str(MathQuiz.player_name_buf)) ~= ""
	if not has_manual_target then
		local alpha = imgui.GetStyle().Alpha
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha * 0.5)
	end
	if imgui.Button("Остановить прием сообщений##math_stop_listen") and has_manual_target then
		if not MathQuiz.active then
			ctx.update_status("Игра не активна. Сначала начните раунд.")
		elseif not MathQuiz.answer_start_time then
			ctx.update_status("Нет активного примера для проверки.")
		elseif not MathQuiz.accepting_answers then
			ctx.update_status("Раунд уже закрыт. Запустите следующий пример.")
		else
			local name, player_id, err = ctx.resolve_player_from_inputs(MathQuiz.player_id_buf, MathQuiz.player_name_buf)
			if err then
				ctx.update_status(err)
			else
				local stats, finalize_err = finalize_manual_math_round_winner(name, player_id)
				if finalize_err then
					ctx.update_status(finalize_err)
				else
					local display_name = ctx.format_display_name(stats and stats.winner or name, stats and stats.player_id or player_id)
					local stop_message = string.format("%s %s", NEWS_PREFIX, "Стоп!")
					local sent = ctx.broadcast_sequence({ stop_message }, "math_stop_listen")
					if sent then
						ctx.stop_sms_for_announcement()
						ctx.update_status('Победитель %s зафиксирован. Отправлено сообщение "Стоп!".', display_name)
					elseif ctx.get_selected_method() == 3 then
						ctx.stop_sms_for_announcement()
						ctx.update_status(
							'Победитель %s зафиксирован. Сообщение "Стоп!" не отправлено: выбран режим "В пустоту".',
							display_name
						)
					else
						ctx.update_status('Победитель %s зафиксирован, но сообщение "Стоп!" не отправлено.', display_name)
					end
					ctx.reset_capitals_buffers()
				end
			end
		end
	end
	if not has_manual_target then
		imgui.PopStyleVar()
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

-- === Main draw function ===

function M.DrawMathQuiz(show_tables)
	local MathQuiz = ctx.MathQuiz
	local SMILive = ctx.SMILive
	local trim = ctx.trim
	local escape_imgui_text = ctx.escape_imgui_text
	local mimgui_funcs = ctx.mimgui_funcs
	local NEWS_PREFIX = ctx.NEWS_PREFIX
	local push_button_palette = ctx.push_button_palette
	local pop_button_palette = ctx.pop_button_palette
	local COLOR_ACCENT_PRIMARY = ctx.COLOR_ACCENT_PRIMARY
	local COLOR_ACCENT_SUCCESS = ctx.COLOR_ACCENT_SUCCESS
	local COLOR_ACCENT_DANGER = ctx.COLOR_ACCENT_DANGER

	imgui.TextWrapped(escape_imgui_text(MathQuiz.status_text or ""))
	imgui.Dummy(imgui.ImVec2(0, 4))

	if not MathQuiz.active then
		for idx, target in ipairs(MathQuiz.target_scores) do
			if idx > 1 then
				imgui.SameLine()
			end
			imgui.PushIDInt(idx)
			if imgui.RadioButtonBool(string.format("%d очка", target), MathQuiz.target_index == idx) then
				MathQuiz.target_index = idx
				ctx.Config:save()
			end
			imgui.PopID()
		end

		imgui.SameLine()
		push_button_palette(COLOR_ACCENT_SUCCESS)
		if imgui.Button("Начать игру") then
			start_new_game()
		end
		pop_button_palette()
	else
		if MathQuiz.awaiting_next_round then
			push_button_palette(COLOR_ACCENT_PRIMARY)
			if imgui.Button("Следующий пример") then
				begin_round()
			end
			pop_button_palette()
		else
			push_button_palette(COLOR_ACCENT_PRIMARY)
			if imgui.Button("Сгенерировать пример") then
				begin_round()
			end
			pop_button_palette()
		end

		imgui.SameLine()
		push_button_palette(COLOR_ACCENT_DANGER)
		if imgui.Button("Завершить игру") then
			stop_math_game_manually()
		end
		pop_button_palette()

		imgui.Dummy(imgui.ImVec2(0, 4))
		imgui.Separator()
		local typed_problem = trim(str(MathQuiz.custom_problem_buf))
		local displayed_problem = typed_problem ~= "" and typed_problem or MathQuiz.current_problem
		local displayed_answer = MathQuiz.current_answer
		local typed_problem_error = nil
		if typed_problem ~= "" then
			local resolved_answer, resolve_err = M._resolve_math_custom_problem_answer(typed_problem)
			if type(resolved_answer) == "number" then
				displayed_answer = resolved_answer
			else
				displayed_answer = nil
				typed_problem_error = resolve_err
			end
		end

		if displayed_problem then
			imgui.Text(escape_imgui_text(string.format("Текущий пример: %s", displayed_problem)))
			if displayed_answer then
				imgui.SameLine()
				imgui.TextColored(
					imgui.ImVec4(0.4, 1.0, 0.4, 1),
					escape_imgui_text(string.format("= %s", tostring(displayed_answer)))
				)
			end
		end

		imgui.Text("Текст для отправки:")
		local style = imgui.GetStyle()
		local avail = imgui.GetContentRegionAvail()
		local send_btn_text = "Отправить"
		local send_btn_width = imgui.CalcTextSize(send_btn_text).x + style.FramePadding.x * 2
		local input_width = math.max(180, avail.x - send_btn_width - style.ItemSpacing.x)
		local problem_buf_size = ffi.sizeof(MathQuiz.custom_problem_buf)

		imgui.PushItemWidth(input_width)
		imgui.InputText("##MathQuizCustom", MathQuiz.custom_problem_buf, problem_buf_size)
		imgui.PopItemWidth()
		imgui.SameLine()
		if imgui.Button(send_btn_text .. "##math_send_problem", imgui.ImVec2(send_btn_width, 0)) then
			local prepared_problem = trim(str(MathQuiz.custom_problem_buf))
			if prepared_problem == "" then
				local message = "Введите текст перед отправкой."
				ctx.update_status(message)
				MathQuiz.custom_error = message
			elseif MathQuiz.awaiting_next_round then
				local message = 'Следующий раунд начнется после объявления победителя. Нажмите "Следующий пример".'
				ctx.update_status(message)
				MathQuiz.custom_error = message
			else
				local answer_value, answer_error = M._resolve_math_custom_problem_answer(prepared_problem)
				if type(answer_value) ~= "number" then
					local message = answer_error or "Не удалось определить ответ для примера."
					if type(message) == "string" and message:find("целым", 1, true) then
						message = "Ответ примера должен быть целым числом (без дробей)."
					end
					ctx.update_status(message)
					MathQuiz.custom_error = message
				else
					local has_current_answer = type(MathQuiz.current_answer) == "number"
					local should_restart_round = (not has_current_answer) or (answer_value ~= MathQuiz.current_answer)

					if should_restart_round then
						start_round(prepared_problem, answer_value)
					else
						MathQuiz.current_problem = prepared_problem
					end

					if type(MathQuiz.current_problem) == "string" and MathQuiz.current_problem ~= "" then
						MathQuiz.custom_error = nil
						imgui.StrCopy(MathQuiz.custom_problem_buf, MathQuiz.current_problem, problem_buf_size)
						ctx.broadcast_problem(MathQuiz.current_problem)
					end
				end
			end
		end
		local preview_problem = trim(str(MathQuiz.custom_problem_buf))
		local preview_line = ""
		if preview_problem ~= "" then
			preview_line = string.format("%s %s", NEWS_PREFIX, preview_problem)
		end
		mimgui_funcs.imgui_hover_tooltip_safe(ctx.build_send_tooltip(preview_line))
		if type(typed_problem_error) == "string" and typed_problem_error:find("целым", 1, true) then
			imgui.TextColored(imgui.ImVec4(1.0, 0.6, 0.3, 1), "Ответ примера должен быть целым числом (без дробей).")
		end
		if MathQuiz.custom_error then
			imgui.TextColored(imgui.ImVec4(1.0, 0.4, 0.4, 1), escape_imgui_text(MathQuiz.custom_error))
		end
	end

	if show_tables ~= false then
		draw_math_quiz_tables_section()
	end
end

return M
