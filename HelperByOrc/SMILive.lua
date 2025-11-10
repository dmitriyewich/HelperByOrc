local SMILive = {}

local imgui = require("mimgui")
local new = imgui.new
local ffi = require("ffi")
local str = ffi.string

local binder
local tags_module
local start_sms_listener
local stop_sms_listener
local funcs

local math_random = math.random
local os_clock = os.clock

local NEWS_PREFIX = "/news"

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
                if not currentLiveSection or data.EventFlag ~= INPUTTEXT_CALLBACK_RESIZE then
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

local function send_sequence(messages, method, interval)
        if type(messages) ~= "table" or #messages == 0 then
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
        local function worker()
                for idx, msg in ipairs(messages) do
                        if type(msg) == "string" and msg ~= "" then
                                send_fn(msg, target)
                        end
                        if idx < #messages and delay > 0 then
                                wait(delay)
                        end
                end
        end
        if lua_thread and lua_thread.create then
                lua_thread.create(worker)
        else
                worker()
        end
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
                string.format("%s И так, следующий пример...", NEWS_PREFIX),
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
end

local function ensure_player(name)
        local normalized = normalize_player_name(name)
        if normalized == "" then
                return nil, normalized
        end
        MathQuiz.players[normalized] = MathQuiz.players[normalized] or { score = 0, last_answer = nil, last_correct = false }
        return MathQuiz.players[normalized], normalized
end

local function begin_round()
        local problem, answer = generate_math_problem()
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
        update_status("Раунд %d: огласите пример и ждите ответы.", MathQuiz.round + 1)
        reset_buffers()
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


function SMILive.DrawMathQuiz()
        imgui.TextColored(imgui.ImVec4(0.9, 0.75, 0.2, 1), "Эфир-викторина \"Математика\"")
        imgui.Separator()

        if not MathQuiz.active then
                imgui.Text("Выберите цель по очкам:")
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
                if imgui.Button("Начать новую игру") then
                        start_new_game()
                end
                if MathQuiz.winner then
                        imgui.TextColored(imgui.ImVec4(0.6, 1.0, 0.6, 1), format_status("Прошлый победитель: %s", MathQuiz.winner))
                end
        else
                imgui.Text(string.format("Цель: %d очка", MathQuiz.target_scores[MathQuiz.target_index]))
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
                end
        end

        if MathQuiz.current_problem then
                imgui.Spacing()
                imgui.Text(string.format("Текущий пример: %s", MathQuiz.current_problem))
                imgui.SameLine()
                imgui.Checkbox("Показать ответ", MathQuiz.show_answer)
                if MathQuiz.show_answer[0] and MathQuiz.current_answer then
                        imgui.SameLine()
                        imgui.TextColored(imgui.ImVec4(0.4, 1.0, 0.4, 1), string.format("= %s", tostring(MathQuiz.current_answer)))
                end
        end

        imgui.Spacing()
        imgui.TextWrapped(MathQuiz.status_text)
        imgui.Spacing()

        if MathQuiz.latest_round_stats then
                local stats = MathQuiz.latest_round_stats
                imgui.TextColored(imgui.ImVec4(0.7, 0.9, 1.0, 1), format_status("Первый верный ответ: %s[%s] - %s", stats.winner, stats.player_id or "-", format_seconds(stats.response_time)))
                if stats.lead then
                        imgui.Text(format_status("Преимущество по времени: %s", format_seconds(stats.lead)))
                else
                        imgui.Text("Преимущество по времени: -")
                end
                if stats.total_responses then
                        imgui.Text(format_status("Всего ответов: %d", stats.total_responses))
                end
                if stats.correct_answer ~= nil then
                        imgui.Text(format_status("Правильный ответ: %s", tostring(stats.correct_answer)))
                end
                imgui.Spacing()
        end

        imgui.Separator()
        imgui.Text("Объявления /news")

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

        if MathQuiz.current_problem then
                if imgui.Button("Отправить текущий пример в чат") then
                        broadcast_problem(MathQuiz.current_problem)
                end
        end
        if MathQuiz.latest_round_stats and MathQuiz.latest_round_stats.winner then
                if MathQuiz.current_problem then
                        imgui.SameLine()
                end
                if imgui.Button("Объявить правильный ответ в чат") then
                        local stats = MathQuiz.latest_round_stats
                        broadcast_correct_answer(stats.winner, stats.correct_answer, stats.score, stats.game_finished, stats.player_id)
                end
        end
        imgui.Spacing()

        if MathQuiz.active and MathQuiz.current_problem then
                imgui.InputText("Ник игрока", MathQuiz.player_name_buf, 48)
                imgui.InputText("Ответ", MathQuiz.player_answer_buf, 32, imgui.InputTextFlags.CharsDecimal)
                if imgui.Button("Проверить ответ") then
                        local name = normalize_player_name(str(MathQuiz.player_name_buf))
                        local answer_str = str(MathQuiz.player_answer_buf)
                        local provided = tonumber(answer_str)
                        if name == "" then
                                update_status("Введите ник игрока.")
                        elseif not provided then
                                update_status("Введите числовой ответ.")
                        else
                                if MathQuiz.current_answer and provided == MathQuiz.current_answer then
                                        handle_correct_answer(name)
                                else
                                        handle_wrong_answer(name, provided)
                                end
                                reset_buffers()
                        end
                end
        end

        imgui.Spacing()
        if has_players() then
                imgui.Separator()
                imgui.Text("Таблица очков")
                imgui.Columns(3, "math_quiz_scoreboard", true)
                imgui.Text("Игрок")
                imgui.NextColumn()
                imgui.Text("Очки")
                imgui.NextColumn()
                imgui.Text("Последний ответ")
                imgui.NextColumn()
                imgui.Separator()
                for _, row in ipairs(iterate_players_sorted()) do
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
        end

        if #MathQuiz.current_responses > 0 then
                imgui.Separator()
                imgui.Text("Ответы текущего раунда")
                imgui.Columns(4, "math_quiz_responses", true)
                imgui.Text("Игрок")
                imgui.NextColumn()
                imgui.Text("ID")
                imgui.NextColumn()
                imgui.Text("Ответ")
                imgui.NextColumn()
                imgui.Text("Время")
                imgui.NextColumn()
                imgui.Separator()
                for _, resp in ipairs(MathQuiz.current_responses) do
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
        end

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

local LiveWindow = {
        show = new.bool(false),
}

local function draw_live_broadcast_controls()
        imgui.Text("Управление эфиром")
        imgui.Spacing()

        local intro_changed = update_live_buffer_from_imgui(LiveBroadcast.intro, "Вступление##live_intro_text", 80)
        if intro_changed then
                Config:save()
        end
        if imgui.Button("Начать эфир") then
                send_live_sequence_from_section(LiveBroadcast.intro, "Вступление")
        end

        imgui.Spacing()

        local outro_changed = update_live_buffer_from_imgui(LiveBroadcast.outro, "Завершение##live_outro_text", 80)
        if outro_changed then
                Config:save()
        end
        if imgui.Button("Закончить эфир") then
                send_live_sequence_from_section(LiveBroadcast.outro, "Завершение эфира")
        end

        imgui.Spacing()

        local reminder_changed = update_live_buffer_from_imgui(LiveBroadcast.reminder, "Напоминание##live_reminder_text", 80)
        if reminder_changed then
                Config:save()
        end
        if imgui.Button("Напоминание") then
                send_live_sequence_from_section(LiveBroadcast.reminder, "Напоминание")
        end
end

local function draw_sms_listener_controls()
        imgui.Text("Приём SMS-сообщений")
        imgui.Spacing()

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

        if imgui.Button(button_label) then
                if controls_available or sms_listener_active then
                        action()
                else
                        update_status("Модуль my_hooks не поддерживает приём SMS-сообщений.")
                end
        end

        imgui.SameLine()
        local status_color = sms_listener_active and imgui.ImVec4(0.4, 1.0, 0.4, 1) or imgui.ImVec4(1.0, 0.6, 0.4, 1)
        local status_label = sms_listener_active and "приём активен" or "приём отключён"
        imgui.TextColored(status_color, status_label)

        if not controls_available then
                imgui.Spacing()
                imgui.TextColored(imgui.ImVec4(0.9, 0.6, 0.4, 1), "Управление доступно после загрузки модуля my_hooks.")
        end
end

local function draw_live_window()
        imgui.TextWrapped("Окно SMI Live помогает вести эфир-викторину и контролировать ход раундов.")
        imgui.Spacing()
        imgui.Separator()
        draw_live_broadcast_controls()
        imgui.Spacing()
        imgui.Separator()
        draw_sms_listener_controls()
        imgui.Spacing()
        imgui.Separator()
        SMILive.DrawMathQuiz()
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

        if resume_listener then
                start_sms_listener(true)
        end
end

return SMILive
