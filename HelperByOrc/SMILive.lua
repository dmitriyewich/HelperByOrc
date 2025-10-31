local SMILive = {}

local imgui = require("mimgui")
local new = imgui.new
local ffi = require("ffi")
local str = ffi.string

local math_random = math.random

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

local function generate_math_problem()
        local a = math_random(2, 25)
        local b = math_random(2, 25)
        local ops = {
                {"+", function(x, y)
                        return x + y
                end},
                {"-", function(x, y)
                        return x - y
                end},
                {"×", function(x, y)
                        return x * y
                end},
        }
        local op = ops[math_random(1, #ops)]
        local symbol, fn = op[1], op[2]
        if symbol == "-" and a < b then
                a, b = b, a
        end
        local answer = fn(a, b)
        local text = string.format("%d %s %d", a, symbol, b)
        return text, answer
end

local MathQuiz = {
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
}

local function reset_buffers()
        imgui.StrCopy(MathQuiz.player_name_buf, "")
        imgui.StrCopy(MathQuiz.player_answer_buf, "")
end

local function update_status(text, ...)
        MathQuiz.status_text = format_status(text, ...)
end

local function reset_scoreboard()
        MathQuiz.players = {}
        MathQuiz.round = 0
        MathQuiz.winner = nil
        MathQuiz.current_problem = nil
        MathQuiz.current_answer = nil
        MathQuiz.show_answer[0] = false
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
end

local function ensure_player(name)
        MathQuiz.players[name] = MathQuiz.players[name] or { score = 0, last_answer = nil, last_correct = false }
        return MathQuiz.players[name]
end

local function begin_round()
        local problem, answer = generate_math_problem()
        MathQuiz.current_problem = problem
        MathQuiz.current_answer = answer
        MathQuiz.round = MathQuiz.round + 1
        MathQuiz.show_answer[0] = false
        update_status("Раунд %d: огласите пример и ждите ответы.", MathQuiz.round)
        reset_buffers()
end

local function handle_correct_answer(player_name)
        local entry = ensure_player(player_name)
        entry.score = entry.score + 1
        entry.last_correct = true
        entry.last_answer = MathQuiz.current_answer

        local target = MathQuiz.target_scores[MathQuiz.target_index]
        if entry.score >= target then
                end_game(player_name)
        else
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
                        begin_round()
                end
                if MathQuiz.current_problem then
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
                                        MathQuiz.current_problem = nil
                                        MathQuiz.current_answer = nil
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

local function draw_live_window()
        imgui.TextWrapped("Окно SMI Live помогает вести эфир-викторину и контролировать ход раундов.")
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
        -- зарезервировано для будущей интеграции
end

return SMILive
