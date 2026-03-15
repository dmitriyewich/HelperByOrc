local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local str = ffi.string
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local ctx

local SCOREBOARD_HEIGHT = 170
local RESPONSES_HEIGHT = 150

function M.init(c)
	ctx = c
end

-- === Text normalization for answer matching ===

local function is_valid_utf8(text)
	if type(text) ~= "string" then
		return false
	end
	local i = 1
	local len = #text
	while i <= len do
		local c = text:byte(i)
		if c < 0x80 then
			i = i + 1
		elseif c >= 0xC2 and c <= 0xDF then
			local c2 = text:byte(i + 1)
			if not c2 or c2 < 0x80 or c2 > 0xBF then
				return false
			end
			i = i + 2
		elseif c >= 0xE0 and c <= 0xEF then
			local c2 = text:byte(i + 1)
			local c3 = text:byte(i + 2)
			if not c2 or not c3 or c2 < 0x80 or c2 > 0xBF or c3 < 0x80 or c3 > 0xBF then
				return false
			end
			i = i + 3
		elseif c >= 0xF0 and c <= 0xF4 then
			local c2 = text:byte(i + 1)
			local c3 = text:byte(i + 2)
			local c4 = text:byte(i + 3)
			if
				not c2
				or not c3
				or not c4
				or c2 < 0x80
				or c2 > 0xBF
				or c3 < 0x80
				or c3 > 0xBF
				or c4 < 0x80
				or c4 > 0xBF
			then
				return false
			end
			i = i + 4
		else
			return false
		end
	end
	return true
end

local UTF8_UPPER_TO_LOWER = {
	["А"] = "а",
	["Б"] = "б",
	["В"] = "в",
	["Г"] = "г",
	["Д"] = "д",
	["Е"] = "е",
	["Ё"] = "ё",
	["Ж"] = "ж",
	["З"] = "з",
	["И"] = "и",
	["Й"] = "й",
	["К"] = "к",
	["Л"] = "л",
	["М"] = "м",
	["Н"] = "н",
	["О"] = "о",
	["П"] = "п",
	["Р"] = "р",
	["С"] = "с",
	["Т"] = "т",
	["У"] = "у",
	["Ф"] = "ф",
	["Х"] = "х",
	["Ц"] = "ц",
	["Ч"] = "ч",
	["Ш"] = "ш",
	["Щ"] = "щ",
	["Ъ"] = "ъ",
	["Ы"] = "ы",
	["Ь"] = "ь",
	["Э"] = "э",
	["Ю"] = "ю",
	["Я"] = "я",
}

local function utf8_lower(text)
	text = text:gsub("%a", string.lower)
	return (text:gsub("[%z\1-\127\194-\244][\128-\191]*", function(ch)
		return UTF8_UPPER_TO_LOWER[ch] or ch
	end))
end

local function cp1251_lower(text)
	text = text:gsub("%a", string.lower)
	text = text:gsub("[\192-\223]", function(ch)
		return string.char(ch:byte(1) + 32)
	end)
	text = text:gsub(string.char(168), string.char(184))
	return text
end

local function normalize_capital_text_utf8(text)
	local lowered = utf8_lower(text)
	lowered = lowered:gsub("С'", "Рµ")
	lowered = lowered:gsub("\226\128\144", "")
	lowered = lowered:gsub("\226\128\145", "")
	lowered = lowered:gsub("\226\128\146", "")
	lowered = lowered:gsub("\226\128\147", "")
	lowered = lowered:gsub("\226\128\148", "")
	lowered = lowered:gsub("\226\136\146", "")
	lowered = lowered:gsub("\194\183", "")
	lowered = lowered:gsub("\194\160", "")
	lowered = lowered:gsub("\226\128\152", "")
	lowered = lowered:gsub("\226\128\153", "")
	lowered = lowered:gsub("\202\188", "")
	lowered = lowered:gsub("\203\136", "")
	lowered = lowered:gsub("[%s%p]", "")
	return lowered
end

local function normalize_capital_text_cp1251(text)
	local lowered = cp1251_lower(text)
	lowered = lowered:gsub(string.char(184), string.char(229))
	lowered = lowered:gsub(string.char(150), "")
	lowered = lowered:gsub(string.char(151), "")
	lowered = lowered:gsub(string.char(145), "")
	lowered = lowered:gsub(string.char(146), "")
	lowered = lowered:gsub(string.char(160), "")
	lowered = lowered:gsub("[%s%p]", "")
	return lowered
end

local function build_capital_variants(text)
	local variants = {}
	local trim = ctx.trim
	local cleaned = trim(text or "")
	if cleaned == "" then
		return variants
	end

	local function add_variant(value)
		if not value or value == "" then
			return
		end
		for _, existing in ipairs(variants) do
			if existing == value then
				return
			end
		end
		variants[#variants + 1] = value
	end

	local is_utf = is_valid_utf8(cleaned)
	if is_utf then
		add_variant(normalize_capital_text_utf8(cleaned))
		if u8 and type(u8.encode) == "function" then
			local encoded = u8:encode(cleaned)
			if encoded and encoded ~= "" then
				add_variant(normalize_capital_text_cp1251(encoded))
			end
		end
	else
		add_variant(normalize_capital_text_cp1251(cleaned))
		if u8 and type(u8.decode) == "function" then
			local decoded = u8:decode(cleaned)
			if decoded and decoded ~= "" and is_valid_utf8(decoded) then
				add_variant(normalize_capital_text_utf8(decoded))
			end
		end
	end

	return variants
end

local function is_capital_answer_correct(text, correct_variants)
	if type(correct_variants) ~= "table" or #correct_variants == 0 then
		return false
	end
	local provided_variants = build_capital_variants(text)
	for _, provided in ipairs(provided_variants) do
		for _, expected in ipairs(correct_variants) do
			if provided == expected then
				return true
			end
		end
	end
	return false
end

-- === Answer candidate parsing ===

local function normalize_answer_candidate(text)
	local trim = ctx.trim
	local value = trim(text or "")
	if value == "" then
		return ""
	end
	value = value:gsub("[%s%p]+$", "")
	value = trim(value)
	return value
end

local function append_unique_candidate(list, value)
	local candidate = normalize_answer_candidate(value)
	if candidate == "" then
		return
	end
	for _, existing in ipairs(list) do
		if existing == candidate then
			return
		end
	end
	list[#list + 1] = candidate
end

local function build_capitals_answer_candidates(message)
	local trim = ctx.trim
	local candidates = {}
	local base = trim(message or "")
	append_unique_candidate(candidates, base)
	if base == "" then
		return candidates
	end

	append_unique_candidate(candidates, base:match("[Оо]твет%s*[:%-–—=]+%s*(.+)$"))
	append_unique_candidate(candidates, base:match(".*[:=]%s*(.+)$"))
	append_unique_candidate(candidates, base:match(".*%s%-%s+(.+)$"))
	append_unique_candidate(candidates, base:match(".*%s\226\128\147%s+(.+)$"))
	append_unique_candidate(candidates, base:match(".*%s\226\128\148%s+(.+)$"))
	append_unique_candidate(candidates, base:match(".*[%?%!]+%s*(.+)$"))
	append_unique_candidate(candidates, base:match(".*[%.]+%s+(.+)$"))

	return candidates
end

-- === Country management ===

local function get_capitals_country_key(country)
	local CapitalsEditor = ctx.capitals_editor_mod
	local normalized
	if CapitalsEditor and type(CapitalsEditor.normalize_country_key) == "function" then
		normalized = CapitalsEditor.normalize_country_key(tostring(country or ""))
	end
	if type(normalized) == "string" and normalized ~= "" then
		return normalized
	end
	local trim = ctx.trim
	local fallback = trim(tostring(country or ""))
	if fallback == "" then
		return nil
	end
	return string.lower(fallback:gsub("%s+", " "))
end

local function is_capitals_country_announced(country)
	local key = get_capitals_country_key(country)
	if not key then
		return false
	end
	local announced = ctx.CapitalsQuiz.announced_countries
	return type(announced) == "table" and announced[key] and true or false
end

local function mark_capitals_country_announced(country)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local key = get_capitals_country_key(country)
	if not key then
		return false
	end
	if type(CapitalsQuiz.announced_countries) ~= "table" then
		CapitalsQuiz.announced_countries = {}
	end
	if CapitalsQuiz.announced_countries[key] then
		return false
	end
	CapitalsQuiz.announced_countries[key] = true
	return true
end

local function pick_random_capitals_entry()
	local CapitalsQuiz = ctx.CapitalsQuiz
	local entries = CapitalsQuiz.entries or {}
	local count = #entries
	if count == 0 then
		return nil, "Таблица столиц пуста. Заполните capitals_quiz.metropolis в SMILive.json."
	end

	local available = {}
	for _, entry in ipairs(entries) do
		if not is_capitals_country_announced(entry and entry.country) then
			available[#available + 1] = entry
		end
	end

	if #available == 0 then
		return nil, "Все страны уже были озвучены кнопкой \"Отправить вопрос в чат\". Начните новую игру."
	end

	local pool = available
	if CapitalsQuiz.current_entry and #available > 1 then
		local filtered = {}
		for _, entry in ipairs(available) do
			if entry ~= CapitalsQuiz.current_entry then
				filtered[#filtered + 1] = entry
			end
		end
		if #filtered > 0 then
			pool = filtered
		end
	end

	return pool[math.random(1, #pool)]
end

-- === Fact messages ===

local function build_capitals_fact_messages(entry)
	if not entry or type(entry.facts) ~= "table" or #entry.facts == 0 then
		return nil, "Для этой страны нет фактов."
	end

	local fact = entry.facts[math.random(1, #entry.facts)]
	local messages = {}
	for _, part in ipairs(fact) do
		local message, length = ctx.build_news_message_from_text(part)
		if not message then
			return nil, string.format("Факт слишком длинный: %d / %d.", length, ctx.NEWS_INPUT_MAX_LENGTH)
		end
		messages[#messages + 1] = message
	end

	if #messages == 0 then
		return nil, "Факт пустой."
	end

	return messages
end

local function are_message_lists_equal(a, b)
	if type(a) ~= "table" or type(b) ~= "table" then
		return false
	end
	if #a ~= #b then
		return false
	end
	for idx = 1, #a do
		if a[idx] ~= b[idx] then
			return false
		end
	end
	return true
end

function M.regenerate_capitals_fact_messages(entry)
	if not entry then
		return nil, "Нет данных для факта."
	end

	local previous = entry._cached_fact_messages
	entry._cached_fact_messages = nil

	local facts_count = type(entry.facts) == "table" and #entry.facts or 0
	local attempts = 1
	if previous and facts_count > 1 then
		attempts = math.min(8, facts_count * 2)
	end

	local latest_messages
	for _ = 1, attempts do
		local messages, err = build_capitals_fact_messages(entry)
		if not messages then
			entry._cached_fact_messages = previous
			return nil, err
		end
		latest_messages = messages
		if not previous or not are_message_lists_equal(messages, previous) then
			break
		end
	end

	entry._cached_fact_messages = latest_messages
	return latest_messages
end

function M.peek_capitals_fact_messages(entry)
	if not entry then
		return nil, "Нет данных для факта."
	end
	if entry._cached_fact_messages then
		return entry._cached_fact_messages
	end
	local messages, err = build_capitals_fact_messages(entry)
	if messages then
		entry._cached_fact_messages = messages
	end
	return messages, err
end

local function consume_capitals_fact_messages(entry)
	if not entry then
		return nil, "Нет данных для факта."
	end
	if entry._cached_fact_messages then
		local messages = entry._cached_fact_messages
		entry._cached_fact_messages = nil
		return messages
	end
	return build_capitals_fact_messages(entry)
end

-- === Broadcast helpers ===

local function broadcast_capitals_question(entry_or_country)
	local entry
	local country
	if type(entry_or_country) == "table" then
		entry = entry_or_country
		country = entry.country
	else
		country = entry_or_country
	end

	if type(country) ~= "string" or country == "" then
		ctx.update_status("Нет данных для вопроса о столице.")
		return false
	end

	local messages = {}
	local warning
	if entry then
		local fact_messages, err = consume_capitals_fact_messages(entry)
		if fact_messages then
			for _, msg in ipairs(fact_messages) do
				messages[#messages + 1] = msg
			end
		elseif err then
			warning = err
		end
	end

	local question = string.format("Страна: %s", country)
	local message, length = ctx.build_news_message_from_text(question)
	if not message then
		ctx.update_status("Вопрос слишком длинный: %d / %d.", length, ctx.NEWS_INPUT_MAX_LENGTH)
		return false
	end
	messages[#messages + 1] = message

	if ctx.get_selected_method() == 3 then
		ctx.update_status('Вопрос не отправлен: выбран режим "В пустоту".')
		return false
	end

	local ok = ctx.broadcast_sequence(messages, "capitals_question")
	if not ok then
		return false
	end
	ctx.start_sms_listener(true)
	if warning then
		ctx.update_status("Факт не отправлен: %s. Вопрос отправлен.", warning)
	end
	return true
end

local function send_capitals_fact(entry)
	if not entry then
		ctx.update_status("Нет данных для факта.")
		return false
	end

	local messages, err = build_capitals_fact_messages(entry)
	if not messages then
		if err then
			ctx.update_status(err)
		end
		return false
	end

	if ctx.get_selected_method() == 3 then
		ctx.update_status('Факт не отправлен: выбран режим "В пустоту".')
		return false
	end

	local ok = ctx.broadcast_sequence(messages, "capitals_fact")
	if not ok then
		return false
	end
	ctx.update_status("Факт отправлен в эфир.")
	return true
end

local function get_capitals_question_preview(entry_or_country)
	local entry
	local country
	if type(entry_or_country) == "table" then
		entry = entry_or_country
		country = entry.country
	else
		country = entry_or_country
	end
	if not country or country == "" then
		return nil
	end
	local question = string.format("Страна: %s", country)
	local question_message = ctx.build_news_message_from_text(question)
	local first_line = question_message or question
	if entry then
		local fact_messages = M.peek_capitals_fact_messages(entry)
		if fact_messages and fact_messages[1] then
			first_line = fact_messages[1]
		end
	end
	return first_line
end

-- === Game state ===

local function reset_capitals_buffers()
	local CapitalsQuiz = ctx.CapitalsQuiz
	imgui.StrCopy(CapitalsQuiz.player_id_buf, "")
	imgui.StrCopy(CapitalsQuiz.player_name_buf, "")
end

local function reset_capitals_scoreboard()
	local CapitalsQuiz = ctx.CapitalsQuiz
	CapitalsQuiz.players = {}
	ctx.mark_capitals_scoreboard_dirty()
	CapitalsQuiz.round = 0
	CapitalsQuiz.winner = nil
	CapitalsQuiz.current_country = nil
	CapitalsQuiz.current_capital = nil
	CapitalsQuiz.current_entry = nil
	CapitalsQuiz.current_answer_norms = nil
	CapitalsQuiz.round_answer = nil
	CapitalsQuiz.round_answer_norms = nil
	CapitalsQuiz.show_answer[0] = false
	CapitalsQuiz.answer_start_time = nil
	CapitalsQuiz.accepting_answers = false
	CapitalsQuiz.current_responses = {}
	CapitalsQuiz.first_correct = nil
	CapitalsQuiz.latest_round_stats = nil
	CapitalsQuiz.awaiting_next_round = false
	CapitalsQuiz.custom_error = nil
	CapitalsQuiz.last_entry = nil
	CapitalsQuiz.last_country = nil
	CapitalsQuiz.announced_countries = {}
	ctx.update_status("Игра сброшена. Сгенерируйте вопрос, чтобы начать раунд.")
	reset_capitals_buffers()
	ctx.Config:clearRuntimeState()
end

local function start_new_capitals_game()
	local CapitalsQuiz = ctx.CapitalsQuiz
	CapitalsQuiz.active = true
	reset_capitals_scoreboard()
	ctx.update_status("Игра началась. Цель - %d очка(ов).", CapitalsQuiz.target_scores[CapitalsQuiz.target_index])
end

local function end_capitals_game(winner)
	local CapitalsQuiz = ctx.CapitalsQuiz
	CapitalsQuiz.active = false
	CapitalsQuiz.winner = winner
	ctx.update_status(
		"%s достигает %d очков и побеждает!",
		winner,
		CapitalsQuiz.target_scores[CapitalsQuiz.target_index]
	)
	CapitalsQuiz.current_country = nil
	CapitalsQuiz.current_capital = nil
	CapitalsQuiz.current_entry = nil
	CapitalsQuiz.current_answer_norms = nil
	CapitalsQuiz.round_answer = nil
	CapitalsQuiz.round_answer_norms = nil
	CapitalsQuiz.answer_start_time = nil
	CapitalsQuiz.accepting_answers = false
	CapitalsQuiz.awaiting_next_round = false
	CapitalsQuiz.custom_error = nil
	CapitalsQuiz.announced_countries = {}
	ctx.Config:clearRuntimeState()
end

local function stop_capitals_game_manually()
	local CapitalsQuiz = ctx.CapitalsQuiz
	CapitalsQuiz.active = false
	ctx.update_status("Игра завершена вручную.")
	CapitalsQuiz.current_country = nil
	CapitalsQuiz.current_capital = nil
	CapitalsQuiz.current_entry = nil
	CapitalsQuiz.current_answer_norms = nil
	CapitalsQuiz.round_answer = nil
	CapitalsQuiz.round_answer_norms = nil
	CapitalsQuiz.answer_start_time = nil
	CapitalsQuiz.accepting_answers = false
	CapitalsQuiz.awaiting_next_round = false
	CapitalsQuiz.custom_error = nil
	CapitalsQuiz.announced_countries = {}
	ctx.Config:clearRuntimeState()
end

-- === Player management ===

local function ensure_capitals_player(name, player_id)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local normalized = ctx.normalize_player_name(name)
	if normalized == "" then
		return nil, normalized
	end
	local entry = CapitalsQuiz.players[normalized]
	if not entry then
		entry = { score = 0, last_answer = nil, last_correct = false }
		CapitalsQuiz.players[normalized] = entry
		ctx.mark_capitals_scoreboard_dirty()
	end
	if player_id ~= nil then
		local prev_id = entry.player_id
		entry.player_id = player_id
		if prev_id ~= player_id then
			ctx.mark_capitals_scoreboard_dirty()
		end
	end
	return entry, normalized
end

local function iterate_capitals_players_sorted()
	local scoreboard_cache = ctx.scoreboard_cache
	local cache = scoreboard_cache.capitals
	if not cache.dirty then
		return cache.list
	end
	local CapitalsQuiz = ctx.CapitalsQuiz
	local list = {}
	for name, data in pairs(CapitalsQuiz.players) do
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

local function update_capitals_player_last_answer(name, provided, is_correct, player_id)
	local entry, normalized = ensure_capitals_player(name, player_id)
	if not entry then
		return nil, normalized
	end
	entry.last_correct = is_correct and true or false
	entry.last_answer = provided
	ctx.mark_capitals_scoreboard_dirty()
	return entry, normalized
end

-- === Round management ===

local function start_capitals_round(entry)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local State = ctx.State
	if not entry then
		return false
	end
	entry._cached_fact_messages = nil
	CapitalsQuiz.current_country = entry.country
	CapitalsQuiz.current_capital = entry.capital
	CapitalsQuiz.current_entry = entry
	CapitalsQuiz.current_answer_norms = build_capital_variants(entry.capital)
	CapitalsQuiz.round_answer = entry.capital
	CapitalsQuiz.round_answer_norms = CapitalsQuiz.current_answer_norms
	CapitalsQuiz.show_answer[0] = false
	CapitalsQuiz.answer_start_time = os.clock()
	CapitalsQuiz.accepting_answers = true
	CapitalsQuiz.current_responses = {}
	CapitalsQuiz.first_correct = nil
	CapitalsQuiz.latest_round_stats = nil
	CapitalsQuiz.awaiting_next_round = false
	CapitalsQuiz.custom_error = nil
	State.sms_target_quiz = "capitals"
	ctx.update_status("Раунд %d: огласите страну и ждите ответы.", CapitalsQuiz.round + 1)
	reset_capitals_buffers()
	ctx.mark_runtime_save_dirty()
	return true
end

local function begin_capitals_round()
	local CapitalsQuiz = ctx.CapitalsQuiz
	local entry, err = pick_random_capitals_entry()
	if not entry then
		CapitalsQuiz.custom_error = err or "Не удалось выбрать страну."
		ctx.update_status(CapitalsQuiz.custom_error)
		return false
	end
	return start_capitals_round(entry)
end

-- === Answer handling ===

local function handle_capitals_correct_answer(player_name, player_id)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local entry, normalized = ensure_capitals_player(player_name, player_id)
	if not entry then
		return
	end
	player_name = normalized
	entry.score = entry.score + 1
	entry.last_correct = true
	entry.last_answer = CapitalsQuiz.current_capital
	ctx.mark_capitals_scoreboard_dirty()

	CapitalsQuiz.round = CapitalsQuiz.round + 1
	CapitalsQuiz.round_answer = CapitalsQuiz.round_answer or CapitalsQuiz.current_capital
	CapitalsQuiz.round_answer_norms = CapitalsQuiz.round_answer_norms or CapitalsQuiz.current_answer_norms
	CapitalsQuiz.last_entry = CapitalsQuiz.current_entry
	CapitalsQuiz.last_country = CapitalsQuiz.current_country
	CapitalsQuiz.current_country = nil
	CapitalsQuiz.current_capital = nil
	CapitalsQuiz.current_entry = nil
	CapitalsQuiz.current_answer_norms = nil
	CapitalsQuiz.show_answer[0] = false
	CapitalsQuiz.answer_start_time = nil
	CapitalsQuiz.accepting_answers = false

	local target = CapitalsQuiz.target_scores[CapitalsQuiz.target_index]
	if entry.score >= target then
		CapitalsQuiz.awaiting_next_round = false
		end_capitals_game(player_name)
	else
		CapitalsQuiz.awaiting_next_round = true
		ctx.update_status(
			"%s получает балл (%d/%d). Запустите следующий вопрос.",
			player_name,
			entry.score,
			target
		)
	end
end

function M._finalize_manual_capitals_round_winner(player_name, player_id)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local correct_text = CapitalsQuiz.current_capital or CapitalsQuiz.round_answer
	local correct_norms = CapitalsQuiz.current_answer_norms or CapitalsQuiz.round_answer_norms
	if not correct_text or correct_text == "" then
		return nil, "Не найден текущий ответ вопроса."
	end
	if not correct_norms or #correct_norms == 0 then
		correct_norms = build_capital_variants(correct_text)
	end

	local reference_response = ctx.find_response_for_player(CapitalsQuiz.current_responses, player_name, player_id, true)
	local response_time = reference_response and reference_response.response_time or nil
	local lead = reference_response and ctx.compute_lead_time(reference_response, CapitalsQuiz.current_responses) or nil

	handle_capitals_correct_answer(player_name, player_id)

	local player_entry, normalized_name = ensure_capitals_player(player_name, player_id)
	if not player_entry then
		return nil, "Не удалось обновить счёт игрока."
	end

	CapitalsQuiz.latest_round_stats = {
		winner = normalized_name,
		player_id = player_entry.player_id,
		response_time = response_time,
		lead = lead,
		total_responses = #CapitalsQuiz.current_responses,
		correct_answer = correct_text,
		correct_answer_norms = correct_norms,
		country = CapitalsQuiz.last_country or CapitalsQuiz.current_country,
		score = player_entry.score or 0,
		points_awarded = 1,
		game_finished = not CapitalsQuiz.active,
		manual = true,
	}
	if CapitalsQuiz.active then
		ctx.mark_runtime_save_dirty()
	else
		ctx.Config:clearRuntimeState()
	end

	return CapitalsQuiz.latest_round_stats
end

-- === SMS response recording ===

function M.record_response_from_sms(player_name, player_id, message)
	local CapitalsQuiz = ctx.CapitalsQuiz
	if not CapitalsQuiz.active then
		return
	end

	if not CapitalsQuiz.answer_start_time then
		return
	end

	if not CapitalsQuiz.accepting_answers then
		return
	end

	local correct_text = CapitalsQuiz.current_capital or CapitalsQuiz.round_answer
	local correct_norms = CapitalsQuiz.current_answer_norms or CapitalsQuiz.round_answer_norms
	if not correct_text and CapitalsQuiz.latest_round_stats then
		correct_text = CapitalsQuiz.latest_round_stats.correct_answer
		correct_norms = CapitalsQuiz.latest_round_stats.correct_answer_norms or correct_norms
	end

	if not correct_text or correct_text == "" then
		return
	end

	if not correct_norms or #correct_norms == 0 then
		correct_norms = build_capital_variants(correct_text)
	end

	local normalized_name = ctx.normalize_player_name(player_name)
	if normalized_name ~= "" then
		player_name = normalized_name
	else
		player_name = ctx.trim(player_name)
	end

	local response_time = os.clock() - CapitalsQuiz.answer_start_time
	if response_time < 0 then
		response_time = 0
	end

	local candidates = build_capitals_answer_candidates(message)
	local provided_text = candidates[1] or ""
	local matched_answer
	for _, candidate in ipairs(candidates) do
		if is_capital_answer_correct(candidate, correct_norms) then
			matched_answer = candidate
			break
		end
	end
	local display_answer = matched_answer or provided_text
	local is_correct = matched_answer ~= nil
	local entry = {
		name = player_name,
		player_id = player_id,
		text = message,
		answer_text = display_answer,
		response_time = response_time,
		correct = is_correct,
		outcome = "attempt",
	}
	table.insert(CapitalsQuiz.current_responses, entry)

	if is_correct and not CapitalsQuiz.first_correct then
		entry.outcome = "first"
		CapitalsQuiz.first_correct = entry
		local lead = ctx.compute_lead_time(entry, CapitalsQuiz.current_responses)
		CapitalsQuiz.latest_round_stats = {
			winner = player_name,
			player_id = player_id,
			response_time = entry.response_time,
			lead = lead,
			total_responses = #CapitalsQuiz.current_responses,
			correct_answer = correct_text,
			correct_answer_norms = correct_norms,
			country = CapitalsQuiz.current_country or CapitalsQuiz.last_country,
			score = 0,
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
		local provided = display_answer ~= "" and display_answer or "-"
		update_capitals_player_last_answer(player_name, provided, false, player_id)
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

local function announce_capitals_stats(gender)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local stats = CapitalsQuiz.latest_round_stats
	if not stats or not stats.winner then
		ctx.update_status("Нет данных для объявления ответа.")
		return
	end

	local normalized_gender = gender == "female" and "female" or "male"
	local has_winner = CapitalsQuiz.winner ~= nil
	local display_name = ctx.format_display_name(stats.winner, stats.player_id)
	local subject_label = has_winner and "победителе" or "ответе"
	local send_key = "capitals_announce_" .. normalized_gender

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

local function submit_capitals_answer_from_fields(gender)
	local CapitalsQuiz = ctx.CapitalsQuiz
	if not CapitalsQuiz.active then
		ctx.update_status("Игра не активна. Сначала начните раунд.")
		return
	end

	if not CapitalsQuiz.answer_start_time then
		ctx.update_status("Нет активного вопроса для проверки.")
		return
	end
	if not CapitalsQuiz.accepting_answers then
		ctx.update_status("Раунд уже закрыт. Запустите следующий вопрос.")
		return
	end

	local name, player_id, err = ctx.resolve_player_from_inputs(CapitalsQuiz.player_id_buf, CapitalsQuiz.player_name_buf)
	if err then
		ctx.update_status(err)
		return
	end

	local _, finalize_err = M._finalize_manual_capitals_round_winner(name, player_id)
	if finalize_err then
		ctx.update_status(finalize_err)
		return
	end

	announce_capitals_stats(gender)
	reset_capitals_buffers()
end

-- === Message building ===

function M._build_capitals_round_answer_message(gender)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local trim = ctx.trim
	local SMILive = ctx.SMILive
	local normalized_gender = gender == "female" and "female" or "male"
	local name, player_id, err = ctx.resolve_player_from_inputs(CapitalsQuiz.player_id_buf, CapitalsQuiz.player_name_buf)
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
	local entry = normalized_name ~= "" and CapitalsQuiz.players[normalized_name] or nil
	local current_score = type(entry) == "table" and tonumber(entry.score) or 0
	current_score = math.max(0, math.floor(current_score or 0))
	local projected_score = current_score
	if CapitalsQuiz.accepting_answers and CapitalsQuiz.answer_start_time then
		projected_score = projected_score + 1
	end
	if projected_score <= 0 then
		projected_score = 1
	end
	local score_phrase = ctx.format_score_progress(projected_score, 1, normalized_gender)
	local templates = SMILive._get_round_message_templates_for_gender(normalized_gender)
	local template = templates[math.random(1, #templates)] or SMILive._get_default_round_message_template(normalized_gender)
	local ok, formatted = pcall(string.format, template, display_name, score_phrase, projected_score)
	if not ok or type(formatted) ~= "string" or trim(formatted) == "" then
		formatted = string.format("%s. %s", display_name, score_phrase)
	end
	return formatted, nil
end

function M._build_capitals_winner_message(gender)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local trim = ctx.trim
	local SMILive = ctx.SMILive
	local normalized_gender = gender == "female" and "female" or "male"
	local winner_name = trim(tostring(CapitalsQuiz.winner or ""))
	if winner_name == "" then
		return nil, "Победитель ещё не определён."
	end

	local normalized_winner = ctx.normalize_player_name(winner_name)
	if normalized_winner == "" then
		normalized_winner = winner_name
	end

	local entry = CapitalsQuiz.players and CapitalsQuiz.players[normalized_winner] or nil
	local player_id = type(entry) == "table" and entry.player_id or nil
	local score = type(entry) == "table" and tonumber(entry.score) or nil
	local stats = CapitalsQuiz.latest_round_stats
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
		local target_score = tonumber(CapitalsQuiz.target_scores[CapitalsQuiz.target_index]) or 1
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

local function draw_capitals_scoreboard_table(height)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local escape_imgui_text = ctx.escape_imgui_text
	if
		imgui.BeginChild(
			"capitals_quiz_scoreboard",
			imgui.ImVec2(0, height),
			true,
			imgui.WindowFlags.HorizontalScrollbar
		)
	then
		imgui.Columns(3, "capitals_quiz_scoreboard_cols", true)
		imgui.Text("Игрок")
		imgui.NextColumn()
		imgui.Text("Очки")
		imgui.NextColumn()
		imgui.Text("Последний ответ")
		imgui.NextColumn()
		imgui.Separator()

		local has_rows = false
		for _, row in ipairs(iterate_capitals_players_sorted()) do
			has_rows = true
			imgui.Text(escape_imgui_text(ctx.format_player_label(row.name, row.player_id)))
			if CapitalsQuiz.winner and CapitalsQuiz.winner == row.name then
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

local function draw_capitals_responses_table(height)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local escape_imgui_text = ctx.escape_imgui_text
	if
		imgui.BeginChild(
			"capitals_quiz_responses",
			imgui.ImVec2(0, height),
			true,
			imgui.WindowFlags.HorizontalScrollbar
		)
	then
		imgui.Columns(4, "capitals_quiz_responses_cols", true)
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
		for _, resp in ipairs(CapitalsQuiz.current_responses) do
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

local function draw_capitals_latest_round_stats()
	local CapitalsQuiz = ctx.CapitalsQuiz
	local escape_imgui_text = ctx.escape_imgui_text
	local stats = CapitalsQuiz.latest_round_stats
	if not stats or not stats.winner then
		return
	end

	local winner = stats.winner or "-"
	local player_id = stats.player_id and tostring(stats.player_id) or "-"
	local response_time = stats.response_time and ctx.format_seconds(stats.response_time) or "-"
	local total_responses = stats.total_responses or 0
	local country = stats.country or "-"
	local summary
	if stats.points_awarded and stats.points_awarded > 0 then
		summary = string.format(
			"Страна: %s | Победитель раунда: %s[%s] - %s | Всего ответов: %d",
			country,
			winner,
			player_id,
			response_time,
			total_responses
		)
	else
		summary = string.format(
			"Страна: %s | Рекомендация (первый верный): %s[%s] - %s | Всего ответов: %d",
			country,
			winner,
			player_id,
			response_time,
			total_responses
		)
	end
	imgui.Text(escape_imgui_text(summary))
end

local function draw_capitals_tables_section()
	local CapitalsQuiz = ctx.CapitalsQuiz
	local trim = ctx.trim
	local NEWS_PREFIX = ctx.NEWS_PREFIX

	imgui.Text("Таблица очков")
	draw_capitals_scoreboard_table(SCOREBOARD_HEIGHT)

	imgui.Spacing()
	imgui.Text("Ответы текущего раунда")
	draw_capitals_responses_table(RESPONSES_HEIGHT)

	imgui.Spacing()
	draw_capitals_latest_round_stats()

	imgui.Spacing()
	imgui.PushItemWidth(70)
	if imgui.InputText("ID##capitals_player_id", CapitalsQuiz.player_id_buf, 8) then
		local parsed_id = ctx.parse_player_id_from_buf(CapitalsQuiz.player_id_buf)
		if parsed_id then
			ctx.try_fill_name_from_id(CapitalsQuiz.player_name_buf, parsed_id)
		end
	end
	imgui.PopItemWidth()

	imgui.SameLine()
	imgui.PushItemWidth(240)
	if imgui.InputText("Ник игрока##capitals_name", CapitalsQuiz.player_name_buf, 48) then
		local name = trim(str(CapitalsQuiz.player_name_buf))
		if name ~= "" then
			ctx.try_fill_id_from_name(CapitalsQuiz.player_id_buf, name)
		end
	end
	imgui.PopItemWidth()

	local has_manual_target = trim(str(CapitalsQuiz.player_id_buf)) ~= "" or trim(str(CapitalsQuiz.player_name_buf)) ~= ""
	if not has_manual_target then
		local alpha = imgui.GetStyle().Alpha
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha * 0.5)
	end
	if imgui.Button("Остановить прием сообщений##capitals_stop_listen") and has_manual_target then
		if not CapitalsQuiz.active then
			ctx.update_status("Игра не активна. Сначала начните раунд.")
		elseif not CapitalsQuiz.answer_start_time then
			ctx.update_status("Нет активного вопроса для проверки.")
		elseif not CapitalsQuiz.accepting_answers then
			ctx.update_status("Раунд уже закрыт. Запустите следующий вопрос.")
		else
			local name, player_id, err = ctx.resolve_player_from_inputs(CapitalsQuiz.player_id_buf, CapitalsQuiz.player_name_buf)
			if err then
				ctx.update_status(err)
			else
				local stats, finalize_err = M._finalize_manual_capitals_round_winner(name, player_id)
				if finalize_err then
					ctx.update_status(finalize_err)
				else
					local display_name = ctx.format_display_name(stats and stats.winner or name, stats and stats.player_id or player_id)
					local stop_message = string.format("%s %s", NEWS_PREFIX, "Стоп!")
					local sent = ctx.broadcast_sequence({ stop_message }, "capitals_stop_listen")
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
				end
			end
		end
	end
	if not has_manual_target then
		imgui.PopStyleVar()
	end

	imgui.SameLine()
	if CapitalsQuiz.active then
		if imgui.Button("Сбросить игру##capitals_reset_game") then
			start_new_capitals_game()
		end
	else
		if imgui.Button("Сбросить таблицу##capitals_reset_table") then
			reset_capitals_scoreboard()
		end
	end
end

-- === Main draw function ===

function M.DrawCapitalsQuiz(show_tables)
	local CapitalsQuiz = ctx.CapitalsQuiz
	local SMILive = ctx.SMILive
	local trim = ctx.trim
	local escape_imgui_text = ctx.escape_imgui_text
	local mimgui_funcs = ctx.mimgui_funcs
	local push_button_palette = ctx.push_button_palette
	local pop_button_palette = ctx.pop_button_palette
	local COLOR_ACCENT_PRIMARY = ctx.COLOR_ACCENT_PRIMARY
	local COLOR_ACCENT_SUCCESS = ctx.COLOR_ACCENT_SUCCESS
	local COLOR_ACCENT_DANGER = ctx.COLOR_ACCENT_DANGER
	local news_popup = ctx.news_popup_mod

	imgui.TextWrapped(escape_imgui_text(CapitalsQuiz.status_text or ""))
	imgui.Dummy(imgui.ImVec2(0, 4))

	if CapitalsQuiz.data_error then
		imgui.TextColored(imgui.ImVec4(1.0, 0.6, 0.4, 1), escape_imgui_text(CapitalsQuiz.data_error))
	end

	if not CapitalsQuiz.active then
		for idx, target in ipairs(CapitalsQuiz.target_scores) do
			if idx > 1 then
				imgui.SameLine()
			end
			imgui.PushIDInt(1000 + idx)
			if imgui.RadioButtonBool(string.format("%d очка", target), CapitalsQuiz.target_index == idx) then
				CapitalsQuiz.target_index = idx
				ctx.Config:save()
			end
			imgui.PopID()
		end

		imgui.SameLine()
		push_button_palette(COLOR_ACCENT_SUCCESS)
		if imgui.Button("Начать игру##capitals_start") then
			start_new_capitals_game()
		end
		pop_button_palette()
	else
		if CapitalsQuiz.awaiting_next_round then
			push_button_palette(COLOR_ACCENT_PRIMARY)
			if imgui.Button("Следующая страна##capitals_next") then
				begin_capitals_round()
			end
			pop_button_palette()
		else
			push_button_palette(COLOR_ACCENT_PRIMARY)
			if imgui.Button("Сгенерировать страну##capitals_generate") then
				begin_capitals_round()
			end
			pop_button_palette()

			if CapitalsQuiz.current_entry then
				imgui.SameLine()
				push_button_palette(COLOR_ACCENT_PRIMARY)
				if imgui.Button("Перегенерировать факт##capitals_regenerate_fact") then
					local _, err = M.regenerate_capitals_fact_messages(CapitalsQuiz.current_entry)
					if err then
						ctx.update_status(err)
					else
						ctx.update_status("Факт перегенерирован.")
					end
				end
				local fact_messages = M.peek_capitals_fact_messages(CapitalsQuiz.current_entry)
				local first_line = fact_messages and fact_messages[1] or nil
				local tooltip = ctx.combine_send_tooltip("Сгенерировать другой факт для текущей страны.", first_line)
				mimgui_funcs.imgui_hover_tooltip_safe(tooltip)
				pop_button_palette()
			end
		end

		imgui.SameLine()
		push_button_palette(COLOR_ACCENT_DANGER)
		if imgui.Button("Завершить игру##capitals_stop") then
			stop_capitals_game_manually()
		end
		pop_button_palette()

		imgui.Dummy(imgui.ImVec2(0, 4))
		imgui.Separator()

		if CapitalsQuiz.current_country then
			imgui.Text(
				escape_imgui_text(string.format("Текущая страна: %s", CapitalsQuiz.current_country))
			)
			if CapitalsQuiz.current_capital then
				imgui.SameLine()
				imgui.TextColored(
					imgui.ImVec4(0.4, 1.0, 0.4, 1),
					escape_imgui_text(string.format("= %s", CapitalsQuiz.current_capital))
				)
			end
		end
	end

	if CapitalsQuiz.custom_error then
		imgui.TextColored(imgui.ImVec4(1.0, 0.4, 0.4, 1), escape_imgui_text(CapitalsQuiz.custom_error))
	end

	if CapitalsQuiz.current_country then
		if imgui.Button("Отправить вопрос в чат##capitals_send") then
			local payload = CapitalsQuiz.current_entry or CapitalsQuiz.current_country
			local sent_country = type(payload) == "table" and payload.country or payload
			if is_capitals_country_announced(sent_country) then
				ctx.update_status(
					"Страна \"%s\" уже была озвучена в этой игре. Сгенерируйте следующую.",
					tostring(sent_country or "?")
				)
			elseif broadcast_capitals_question(payload) then
				if mark_capitals_country_announced(sent_country) then
					ctx.mark_runtime_save_dirty()
				end
			end
		end
		local preview_line = get_capitals_question_preview(CapitalsQuiz.current_entry or CapitalsQuiz.current_country)
		local tooltip = ctx.combine_send_tooltip("Отправить вопрос в чат.", preview_line)
		mimgui_funcs.imgui_hover_tooltip_safe(tooltip)
		imgui.SameLine()
		news_popup._draw_news_popup_editor_button({
			popup_scope_key = news_popup._make_news_popup_scope_key("capitals", "question"),
			popup_id = "news_popup_capitals_question",
			section_name = "Вопрос о столице",
			small_button_label = "..",
			build_messages_fn = function()
				local payload = CapitalsQuiz.current_entry or CapitalsQuiz.current_country
				local entry = type(payload) == "table" and payload or nil
				local country = entry and entry.country or payload
				if type(country) ~= "string" or country == "" then
					return nil
				end

				local messages = {}
				if entry then
					local fact_messages = M.peek_capitals_fact_messages(entry)
					if type(fact_messages) == "table" then
						for _, msg in ipairs(fact_messages) do
							messages[#messages + 1] = msg
						end
					end
				end

				local question = string.format("Страна: %s", country)
				local question_message = ctx.build_news_message_from_text(question)
				if question_message then
					messages[#messages + 1] = question_message
				end

				if #messages == 0 then
					return nil
				end
				return messages
			end,
			send_key = "capitals_question",
			source_meta = function()
				return {
					quiz_kind = "capitals",
					action = "question",
				}
			end,
		})
		local fact_entry = CapitalsQuiz.last_entry or CapitalsQuiz.current_entry
		if fact_entry then
			imgui.SameLine()
			local announce_size = imgui.ImVec2(165, 0)
			push_button_palette(COLOR_ACCENT_PRIMARY)
			if imgui.Button("Факт о стране##capitals_fact", announce_size) then
				send_capitals_fact(fact_entry)
			end
			local fact_messages = M.peek_capitals_fact_messages(fact_entry)
			local first_line = fact_messages and fact_messages[1] or nil
			local tooltip2 = ctx.combine_send_tooltip("Отправить случайный факт о стране.", first_line)
			mimgui_funcs.imgui_hover_tooltip_safe(tooltip2)
			imgui.SameLine()
			news_popup._draw_news_popup_editor_button({
				popup_scope_key = news_popup._make_news_popup_scope_key("capitals", "fact_current"),
				popup_id = "news_popup_capitals_fact_current",
				section_name = "Факт о стране (текущий)",
				small_button_label = "..",
				build_messages_fn = function()
					local messages = M.peek_capitals_fact_messages(fact_entry)
					if type(messages) ~= "table" or #messages == 0 then
						return nil
					end
					return messages
				end,
				send_key = "capitals_fact_current",
				source_meta = function()
					return {
						quiz_kind = "capitals",
						action = "fact_current",
					}
				end,
			})
			pop_button_palette()
		end
	end

	if not CapitalsQuiz.current_country and CapitalsQuiz.last_entry then
		local fact_entry = CapitalsQuiz.last_entry
		push_button_palette(COLOR_ACCENT_PRIMARY)
		if imgui.Button("Факт о стране##capitals_fact_last", imgui.ImVec2(165, 0)) then
			send_capitals_fact(fact_entry)
		end
		local fact_messages = M.peek_capitals_fact_messages(fact_entry)
		local first_line = fact_messages and fact_messages[1] or nil
		local tooltip = ctx.combine_send_tooltip("Отправить случайный факт о стране.", first_line)
		mimgui_funcs.imgui_hover_tooltip_safe(tooltip)
		imgui.SameLine()
		news_popup._draw_news_popup_editor_button({
			popup_scope_key = news_popup._make_news_popup_scope_key("capitals", "fact_last"),
			popup_id = "news_popup_capitals_fact_last",
			section_name = "Факт о стране (последний)",
			small_button_label = "..",
			build_messages_fn = function()
				local messages = M.peek_capitals_fact_messages(fact_entry)
				if type(messages) ~= "table" or #messages == 0 then
					return nil
				end
				return messages
			end,
			send_key = "capitals_fact_last",
			source_meta = function()
				return {
					quiz_kind = "capitals",
					action = "fact_last",
				}
			end,
		})
		pop_button_palette()
	end

	if show_tables ~= false then
		draw_capitals_tables_section()
	end
end

-- Export for external use
M.reset_capitals_buffers = reset_capitals_buffers

return M
