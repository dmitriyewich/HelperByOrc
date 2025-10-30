local SMILive = {}

local imgui = require("mimgui")
local new = imgui.new
local ffi = require("ffi")
local str = ffi.string

local math_random = math.random
local math_floor = math.floor
local math_min = math.min
local math_max = math.max
local os_clock = os.clock
local os_date = os.date
local os_time = os.time

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
                        local name = str(MathQuiz.player_name_buf)
                        name = trim(name)
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
        if next(MathQuiz.players) then
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

local buffer_constructors = {
        [24] = function()
                return new.char[24]()
        end,
        [48] = function()
                return new.char[48]()
        end,
        [64] = function()
                return new.char[64]()
        end,
        [160] = function()
                return new.char[160]()
        end,
        [200] = function()
                return new.char[200]()
        end,
        [256] = function()
                return new.char[256]()
        end,
}

local function make_buffer(len, value)
        local ctor = buffer_constructors[len]
        if not ctor then
                error("Unsupported buffer length: " .. tostring(len))
        end
        local holder = { buf = ctor(), size = len }
        imgui.StrCopy(holder.buf, value or "")
        return holder
end

local STATUS_INFO = {
        { name = "Подготовка", color = imgui.ImVec4(0.75, 0.75, 0.75, 1) },
        { name = "В эфире", color = imgui.ImVec4(0.4, 0.9, 0.5, 1) },
        { name = "Пауза", color = imgui.ImVec4(0.95, 0.7, 0.35, 1) },
        { name = "Завершён", color = imgui.ImVec4(0.7, 0.7, 0.7, 1) },
}

local LiveWindow = {
        show = new.bool(false),
        segments = {},
        selected_index = 0,
        new_title = make_buffer(64),
        new_host = make_buffer(48),
        new_time = make_buffer(24),
        new_note = make_buffer(160),
        custom_message = make_buffer(256),
        templates = {
                "В эфире: %s",
                "Готовим к эфиру: %s",
                "Сегмент \"%s\" завершён. Спасибо, что были с нами!",
                "%s — старт через минуту!",
        },
        template_idx = 1,
        event_log = {},
        log_limit = 80,
        feedback = "",
        feedback_color = imgui.ImVec4(0.75, 0.75, 0.75, 1),
        feedback_clock = 0,
}

local function log_event(message)
        message = trim(message or "")
        if message == "" then
                return
        end
        table.insert(LiveWindow.event_log, 1, { time = os_date("%H:%M:%S"), text = message })
        while #LiveWindow.event_log > LiveWindow.log_limit do
                table.remove(LiveWindow.event_log)
        end
end

local function set_feedback(text, kind)
        LiveWindow.feedback = text or ""
        LiveWindow.feedback_clock = os_clock()
        if kind == "error" then
                LiveWindow.feedback_color = imgui.ImVec4(1.0, 0.45, 0.45, 1)
        elseif kind == "success" then
                LiveWindow.feedback_color = imgui.ImVec4(0.55, 0.85, 0.55, 1)
        else
                LiveWindow.feedback_color = imgui.ImVec4(0.75, 0.75, 0.75, 1)
        end
end

local function create_segment(title, host, time_slot, note)
        local seg = {
                status = 1,
                title = make_buffer(64, title),
                host = make_buffer(48, host),
                time = make_buffer(24, time_slot),
                notes = make_buffer(256, note),
                created_at = os_time(),
                last_status_change = os_time(),
        }
        return seg
end

local function get_segment_title(seg)
        if not seg then
                return ""
        end
        return trim(str(seg.title.buf))
end

local function get_segment_host(seg)
        if not seg then
                return ""
        end
        return trim(str(seg.host.buf))
end

local function get_segment_time(seg)
        if not seg then
                return ""
        end
        return trim(str(seg.time.buf))
end

local function get_segment_notes(seg)
        if not seg then
                return ""
        end
        return trim(str(seg.notes.buf))
end

local function ensure_selection()
        local count = #LiveWindow.segments
        if count == 0 then
                LiveWindow.selected_index = 0
                return
        end
        if LiveWindow.selected_index < 1 then
                LiveWindow.selected_index = 1
        elseif LiveWindow.selected_index > count then
                LiveWindow.selected_index = count
        end
end

local function build_segment_summary(seg, idx)
        local lines = {}
        local title = get_segment_title(seg)
        if title == "" then
                title = format_status("Сегмент %d", idx)
        end
        table.insert(lines, format_status("Сегмент: %s", title))
        local host = get_segment_host(seg)
        if host ~= "" then
                table.insert(lines, format_status("Ведущий: %s", host))
        end
        local time_slot = get_segment_time(seg)
        if time_slot ~= "" then
                table.insert(lines, format_status("Слот: %s", time_slot))
        end
        local info = STATUS_INFO[seg.status] or STATUS_INFO[1]
        if info then
                table.insert(lines, format_status("Статус: %s", info.name))
        end
        local note = get_segment_notes(seg)
        if note ~= "" then
                table.insert(lines, format_status("Заметки: %s", note))
        end
        return table.concat(lines, "\n")
end

local function add_segment()
        local title = trim(str(LiveWindow.new_title.buf))
        local host = trim(str(LiveWindow.new_host.buf))
        local time_slot = trim(str(LiveWindow.new_time.buf))
        local note = trim(str(LiveWindow.new_note.buf))
        if title == "" then
                set_feedback("Введите название сегмента.", "error")
                return
        end
        local seg = create_segment(title, host, time_slot, note)
        table.insert(LiveWindow.segments, seg)
        LiveWindow.selected_index = #LiveWindow.segments
        ensure_selection()
        log_event(format_status("Добавлен сегмент \"%s\".", title))
        set_feedback("Сегмент добавлен в сетку эфира.", "success")
        imgui.StrCopy(LiveWindow.new_title.buf, "")
        imgui.StrCopy(LiveWindow.new_host.buf, "")
        imgui.StrCopy(LiveWindow.new_time.buf, "")
        imgui.StrCopy(LiveWindow.new_note.buf, "")
end

local function remove_segment(idx)
        local seg = LiveWindow.segments[idx]
        if not seg then
                return false
        end
        local title = get_segment_title(seg)
        if title == "" then
                title = format_status("Сегмент %d", idx)
        end
        table.remove(LiveWindow.segments, idx)
        ensure_selection()
        log_event(format_status("Удалён сегмент \"%s\".", title))
        set_feedback("Сегмент удалён из сетки.", "info")
        return true
end

local function reorder_segment(idx, delta)
        local new_idx = idx + delta
        if new_idx < 1 or new_idx > #LiveWindow.segments then
                return
        end
        LiveWindow.segments[idx], LiveWindow.segments[new_idx] = LiveWindow.segments[new_idx], LiveWindow.segments[idx]
        LiveWindow.selected_index = new_idx
        local seg = LiveWindow.segments[new_idx]
        local title = get_segment_title(seg)
        if title == "" then
                title = format_status("Сегмент %d", new_idx)
        end
        log_event(format_status("Сегмент \"%s\" перемещён на позицию %d.", title, new_idx))
        set_feedback("Порядок сегментов обновлён.", "info")
end

local function set_segment_status(idx, status)
        local seg = LiveWindow.segments[idx]
        if not seg or seg.status == status then
                return
        end
        seg.status = status
        seg.last_status_change = os_time()
        local title = get_segment_title(seg)
        if title == "" then
                title = format_status("Сегмент %d", idx)
        end
        local info = STATUS_INFO[status] or STATUS_INFO[1]
        log_event(format_status("Статус \"%s\": %s.", title, info.name))
        set_feedback(format_status("Статус обновлён: %s.", info.name), "success")
end

local function apply_template_to_message(template_idx)
        local template = LiveWindow.templates[template_idx]
        if not template then
                return
        end
        local placeholder = "эфир"
        local seg = LiveWindow.segments[LiveWindow.selected_index]
        if seg then
                local title = get_segment_title(seg)
                if title ~= "" then
                        placeholder = title
                end
        end
        local message = format_status(template, placeholder)
        imgui.StrCopy(LiveWindow.custom_message.buf, message)
        set_feedback("Шаблон подставлен в черновик.", "info")
end

local function add_custom_message()
        local message = trim(str(LiveWindow.custom_message.buf))
        if message == "" then
                set_feedback("Введите текст сообщения перед добавлением.", "error")
                return
        end
        log_event(message)
        set_feedback("Сообщение добавлено в журнал.", "success")
        imgui.StrCopy(LiveWindow.custom_message.buf, "")
end

local function draw_segments_list()
        imgui.Text("Новый сегмент")
        imgui.InputText("Название", LiveWindow.new_title.buf, LiveWindow.new_title.size)
        imgui.InputText("Ведущий", LiveWindow.new_host.buf, LiveWindow.new_host.size)
        imgui.InputText("Время/слот", LiveWindow.new_time.buf, LiveWindow.new_time.size)
        imgui.InputTextMultiline("Комментарий", LiveWindow.new_note.buf, LiveWindow.new_note.size, imgui.ImVec2(0, 60))
        if imgui.Button("Добавить сегмент") then
                add_segment()
        end
        imgui.Spacing()
        imgui.Separator()
        imgui.Text("Сетка эфира")
        imgui.BeginChild("live_segments_scroll", imgui.ImVec2(0, 0), true)
        if #LiveWindow.segments == 0 then
                imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Список пуст. Добавьте сегмент выше.")
        else
                local i = 1
                while i <= #LiveWindow.segments do
                        local seg = LiveWindow.segments[i]
                        imgui.PushID(i)
                        local display = get_segment_title(seg)
                        if display == "" then
                                display = format_status("Сегмент %d", i)
                        end
                        if imgui.Selectable(display .. "##live_segment_select", LiveWindow.selected_index == i) then
                                LiveWindow.selected_index = i
                        end
                        local status_info = STATUS_INFO[seg.status] or STATUS_INFO[1]
                        imgui.PushStyleColor(imgui.Col.Text, status_info.color)
                        imgui.Text(status_info.name)
                        imgui.PopStyleColor()
                        local list_changed = false
                        imgui.SameLine()
                        if imgui.SmallButton("▲##live_seg_up") then
                                reorder_segment(i, -1)
                                list_changed = true
                        end
                        imgui.SameLine()
                        if imgui.SmallButton("▼##live_seg_down") then
                                reorder_segment(i, 1)
                                list_changed = true
                        end
                        if list_changed then
                                imgui.PopID()
                                break
                        end
                        imgui.SameLine()
                        local removed = false
                        if imgui.SmallButton("✖##live_seg_remove") then
                                removed = remove_segment(i)
                        end
                        if not removed then
                                local host = get_segment_host(seg)
                                local time_slot = get_segment_time(seg)
                                if host ~= "" or time_slot ~= "" then
                                        imgui.PushStyleColor(imgui.Col.Text, imgui.ImVec4(0.75, 0.75, 0.75, 1))
                                        local meta = {}
                                        if host ~= "" then
                                                table.insert(meta, format_status("Ведущий: %s", host))
                                        end
                                        if time_slot ~= "" then
                                                table.insert(meta, time_slot)
                                        end
                                        imgui.Text(table.concat(meta, " • "))
                                        imgui.PopStyleColor()
                                end
                                local note = get_segment_notes(seg)
                                if note ~= "" then
                                        imgui.PushStyleColor(imgui.Col.Text, imgui.ImVec4(0.65, 0.65, 0.65, 1))
                                        imgui.TextWrapped(note)
                                        imgui.PopStyleColor()
                                end
                                imgui.Separator()
                                imgui.PopID()
                                i = i + 1
                        else
                                imgui.PopID()
                        end
                        if removed then
                                -- не увеличиваем индекс, чтобы проверить новый элемент на этой позиции
                        end
                end
        end
        imgui.EndChild()
end

local function draw_segment_details()
        ensure_selection()
        local idx = LiveWindow.selected_index
        local seg = LiveWindow.segments[idx]
        if not seg then
                imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Выберите сегмент слева, чтобы увидеть детали.")
                return
        end
        local title = get_segment_title(seg)
        if title == "" then
                title = format_status("Сегмент %d", idx)
        end
        local status_info = STATUS_INFO[seg.status] or STATUS_INFO[1]
        imgui.TextColored(status_info.color, format_status("«%s»", title))
        imgui.TextColored(imgui.ImVec4(0.65, 0.65, 0.65, 1), format_status("Создан: %s", os_date("%d.%m %H:%M", seg.created_at)))
        if imgui.BeginCombo("Статус", status_info.name) then
                for status_idx, info in ipairs(STATUS_INFO) do
                        local sel = (seg.status == status_idx)
                        if imgui.Selectable(info.name, sel) then
                                set_segment_status(idx, status_idx)
                        end
                end
                imgui.EndCombo()
        end
        imgui.InputText("Название", seg.title.buf, seg.title.size)
        imgui.InputText("Ведущий", seg.host.buf, seg.host.size)
        imgui.InputText("Время/слот", seg.time.buf, seg.time.size)
        imgui.InputTextMultiline("Заметки", seg.notes.buf, seg.notes.size, imgui.ImVec2(0, 100))
        if imgui.Button("Скопировать карточку") then
                local summary = build_segment_summary(seg, idx)
                imgui.SetClipboardText(summary)
                set_feedback("Карточка скопирована в буфер обмена.", "success")
        end
        imgui.SameLine()
        if imgui.Button("Удалить сегмент##live_detail_remove") then
                remove_segment(idx)
        end
end

local function draw_log_section()
        imgui.Text("Журнал эфира")
        imgui.BeginChild("live_log_scroll", imgui.ImVec2(0, 140), true)
        if #LiveWindow.event_log == 0 then
                imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Журнал пуст. Добавьте сообщения через черновик.")
        else
                imgui.Columns(3, "live_log_columns", false)
                imgui.SetColumnWidth(0, 80)
                imgui.SetColumnWidth(2, 70)
                imgui.TextColored(imgui.ImVec4(0.6, 0.6, 0.6, 1), "Время")
                imgui.NextColumn()
                imgui.Text("Сообщение")
                imgui.NextColumn()
                imgui.Text("Копия")
                imgui.NextColumn()
                imgui.Separator()
                for idx, entry in ipairs(LiveWindow.event_log) do
                        imgui.PushID(idx)
                        imgui.TextColored(imgui.ImVec4(0.8, 0.8, 0.8, 1), entry.time)
                        imgui.NextColumn()
                        imgui.TextWrapped(entry.text)
                        imgui.NextColumn()
                        if imgui.SmallButton("Копия##live_log_copy") then
                                imgui.SetClipboardText(entry.text or "")
                                set_feedback("Сообщение скопировано в буфер обмена.", "success")
                        end
                        imgui.NextColumn()
                        imgui.Separator()
                        imgui.PopID()
                end
                imgui.Columns(1)
        end
        imgui.EndChild()
        if #LiveWindow.event_log > 0 then
                if imgui.Button("Очистить журнал") then
                        LiveWindow.event_log = {}
                        set_feedback("Журнал очищен.", "info")
                end
        end
end

local function draw_message_tools()
        imgui.Text("Сообщения и заметки")
        local current_template = LiveWindow.templates[LiveWindow.template_idx] or LiveWindow.templates[1] or ""
        if imgui.BeginCombo("Шаблон сообщения", current_template) then
                for idx, template in ipairs(LiveWindow.templates) do
                        if imgui.Selectable(template, LiveWindow.template_idx == idx) then
                                LiveWindow.template_idx = idx
                        end
                end
                imgui.EndCombo()
        end
        imgui.SameLine()
        if imgui.Button("Подставить шаблон") then
                apply_template_to_message(LiveWindow.template_idx)
        end
        imgui.InputTextMultiline("Черновик", LiveWindow.custom_message.buf, LiveWindow.custom_message.size, imgui.ImVec2(0, 80))
        if imgui.Button("Добавить в журнал") then
                add_custom_message()
        end
        imgui.SameLine()
        if imgui.Button("Скопировать в буфер") then
                local text = trim(str(LiveWindow.custom_message.buf))
                if text == "" then
                        set_feedback("Сообщение пустое — нечего копировать.", "error")
                else
                        imgui.SetClipboardText(text)
                        set_feedback("Сообщение скопировано в буфер обмена.", "success")
                end
        end
        imgui.Spacing()
        draw_log_section()
end

local function draw_right_panel()
        imgui.Text("Карточка сегмента")
        imgui.Separator()
        draw_segment_details()
        imgui.Spacing()
        imgui.Separator()
        draw_message_tools()
        imgui.Spacing()
        imgui.Separator()
        imgui.PushID("live_math_window")
        if imgui.CollapsingHeader("Интерактив \"Математика\"##live_window") then
                SMILive.DrawMathQuiz()
        end
        imgui.PopID()
end

local function draw_live_window()
        ensure_selection()
        if LiveWindow.feedback ~= "" then
                if LiveWindow.feedback_clock > 0 and (os_clock() - LiveWindow.feedback_clock) > 8 then
                        LiveWindow.feedback = ""
                else
                        imgui.TextColored(LiveWindow.feedback_color, LiveWindow.feedback)
                        imgui.Spacing()
                end
        end
        local avail = imgui.GetContentRegionAvail()
        local left_width = math_floor(math_max(260, avail.x * 0.38))
        if left_width > (avail.x - 200) then
                left_width = math_floor(math_max(220, avail.x * 0.55))
        end
        if left_width < 220 then
                left_width = 220
        end
        if left_width > avail.x then
                left_width = avail.x
        end
        imgui.BeginChild("live_left_panel", imgui.ImVec2(left_width, 0), true)
        draw_segments_list()
        imgui.EndChild()
        imgui.SameLine()
        imgui.BeginChild("live_right_panel", imgui.ImVec2(0, 0), false)
        draw_right_panel()
        imgui.EndChild()
end

function SMILive.DrawHelperSection()
        if not imgui.CollapsingHeader("SMI Live") then
                return
        end

        imgui.TextWrapped("Инструменты для организации прямых эфиров: планирование сегментов, заметки и журнал сообщений.")
        if imgui.Button("Открыть окно SMI Live") then
                LiveWindow.show[0] = true
        end
        if #LiveWindow.segments > 0 then
                imgui.Spacing()
                imgui.Text("Ближайшие сегменты:")
                local preview = math_min(#LiveWindow.segments, 3)
                for idx = 1, preview do
                        local seg = LiveWindow.segments[idx]
                        local title = get_segment_title(seg)
                        if title == "" then
                                title = format_status("Сегмент %d", idx)
                        end
                        local info = STATUS_INFO[seg.status] or STATUS_INFO[1]
                        imgui.PushStyleColor(imgui.Col.Text, info.color)
                        imgui.Text(format_status("%d. %s — %s", idx, title, info.name))
                        imgui.PopStyleColor()
                end
                if #LiveWindow.segments > preview then
                        imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), format_status("…ещё %d сегмент(ов)", #LiveWindow.segments - preview))
                end
        else
                imgui.Spacing()
                imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1), "Сетка эфира пока пуста.")
        end
        imgui.Spacing()
        imgui.Separator()
        imgui.PushID("smi_live_inline_math")
        SMILive.DrawMathQuiz()
        imgui.PopID()
end

imgui.OnFrame(function()
        return LiveWindow.show[0]
end, function()
        imgui.SetNextWindowSize(imgui.ImVec2(720, 520), imgui.Cond.FirstUseEver)
        local opened = imgui.Begin("SMI Live — эфирный помощник", LiveWindow.show, imgui.WindowFlags.NoCollapse)
        if opened then
                draw_live_window()
        end
        imgui.End()
end)

function SMILive.attachModules(modules)
        -- зарезервировано для будущей интеграции
end

return SMILive
