-- HelperByOrc/weapon_rp.lua
local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
encoding.default = "CP1251"
local u8 = encoding.UTF8

local funcs = require('HelperByOrc.funcs')
local mimgui_funcs
local has_sprites = false

-- ===== базовая конфигурация =====
M.config = {
  -- детектор
  tick_ms = 80,
  stable_need = 3,
  cooldown_frames = 10,

  -- режимы
  auto_mode = 0,              -- 0 = авто /me, 1 = по ПКМ
  change_as_two_lines = false,-- смена всегда в одну строку (одна /me)
  ignore_knuckles = true,

  -- вывод
  prefix = "/me",
  min_me_gap_ms = 1000,
  max_len = 96,

  -- стиль фраз
  flavor_level = 2,           -- 1 сдержанно, 2 поярче, 3 разнообразнее

  -- общий набор наречий и соединителей (для всех оружий)
  adverbs_show = { "уверенно", "быстрым движением", "плавно", "ловко", "чётко" },
  adverbs_hide = { "аккуратно", "спокойно", "без лишнего шума", "коротким движением", "бережно" },
  connectors_full  = { ", затем ", "; после чего ", "; и тут же ", ", не теряя времени, " },
  connectors_short = { ", затем " },

  -- отправитель (nil -> sampSendChat)
  sender = nil,

  -- единая таблица оружий
  weapons = {
    -- ближний бой
    [1]  = { name = "кастет",          short = "кастет" },
    [2]  = { name = "клюшку для гольфа",short = "клюшку",     from = "со спины",    to = "за спину" },
    [3]  = { name = "дубинку",         short = "дубинку",    from = "со спины",    to = "за спину" },
    [4]  = { name = "нож",             short = "нож",        from = "из-за пояса", to = "за пояс" },
    [5]  = { name = "бейсбольную биту",short = "биту",       from = "со спины",    to = "за спину" },
    [6]  = { name = "лопату",          short = "лопату",     from = "со спины",    to = "за спину" },
    [7]  = { name = "кий",             short = "кий",        from = "со спины",    to = "за спину" },
    [8]  = { name = "катану",          short = "катану",     from = "со спины",    to = "за спину" },
    [9]  = { name = "бензопилу",       short = "бензопилу",  from = "со спины",    to = "за спину" },
    [10] = { name = "фиолетовый дилдо",short = "дилдо" },
    [11] = { name = "дилдо",           short = "дилдо" },
    [12] = { name = "вибратор",        short = "вибратор" },
    [13] = { name = "серебристый вибратор", short = "вибратор" },
    [14] = { name = "цветы",           short = "цветы" },
    [15] = { name = "трость",          short = "трость" },

    -- взрывчатка и гранаты
    [16] = { name = "гранату",         short = "гранату" },
    [17] = { name = "газовую гранату", short = "газовую гранату" },
    [18] = { name = "коктейль Молотова", short = "Молотов" },
    [39] = { name = "взрывчатку",      short = "сатчел",     from = "из сумки",    to = "в сумку" },
    [40] = { name = "детонатор",       short = "детонатор" },

    -- пистолеты
    [22] = { name = "пистолет 9mm",    short = "9mm",        from = "из кобуры",   to = "в кобуру" },
    [23] = { name = "пистолет с глушителем", short = "9mm с глушителем",   from = "из кобуры",   to = "в кобуру" },
    [24] = { name = "пистолет Desert Eagle", short = "Desert Eagle",from = "из кобуры",   to = "в кобуру" },

    -- дробовики
    [25] = { name = "дробовик",        short = "дробовик",   from = "со спины",    to = "за спину" },
    [26] = { name = "обрез",           short = "обрез",      from = "со спины",    to = "за спину" },
    [27] = { name = "боевой дробовик", short = "SPAS-12",     from = "со спины",    to = "за спину" },

    -- пистолеты-пулемёты
    [28] = { name = "Micro Uzi",       short = "Uzi",        from = "с плеча",     to = "на плечо" },
    [29] = { name = "MP5",             short = "MP5",        from = "с плеча",     to = "на плечо" },
    [32] = { name = "Tec-9",           short = "Tec-9",      from = "с плеча",     to = "на плечо" },

    -- автоматы и винтовки
    [30] = { name = "AK-47",           short = "AK-47",      from = "со спины",    to = "за спину" },
    [31] = { name = "M4",              short = "M4",         from = "со спины",    to = "за спину" },
    [33] = { name = "винтовку",        short = "винтовку",   from = "со спины",    to = "за спину" },
    [34] = { name = "снайперскую винтовку", short = "винтовку",from = "со спины",    to = "за спину" },

    -- тяжёлое
    [35] = { name = "РПГ",             short = "РПГ",        from = "со спины",    to = "за спину" },
    [36] = { name = "наводящийся РПГ", short = "РПГ",        from = "со спины",    to = "за спину" },
    [37] = { name = "огнемёт",         short = "огнемёт",    from = "со спины",    to = "за спину" },
    [38] = { name = "миниган",         short = "миниган",    from = "со спины",    to = "за спину" },

    -- утилиты
    [41] = { name = "баллончик с краской", short = "краску", from = "из рюкзака",  to = "в рюкзак" },
    [42] = { name = "огнетушитель",    short = "огнетушитель", from = "из рюкзака", to = "в рюкзак" },
    [43] = { name = "фотоаппарат",     short = "камеру",     from = "из рюкзака",  to = "в рюкзак" },
    [44] = { name = "прибор ночного видения", short = "ПНВ", from = "из рюкзака",  to = "в рюкзак" },
    [45] = { name = "тепловизор",      short = "тепловизор", from = "из рюкзака",  to = "в рюкзак" },
    [46] = { name = "парашют",         short = "парашют",    from = "из рюкзака",  to = "в рюкзак" }
  }
}

local CONFIG_PATH = "moonloader/HelperByOrc/weapon_rp.json"

-- ===== внутренняя служебка =====

-- дефолт для неизвестного оружия
local DEFAULT_WEAPON = {
  enable = true,
  name   = nil,   -- будет "оружие %d"
  short  = nil,   -- будет "оружие %d"
  from   = "из кармана",
  to     = "в карман",
  verbs  = { show = {"достал"}, hide = {"убрал"} }
}

local function normalize_weapon(id, w)
  w = w or {}
  local r = {}
  r.enable = (w.enable ~= false)
  r.name   = w.name   or ("оружие %d"):format(id)
  r.short  = w.short  or r.name
  r.from   = w.from   or DEFAULT_WEAPON.from
  r.to     = w.to     or DEFAULT_WEAPON.to
  r.verbs  = r.verbs or {}
  r.verbs.show = (w.verbs and w.verbs.show) or funcs.deepcopy(DEFAULT_WEAPON.verbs.show)
  r.verbs.hide = (w.verbs and w.verbs.hide) or funcs.deepcopy(DEFAULT_WEAPON.verbs.hide)
  return r
end


-- ===== загрузка/сохранение =====
local function load_cfg()
  local tbl = funcs.loadTableFromJson(CONFIG_PATH)
  if type(tbl) ~= "table" then tbl = {} end
  -- скопировать в M.config c нормализацией
  for k,v in pairs(M.config) do
    if tbl[k] ~= nil then
      if type(v) == "table" then
        M.config[k] = funcs.deepcopy(tbl[k])
      else
        M.config[k] = tbl[k]
      end
    end
  end
  -- нормализовать оружия
  local out = {}
  for id, w in pairs(M.config.weapons or {}) do
    out[tonumber(id) or id] = normalize_weapon(tonumber(id) or id, w)
  end
  M.config.weapons = out
end

local function save_cfg()
  -- сохраняем как есть
  funcs.saveTableToJson(M.config, CONFIG_PATH)
end

M.save = save_cfg
M.reload = load_cfg

load_cfg()
M._cfg_loaded = true

-- ===== события/детектор =====
local running, thr = false, nil
local prev_weapon, candidate_weapon, stable_count, cooldown = -1, -1, 0, 0
local pending_old, pending_new
local cb_any, cb_show, cb_hide, cb_change

function M.attachModules(mod)
  if mod.funcs then funcs = mod.funcs end
  mimgui_funcs = mod.mimgui_funcs
  has_sprites = type(mimgui_funcs) == "table" and mimgui_funcs.drawWeaponZoom ~= nil
  if not M._cfg_loaded then
    load_cfg()
    M._cfg_loaded = true
  end
end

function M.onAny(fn)    cb_any = fn end
function M.onShow(fn)   cb_show = fn end
function M.onHide(fn)   cb_hide = fn end
function M.onChange(fn) cb_change = fn end

-- ===== очередь /me с паузой =====
local send_queue, send_thr = {}, nil
local function ensure_send_thread()
  if send_thr and send_thr:status() ~= "dead" then return end
  send_thr = lua_thread.create(function()
    while true do
      if #send_queue > 0 then
        local s = table.remove(send_queue, 1)
        s = u8:decode(s)
        if s and s ~= "" then
          if type(M.config.sender) == "function" then
            pcall(M.config.sender, s)
          elseif sampSendChat then
            sampSendChat(s)
          end
          wait(M.config.min_me_gap_ms)
        else
          wait(50)
        end
      else
        wait(50)
      end
    end
  end)
end

local function send_chat(line)
  if not line or line == "" then return end
  ensure_send_thread()
  table.insert(send_queue, line)
end

-- ===== утилиты генерации =====
local function str_len(s)
  s = u8:decode(s)
  local prefix = M.config.prefix or "/me"
  prefix = prefix:gsub("(%W)", "%%%1")
  s = s:gsub("^%s*" .. prefix .. "%s*", "")
  return #s
end

local function pick(t, salt, fixed_by_level)
  if type(t) ~= "table" or #t == 0 then return nil end
  if fixed_by_level then
    -- flavor_level 1 — берём первый
    return t[1]
  end
  local x = math.floor((os.clock()*1000 + (salt or 0))) % #t + 1
  return t[x]
end

local function is_empty_weapon(id)
  if id == -1 or id == 0 then return true end
  if id == 1 and M.config.ignore_knuckles then return true end
  return false
end

local function winfo(id)
  local w = M.config.weapons[id]
  if not w then
    w = normalize_weapon(id, nil)
    M.config.weapons[id] = w
  end
  return w
end

-- постройка базовых частей
local function build_part(kind, id, opts)
  local w = winfo(id)
  if w.enable == false then return nil end
  local verbs = (kind=="show") and w.verbs.show or w.verbs.hide
  local verb  = pick(verbs, id*7 + (kind=="show" and 1 or 2), opts.plain) or (kind=="show" and "достал" or "убрал")
  local advs  = (kind=="show") and M.config.adverbs_show or M.config.adverbs_hide
  local adv   = (opts.use_adv and M.config.flavor_level >= 2) and (pick(advs, id*11) or "") or ""
  if adv ~= "" then adv = adv.." " end
  local place = (kind=="show") and w.from or w.to
  if opts.no_place then place = "" end
  local name  = (opts.short_names and w.short) or w.name
  if place ~= "" then
    return ("%s %s%s %s %s"):format(M.config.prefix, adv, verb, place, name)
  else
    return ("%s %s%s %s"):format(M.config.prefix, adv, verb, name)
  end
end

local function join_changed(hide_line, show_line, connector)
  local s1 = hide_line:gsub("^/me%s*", "")
  local s2 = show_line:gsub("^/me%s*", "")
  return ("%s %s%s%s"):format(M.config.prefix, s1, connector, s2)
end

-- попытки ужатия строки
local function try_build(kind, newId, oldId, level)
  local opts = {
    use_adv     = (level <= 1),
    plain       = (level >= 2),
    short_names = (level >= 3),
    no_place    = (level >= 4),
  }
  local conns = (level >= 1) and M.config.connectors_short or M.config.connectors_full
  local connector = pick(conns, (newId or 0)+(oldId or 0), M.config.flavor_level==1) or ", затем "

  if kind == "shown" then
    return build_part("show", newId, opts)
  elseif kind == "hidden" then
    return build_part("hide", oldId, opts)
  else
    local h = build_part("hide", oldId, opts)
    local s = build_part("show", newId, opts)
    if h and s then return join_changed(h, s, connector) end
    return s or h
  end
end

function M.makeRpLine(kind, newId, oldId)
  -- уровни: 0 максимум украшений → 4 максимально компактно
  local line
  for lvl=0,4 do
    line = try_build(kind, newId, oldId, lvl)
    if line and str_len(line) <= M.config.max_len then return line end
  end
  -- край: отрез по слову + многоточие
  if line and str_len(line) > M.config.max_len then
    local s = line
    while str_len(s) > M.config.max_len - 1 do
      s = s:match("^(.*)%s+[^%s]+$") or s:sub(1, M.config.max_len - 1)
    end
    return s.."…"
  end
  return line
end

-- авто-RP/ПКМ
local function auto_rp(kind, newW, oldW)
  if M.config.auto_mode == 0 then
    local line = M.makeRpLine(kind, newW, oldW)
    if line and line ~= "" then send_chat(line) end
  else
    if not pending_old then pending_old = oldW end
    pending_new = newW
  end
end

local function fire(kind, newW, oldW)
  if type(cb_any) == "function" then pcall(cb_any, kind, newW, oldW) end
  if kind == "shown"  and type(cb_show)   == "function" then pcall(cb_show, newW, oldW)
  elseif kind == "hidden" and type(cb_hide)== "function" then pcall(cb_hide, newW, oldW)
  elseif kind == "changed" and type(cb_change)=="function" then pcall(cb_change, newW, oldW) end
  auto_rp(kind, newW, oldW)
end

-- ===== основной детектор =====
local function update_once()
  -- отложенная отправка по ПКМ
  if M.config.auto_mode == 1 and pending_new and type(isButtonPressed) == "function" and isButtonPressed(0, 6) then
    local oldW, newW = pending_old or -1, pending_new
    local kind
    if is_empty_weapon(oldW) and not is_empty_weapon(newW) then
      kind = "shown"
    elseif not is_empty_weapon(oldW) and is_empty_weapon(newW) then
      kind = "hidden"
    elseif oldW ~= newW then
      kind = "changed"
    end
    if kind then
      local line = M.makeRpLine(kind, newW, oldW)
      if line and line ~= "" then send_chat(line) end
    end
    pending_old, pending_new = nil, nil
  end

  local ped = PLAYER_PED
  if not ped then return end
  local w = getCurrentCharWeapon(ped)
  if w == nil then return end

  if w == candidate_weapon then
    if stable_count < M.config.stable_need then stable_count = stable_count + 1 end
  else
    candidate_weapon = w
    stable_count = 1
  end

  if cooldown > 0 then
    cooldown = cooldown - 1
    return
  end

  if stable_count >= M.config.stable_need and candidate_weapon ~= prev_weapon then
    local oldW, newW = prev_weapon, candidate_weapon
    local kind
    if is_empty_weapon(oldW) and not is_empty_weapon(newW) then
      kind = "shown"
    elseif not is_empty_weapon(oldW) and is_empty_weapon(newW) then
      kind = "hidden"
    else
      kind = "changed"
    end
    prev_weapon = candidate_weapon
    cooldown = M.config.cooldown_frames
    fire(kind, newW, oldW)
  end
end

function M.start(interval_ms)
  if not M._cfg_loaded then
    load_cfg()
    M._cfg_loaded = true
  end
  if running then return end
  running = true
  prev_weapon, candidate_weapon, stable_count, cooldown = 0, 0, 0, 0
  pending_old, pending_new = nil, nil
  if interval_ms then M.config.tick_ms = interval_ms end
  ensure_send_thread()
  thr = lua_thread.create(function()
    while running do
      wait(M.config.tick_ms)
      update_once()
    end
  end)
end

function M.stop()
  running = false
  pending_old, pending_new = nil, nil
  if thr and thr:status() ~= "dead" then thr:terminate() end
  thr = nil
end

-- ======= UI =======
-- лёгкая таблица с поиском + попап-редактор
local function tooltip(text) if imgui.IsItemHovered() then imgui.SetTooltip(text) end end

-- иконки из стандартного списка (опционально)
local weaponsID = {
  0,1,2,3,4,5,6,7,8,9,22,23,24,25,26,27,28,29,32,30,31,33,34,35,36,37,38,16,17,18,39,41,42,43,10,11,12,13,14,15,44,45,46,40
}
local sprite_idx_by_weapon, unknown_sprite_idx = {}, 1
for idx, id in ipairs(weaponsID) do sprite_idx_by_weapon[id] = idx end

-- попап редактирования одного оружия
local function draw_weapon_popup(id)
  local popup = ("weapon_edit_%s"):format(id)
  local w = winfo(id)
  if imgui.BeginPopup(popup) then
    local en = imgui.new.bool(w.enable ~= false)
    if imgui.Checkbox("Включить", en) then
      w.enable = en[0]
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Учитывать это оружие для RP")

    local nm = imgui.new.char[96](w.name or "")
    if imgui.InputText("Полное имя", nm, ffi.sizeof(nm)) then
      w.name = ffi.string(nm)
      if (w.short or "") == "" then w.short = w.name end
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Например: пистолет Desert Eagle")

    local sh = imgui.new.char[48](w.short or "")
    if imgui.InputText("Короткое имя", sh, ffi.sizeof(sh)) then
      w.short = ffi.string(sh)
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Например: Deagle")

    local fr = imgui.new.char[48](w.from or "")
    if imgui.InputText("Откуда", fr, ffi.sizeof(fr)) then
      w.from = ffi.string(fr)
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Например: из кобуры, из сумки, со спины")

    local to = imgui.new.char[48](w.to or "")
    if imgui.InputText("Куда", to, ffi.sizeof(to)) then
      w.to = ffi.string(to)
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Например: в кобуру, в сумку, на плечо")

    local showv = imgui.new.char[160](table.concat(w.verbs.show or {}, ","))
    if imgui.InputText("Глаголы достать", showv, ffi.sizeof(showv)) then
      w.verbs.show = funcs.parseList(ffi.string(showv))
      if #w.verbs.show == 0 then w.verbs.show = {"достал"} end
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Через запятую: достал, вытащил, снял")

    local hidev = imgui.new.char[160](table.concat(w.verbs.hide or {}, ","))
    if imgui.InputText("Глаголы убрать", hidev, ffi.sizeof(hidev)) then
      w.verbs.hide = funcs.parseList(ffi.string(hidev))
      if #w.verbs.hide == 0 then w.verbs.hide = {"убрал"} end
      M.config.weapons[id] = w
      save_cfg()
    end
    tooltip("Через запятую: убрал, спрятал, повесил")

    imgui.EndPopup()
  end
end

-- строка одного оружия (таблица)
local function draw_weapon_row(id, w)
  local dl = imgui.GetWindowDrawList()
  local row_h = 24
  local start = imgui.GetCursorScreenPos()
  local fullW = imgui.GetContentRegionAvail().x
  -- фон строки
  dl:AddRectFilled(start, imgui.ImVec2(start.x+fullW, start.y+row_h), imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.FrameBg]), 6)

  -- иконка
  if has_sprites then
    local spr = sprite_idx_by_weapon[id] or unknown_sprite_idx
    imgui.SetCursorScreenPos(imgui.ImVec2(start.x + 6, start.y + 2))
    mimgui_funcs.drawWeaponZoom(mimgui_funcs.weapon_standard, spr, imgui.ImVec2(36, 12), 1.0)
  end

  -- чекбокс вкл
  imgui.SetCursorScreenPos(imgui.ImVec2(start.x + 48, start.y + 3))
  local en = imgui.new.bool(w.enable ~= false)
  if imgui.Checkbox(("##en_%s"):format(id), en) then
    w.enable = en[0]; M.config.weapons[id] = w; save_cfg()
  end
  tooltip("Включить/выключить")

  -- label
  local label = ("%s [%d]"):format(w.short or w.name or ("Weapon "..id), id)
  local txt = imgui.CalcTextSize(label)
  local txt_y = start.y + (row_h - txt.y) / 2
  local txt_x = start.x + 72
  imgui.GetWindowDrawList():AddText(imgui.ImVec2(txt_x, txt_y), imgui.GetColorU32Vec4(imgui.GetStyle().Colors[imgui.Col.Text]), label)

  -- кнопка редактировать
  local btn_w = 90
  imgui.SetCursorScreenPos(imgui.ImVec2(start.x + fullW - btn_w - 6, start.y + 2))
  if imgui.Button(("Изменить##%s"):format(id), imgui.ImVec2(btn_w, row_h-4)) then
    imgui.OpenPopup(("weapon_edit_%s"):format(id))
  end
  draw_weapon_popup(id)

  imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + row_h + 4))
end

-- основная панель
function M.DrawSettingsInline()
  -- верх: включение и режим
  local run = imgui.new.bool(running)
  if imgui.Checkbox("Включить модуль", run) then
    if run[0] then M.start() else M.stop() end
  end
  tooltip("Запуск/остановка отслеживания оружия")

  imgui.SameLine()
  local mode_labels = { "Авто /me", "По ПКМ" }
  local mode_labels_ffi = imgui.new["const char*"][#mode_labels](mode_labels)
  local mode = ffi.new("int[1]", M.config.auto_mode)
  if imgui.Combo("Режим", mode, mode_labels_ffi, #mode_labels) then
    M.config.auto_mode = mode[0]; save_cfg()
  end
  tooltip("Автоматически слать RP или по правой кнопке мыши")

  -- общие настройки
  if imgui.CollapsingHeader("Общие настройки") then
    local two = imgui.new.bool(M.config.change_as_two_lines)
    if imgui.Checkbox("Смена в одну строку", imgui.new.bool(not two[0])) then
      -- визуальный трюк: чекбокс формулируем как «в одну строку»
    end
    -- принудительно держим одну строку:
    M.config.change_as_two_lines = false

    local ign = imgui.new.bool(M.config.ignore_knuckles)
    if imgui.Checkbox("Игнорировать кастеты как пустые", ign) then
      M.config.ignore_knuckles = ign[0]; save_cfg()
    end

    local prefix = imgui.new.char[16](M.config.prefix)
    if imgui.InputText("Префикс команды", prefix, ffi.sizeof(prefix)) then
      M.config.prefix = ffi.string(prefix); save_cfg()
    end

    local flavor = ffi.new("int[1]", M.config.flavor_level)
    if imgui.SliderInt("Степень приукрашивания", flavor, 1, 3) then
      M.config.flavor_level = flavor[0]; save_cfg()
    end

    local max_len = ffi.new("int[1]", M.config.max_len)
    if imgui.InputInt("Макс длина RP-строки", max_len) then
      M.config.max_len = math.max(30, max_len[0]); save_cfg()
    end

    imgui.Separator()
    local tick = ffi.new("int[1]", M.config.tick_ms)
    if imgui.InputInt("Интервал проверки (мс)", tick) then
      M.config.tick_ms = math.max(10, tick[0]); save_cfg()
    end

    local stable = ffi.new("int[1]", M.config.stable_need)
    if imgui.InputInt("Кадров стабильности", stable) then
      M.config.stable_need = math.max(1, stable[0]); save_cfg()
    end

    local cooldown = ffi.new("int[1]", M.config.cooldown_frames)
    if imgui.InputInt("Кадров задержки", cooldown) then
      M.config.cooldown_frames = math.max(0, cooldown[0]); save_cfg()
    end

    local min_gap = ffi.new("int[1]", M.config.min_me_gap_ms)
    if imgui.InputInt("Пауза между /me (мс)", min_gap) then
      M.config.min_me_gap_ms = math.max(0, min_gap[0]); save_cfg()
    end
  end

  -- стилистика: наречия/соединители (глобальные)
  if imgui.CollapsingHeader("Слова и связки (общие)") then
    local adv_show = imgui.new.char[512](table.concat(M.config.adverbs_show or {}, "\n"))
    if imgui.InputTextMultiline("Наречия (достать)", adv_show, ffi.sizeof(adv_show), imgui.ImVec2(0, 80)) then
      M.config.adverbs_show = funcs.parseLines(ffi.string(adv_show)); save_cfg()
    end
    local adv_hide = imgui.new.char[512](table.concat(M.config.adverbs_hide or {}, "\n"))
    if imgui.InputTextMultiline("Наречия (убрать)", adv_hide, ffi.sizeof(adv_hide), imgui.ImVec2(0, 80)) then
      M.config.adverbs_hide = funcs.parseLines(ffi.string(adv_hide)); save_cfg()
    end
    local conn_full = imgui.new.char[512](table.concat(M.config.connectors_full or {}, "\n"))
    if imgui.InputTextMultiline("Соединители (полные)", conn_full, ffi.sizeof(conn_full), imgui.ImVec2(0, 80)) then
      M.config.connectors_full = funcs.parseLines(ffi.string(conn_full), true); save_cfg()
    end
    local conn_short = imgui.new.char[512](table.concat(M.config.connectors_short or {}, "\n"))
    if imgui.InputTextMultiline("Соединители (короткие)", conn_short, ffi.sizeof(conn_short), imgui.ImVec2(0, 80)) then
      M.config.connectors_short = funcs.parseLines(ffi.string(conn_short), true); save_cfg()
    end
  end

  -- список оружия: поиск + добавление + строки
  if imgui.CollapsingHeader("Оружие") then
    M._search = M._search or imgui.new.char[64]()
    imgui.InputText("Поиск", M._search, ffi.sizeof(M._search))
    local query = ffi.string(M._search):lower()

    imgui.SameLine()
    if imgui.Button("+ Добавить") then imgui.OpenPopup("weapon_add_popup") end

    if imgui.BeginPopup("weapon_add_popup") then
      M._new_id = M._new_id or imgui.new.int(0)
      M._new_name = M._new_name or imgui.new.char[64]()
      M._new_short= M._new_short or imgui.new.char[32]()
      M._new_from = M._new_from or imgui.new.char[32]("из кармана")
      M._new_to   = M._new_to   or imgui.new.char[32]("в карман")
      M._new_show = M._new_show or imgui.new.char[128]("достал, вытащил")
      M._new_hide = M._new_hide or imgui.new.char[128]("убрал, спрятал")

      imgui.InputInt("ID", M._new_id)
      imgui.SameLine()
      if imgui.Button("Текущее") then
        local ped = PLAYER_PED
        if ped then M._new_id[0] = getCurrentCharWeapon(ped) or 0 end
      end
      imgui.InputText("Имя", M._new_name, ffi.sizeof(M._new_name))
      imgui.InputText("Коротко", M._new_short, ffi.sizeof(M._new_short))
      imgui.InputText("Откуда", M._new_from, ffi.sizeof(M._new_from))
      imgui.InputText("Куда",   M._new_to,   ffi.sizeof(M._new_to))
      imgui.InputText("Глаголы достать", M._new_show, ffi.sizeof(M._new_show))
      imgui.InputText("Глаголы убрать",  M._new_hide, ffi.sizeof(M._new_hide))

      if imgui.Button("OK##addw") then
        local nid  = M._new_id[0]
        local nm   = ffi.string(M._new_name)
        local sh   = ffi.string(M._new_short)
        local fr   = ffi.string(M._new_from)
        local to   = ffi.string(M._new_to)
        local show = funcs.parseList(ffi.string(M._new_show)); if #show==0 then show={"достал"} end
        local hide = funcs.parseList(ffi.string(M._new_hide)); if #hide==0 then hide={"убрал"} end
        if nm ~= "" then
          M.config.weapons[nid] = normalize_weapon(nid, {
            enable = true, name = nm, short = (sh~="" and sh or nm),
            from = fr, to = to, verbs = { show = show, hide = hide }
          })
          save_cfg()
        end
        imgui.CloseCurrentPopup()
      end
      imgui.SameLine()
      if imgui.Button("Отмена##addw") then imgui.CloseCurrentPopup() end
      imgui.EndPopup()
    end

    imgui.Separator()

    -- аккуратная таблица
    imgui.Columns(1, nil, false)
    for id, w in pairs(M.config.weapons) do
      local label = (w.short or w.name or ("Weapon "..id))
      local match = (query == "")
        or tostring(id):find(query, 1, true)
        or (label:lower():find(query, 1, true) ~= nil)
      if match then
        draw_weapon_row(id, w)
      end
    end
  end
end

-- ===== публичные сеттеры (сохранение) =====
function M.setAutoRp(b)            M.config.auto_mode = b and 0 or 1; save_cfg() end
function M.setSender(fn)           M.config.sender = fn; save_cfg() end
function M.setPrefix(p)            M.config.prefix = tostring(p or "/me"); save_cfg() end
function M.setStableNeed(n)        M.config.stable_need = math.max(1, tonumber(n) or 3); save_cfg() end
function M.setCooldownFrames(n)    M.config.cooldown_frames = math.max(0, tonumber(n) or 0); save_cfg() end
function M.setIgnoreKnuckles(b)    M.config.ignore_knuckles = not not b; save_cfg() end
function M.setMinMeGapMs(n)        M.config.min_me_gap_ms = math.max(0, tonumber(n) or 0); save_cfg() end
function M.setFlavorLevel(n)       M.config.flavor_level = math.max(1, math.min(3, tonumber(n) or 2)); save_cfg() end
function M.setMaxLen(n)            M.config.max_len = math.max(30, tonumber(n) or 96); save_cfg() end

-- ===== пример логов =====
function M.bindExample()
  M.onAny(function(kind, newW, oldW)
    if sampAddChatMessage then
      sampAddChatMessage(("[оружие] %s: %d -> %d"):format(kind, oldW or -1, newW or -1), 0xFFCC66)
    end
  end)
end

return M
