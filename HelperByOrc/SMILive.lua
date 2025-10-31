local SMILive = {}

local imgui = require("mimgui")
local new = imgui.new
local ffi = require("ffi")
local str = ffi.string

local binder = require("HelperByOrc.binder")

local MathQuiz = {}

local encoding = require "encoding"
encoding.default = "CP1251"
local u8 = encoding.UTF8

local math_random = math.random
local os_clock = os.clock

local send_labels = {"В чат", "Клиенту", "Серверу", "В пустоту"}
local send_labels_ffi = imgui.new["const char*"][#send_labels](send_labels)
local NEWS_PREFIX = "/news"

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
        if method < 0 then
                method = 0
        elseif method > 3 then
                method = 3
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

local function send_chat_message(text, method)
        if type(text) ~= "string" or text == "" then
                return
        end
        local target = method or get_selected_method()
        local send_fn = binder and binder.doSend
        if type(send_fn) == "function" then
                send_fn(text, target)
                return
        end

        local decoded = u8:decode(text)
        if target == 0 then
                sampAddChatMessage(decoded, 0x00DD00)
        elseif target == 1 then
                sampProcessChatInput(decoded)
        elseif target == 2 then
                sampSendChat(decoded)
        end
end

local function send_sequence(messages, method, interval)
        if type(messages) ~= "table" or #messages == 0 then
                return
        end
        local delay = math.max(0, tonumber(interval) or 0)
        delay = math.floor(delay + 0.5)
        local target = method or get_selected_method()
        local function worker()
                for idx, msg in ipairs(messages) do
                        send_chat_message(msg, target)
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

local function should_auto_broadcast()
        return MathQuiz.auto_broadcast and true or false
end

local function broadcast_sequence(messages, force)
        if not force then
                if not should_auto_broadcast() then
                        return
                end
                if get_selected_method() == 3 then
                        return
                end
        end
        send_sequence(messages, get_selected_method(), get_interval_ms())
end

local function broadcast_problem(problem, force)
        if type(problem) ~= "string" or problem == "" then
                return
        end
        local messages = {
                string.format("%s И так, следующий пример...", NEWS_PREFIX),
                string.format("%s *%s*", NEWS_PREFIX, problem)
        }
        broadcast_sequence(messages, force)
end

local function broadcast_correct_answer(player_name, answer, score, is_final, force)
        if type(player_name) ~= "string" or player_name == "" then
                return
        end
        local answer_text = answer ~= nil and tostring(answer) or "—"
        local gained = 1
        local score_phrase = format_score_progress(score or 0, gained)
        local messages = {
                string.format("%s Стоп!", NEWS_PREFIX),
                string.format("%s У нас есть правильный ответ! И это: *%s*", NEWS_PREFIX, answer_text),
                string.format("%s Верный ответ прислал..", NEWS_PREFIX),
                string.format("%s *%s*! %s", NEWS_PREFIX, player_name, score_phrase)
        }
        if is_final then
                messages[#messages + 1] = string.format(
                        "%s Викторина завершена! %s набирает %s и побеждает!",
                        NEWS_PREFIX,
                        player_name,
                        pluralize_points(score or 0)
                )
        end
        broadcast_sequence(messages, force)
end

local function format_status(fmt, ...)
        local ok, msg = pcall(string.format, fmt, ...)
        if ok then
                return msg
        end
        return fmt
end

local function trim(s)
        return (s or ""):gsub("^%s*(.-)%s*$", "%1")
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
                return "—"
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
        local ops = {"+", "-", "×", "/"}
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
        elseif op == "×" then
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

MathQuiz = {
        target_scores = { 3, 5 },
        target_index = 1,
        active = false,
        round = 0,
        current_problem = nil,
        current_answer = nil,
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
        auto_broadcast = true,
        chat_method = 2,
        chat_interval_ms = 750,
}

local update_status
local handle_server_sms

local sms_listener_active = false
local my_hooks_module = nil

local function can_use_sms_listener()
        return my_hooks_module
                and type(my_hooks_module.addServerMessageListener) == "function"
                and type(my_hooks_module.removeServerMessageListener) == "function"
end

local function start_sms_listener(silent)
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

local function stop_sms_listener(silent)
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

local function reset_buffers()
        imgui.StrCopy(MathQuiz.player_name_buf, "")
        imgui.StrCopy(MathQuiz.player_answer_buf, "")
end

update_status = function(text, ...)
        MathQuiz.status_text = format_status(text, ...)
end

local function reset_scoreboard()
        MathQuiz.players = {}
        MathQuiz.round = 0
        MathQuiz.winner = nil
        MathQuiz.current_problem = nil
        MathQuiz.current_answer = nil
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
        update_status("Игра началась. Цель — %d очка(ов).", MathQuiz.target_scores[MathQuiz.target_index])
end

local function end_game(winner)
        MathQuiz.active = false
        MathQuiz.winner = winner
        update_status("%s достигает %d очков и побеждает!", winner, MathQuiz.target_scores[MathQuiz.target_index])
        MathQuiz.current_problem = nil
        MathQuiz.current_answer = nil
        MathQuiz.answer_start_time = nil
        MathQuiz.accepting_answers = false
        MathQuiz.awaiting_next_round = false
end

local function ensure_player(name)
        MathQuiz.players[name] = MathQuiz.players[name] or { score = 0, last_answer = nil, last_correct = false }
        return MathQuiz.players[name]
end

local function begin_round()
        local problem, answer = generate_math_problem()
        MathQuiz.current_problem = problem
        MathQuiz.current_answer = answer
        MathQuiz.show_answer[0] = false
        MathQuiz.answer_start_time = os_clock()
        MathQuiz.accepting_answers = true
        MathQuiz.current_responses = {}
        MathQuiz.first_correct = nil
        MathQuiz.latest_round_stats = nil
        MathQuiz.awaiting_next_round = false
        update_status("Раунд %d: огласите пример и ждите ответы.", MathQuiz.round + 1)
        broadcast_problem(problem, false)
        reset_buffers()
end

local function handle_correct_answer(player_name)
        local entry = ensure_player(player_name)
        entry.score = entry.score + 1
        entry.last_correct = true
        entry.last_answer = MathQuiz.current_answer

        MathQuiz.round = MathQuiz.round + 1
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
                update_status("%s получает балл (%d/%d). Запустите следующий пример.", player_name, entry.score, target)
        end
end

local function handle_wrong_answer(player_name, provided)
        local entry = ensure_player(player_name)
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
        local entry = ensure_player(name)
        entry.last_correct = is_correct and true or false
        entry.last_answer = provided
        return entry
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
        if not (MathQuiz.active and MathQuiz.current_answer and MathQuiz.answer_start_time and MathQuiz.accepting_answers) then
                return
        end

        local response_time = os_clock() - MathQuiz.answer_start_time
        if response_time < 0 then
                response_time = 0
        end

        local numeric_answer = extract_numeric_answer(message)
        local is_correct = numeric_answer ~= nil and MathQuiz.current_answer == numeric_answer
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
                local correct_value = MathQuiz.current_answer
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

                broadcast_correct_answer(player_name, correct_value, player_entry and player_entry.score or 0, MathQuiz.latest_round_stats.game_finished, false)

                return
        end

        if is_correct then
                entry.outcome = "late"
                update_player_last_answer(player_name, MathQuiz.current_answer, false)
                update_status("%s прислал верный ответ через %.2f с, но уже после завершения раунда.", player_name, entry.response_time)
        else
                entry.outcome = "wrong"
                local provided = message ~= "" and message or "—"
                update_player_last_answer(player_name, provided, false)
                update_status("%s отвечает через %.2f с: %s (неверно).", player_name, entry.response_time, provided)
        end
end

handle_server_sms = function(color, text)
        local name, player_id, message = parse_sms_message(text)
        if not name then
                return
        end
        record_response_from_sms(name, player_id, message)
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
                imgui.TextColored(imgui.ImVec4(0.7, 0.9, 1.0, 1), format_status("Первый верный ответ: %s[%s] — %s", stats.winner, stats.player_id or "—", format_seconds(stats.response_time)))
                if stats.lead then
                        imgui.Text(format_status("Преимущество по времени: %s", format_seconds(stats.lead)))
                else
                        imgui.Text("Преимущество по времени: —")
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
        local auto_flag = imgui.new.bool(MathQuiz.auto_broadcast and true or false)
        if imgui.Checkbox("Автоматически отправлять сообщения", auto_flag) then
                MathQuiz.auto_broadcast = auto_flag[0]
        end

        imgui.PushItemWidth(200)
        local method_buf = ffi.new("int[1]", MathQuiz.chat_method)
        if imgui.Combo("Метод отправки", method_buf, send_labels_ffi, #send_labels) then
                MathQuiz.chat_method = method_buf[0]
        end
        imgui.PopItemWidth()

        imgui.PushItemWidth(200)
        local interval_buf = ffi.new("int[1]", MathQuiz.chat_interval_ms)
        if imgui.InputInt("Интервал между сообщениями (мс)", interval_buf) then
                MathQuiz.chat_interval_ms = math.max(0, interval_buf[0])
        end
        imgui.PopItemWidth()

        if MathQuiz.current_problem then
                if imgui.Button("Отправить текущий пример в чат") then
                        broadcast_problem(MathQuiz.current_problem, true)
                end
        end
        if MathQuiz.latest_round_stats and MathQuiz.latest_round_stats.winner then
                if MathQuiz.current_problem then
                        imgui.SameLine()
                end
                if imgui.Button("Объявить правильный ответ в чат") then
                        local stats = MathQuiz.latest_round_stats
                        broadcast_correct_answer(stats.winner, stats.correct_answer, stats.score, stats.game_finished, true)
                end
        end
        imgui.Spacing()

        if MathQuiz.active and MathQuiz.current_problem then
                imgui.InputText("Ник игрока", MathQuiz.player_name_buf, 48)
                imgui.InputText("Ответ", MathQuiz.player_answer_buf, 32, imgui.InputTextFlags.CharsDecimal)
                if imgui.Button("Проверить ответ") then
                        local name = trim(str(MathQuiz.player_name_buf))
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
                                imgui.Text("—")
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
                        imgui.Text(resp.player_id and tostring(resp.player_id) or "—")
                        imgui.NextColumn()
                        local display_answer = resp.text ~= "" and resp.text or "—"
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
                                imgui.Text("—")
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
                        table.insert(preview, format_status("%s — %d", row.name, row.score))
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
        local opened = imgui.Begin("SMI Live — эфир-викторина", LiveWindow.show, imgui.WindowFlags.NoCollapse)
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

        my_hooks_module = modules and modules.my_hooks or nil

        if resume_listener then
                start_sms_listener(true)
        end
end

return SMILive
