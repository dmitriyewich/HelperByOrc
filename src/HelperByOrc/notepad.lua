local module = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
local paths = require("HelperByOrc.paths")
encoding.default = "CP1251"
local u8 = encoding.UTF8
local new = imgui.new

local mimgui_funcs
local ok2, fa = pcall(require, "fAwesome7")
local funcs
local passFilter
local escape_imgui_text
local tags_module
local config_manager
local event_bus

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
	tags_module = mod.tags or tags_module
	if not tags_module then
		local ok_tags, tags = pcall(require, "HelperByOrc.tags")
		if ok_tags then
			tags_module = tags
		end
	end
	passFilter = funcs.passFilter
	escape_imgui_text = mimgui_funcs.escape_imgui_text
end

syncDependencies()
local JSON_PATH_REL = "notepad.json"
local base_path = paths.dataPath("notepad")
local images_path = paths.join(base_path, "images")
local ok_moonloader, moonloader = pcall(require, "moonloader")
local dlstatus = ok_moonloader and moonloader and moonloader.download_status or nil

local folder_icon = ok2 and fa and fa.FOLDER or ""
local file_icon = ok2 and fa and fa.FILE_LINES or ""
local note_icon = ok2 and fa and fa.BOOK or ""
local plus_icon = ok2 and fa and fa.SQUARE_PLUS or "+"
local edit_icon = ok2 and fa and fa.PEN or ""
local save_icon = ok2 and fa and fa.FLOPPY_DISK or ""
local cancel_icon = ok2 and fa and fa.XMARK or ""
local delete_icon = ok2 and fa and fa.TRASH or ""
local search_icon = ok2 and fa and fa.MAGNIFYING_GLASS or ""
local star_icon = ok2 and fa and fa.STAR or ""
local exp_icon = ok2 and fa and fa.FILE_EXPORT or ""
local arrows_icon = ok2 and fa and fa.ARROWS_LEFT_RIGHT or "<->"
local folder_plus_icon = ok2 and fa and fa.FOLDER_PLUS or "+"
local close_icon = ok2 and fa and fa.XMARK or "[X]"

local filter = imgui.ImGuiTextFilter()

-- ====== Case-insensitive (кириллица) ======

local supported_font_sizes = {
	[12] = true,
	[14] = true,
	[16] = true,
	[18] = true,
	[30] = true,
}
local note_font_sizes = { 12, 14, 16, 18, 30 }
local note_fonts = {}
local note_fonts_with_icons = {}
local font_candidates = {
	"C:\\Windows\\Fonts\\arial.ttf",
	"C:\\Windows\\Fonts\\tahoma.ttf",
	"trebucbd.ttf",
}

local txt_cache, txt_threads, tree_root, tree_flat = {}, {}, {}, {}
local notes, selectedNode, editingText, editingBufSize = {}, nil, nil, 4096
local newNoteTitle, newNoteCat = imgui.new.char[64](), imgui.new.char[128]()
local editingMode = new.bool(false)
local copyModeEnabled = new.bool(false)
local copyModeKind = new.int(1) -- 1 = выделение, 2 = ЛКМ по строкам
local favorites, history = {}, {}
local draft = { text = "", idx = -1, isJson = true }
local fade_alpha = {}
local move_cat_buf = imgui.new.char[128]()
local popup_move = new.bool(false)
local popup_warning = new.bool(false)
local popup_rename = new.bool(false)
local rename_buf = imgui.new.char[128]()
local popup_newfolder = new.bool(false)
local popup_bb_help = new.bool(false)
local newfolder_buf = imgui.new.char[128]()
local current_folder = nil
local scrollToLine = nil
local imported_original_text = ""
local rich_cache = { text = nil, lines = nil }
local rich_wrapped_cache = { text = nil, avail = -1, font_size = -1, lines = nil }
local wrapped_cache = { text = nil, avail = -1, font_size = -1, lines = nil }
local tag_render_cache = { raw = nil, rendered = nil, at = 0 }
local copy_mode_buf = nil
local copy_mode_buf_size = 0
local copy_mode_buf_text = nil
local copy_mode_lines_cache = { raw = nil, lines = nil }
local image_texture_cache = {}
local image_size_cache = {}
local image_url_cache = {}
local notes_revision = 0
local tree_revision = 0
local txt_cache_revision = 0
local global_search_cache = { key = nil, results = {} }
local txt_tree_filter_cache = { key = nil, matches = nil }
local bb_help_entries = {
	{ code = "#img(1.png)", desc = "Картинка из HelperByOrc\\notepad\\images\\." },
	{ code = "#img(1.png, size(10,10))", desc = "Фиксированный размер изображения." },
	{ code = "#img(1.png, pos(1,1))", desc = "Позиция X/Y относительно окна шпаргалки." },
	{ code = "#img(1.png, pos(1,1), size(10,10))", desc = "Позиция + размер." },
	{ code = "#img(1.png, size(10,10), pos(1,1))", desc = "Порядок size/pos не важен." },
	{ code = "#img(1.png, size(-1,-1))", desc = "Авторазмер от области окна." },
	{ code = "#img(weapons/1.png)", desc = "Вложенный путь в images\\." },
	{ code = "#img(C:\\Users\\user\\Изображения\\Wallpapers\\1.png)", desc = "Абсолютный путь Windows." },
	{ code = "#img(https://site.com/image.png)", desc = "URL с кешированием в images\\url\\." },
	{ code = "{ff0000}Красный текст", desc = "Цвет текста строки (RRGGBB)." },
	{ code = "#color00ff00 Зелёный текст", desc = "Альтернатива цвету через команду." },
	{ code = "#bg202020 #colorffffff Текст на фоне", desc = "Фон под текстом + цвет текста." },
	{ code = "#alpha70 Полупрозрачный текст", desc = "Прозрачность 0..100 для строки." },
	{ code = "#center Заголовок", desc = "Выравнивание по центру." },
	{ code = "#right Текст справа", desc = "Выравнивание по правому краю." },
	{ code = "#left Текст слева", desc = "Явное выравнивание по левому краю." },
	{ code = "#sameline #right Правая часть", desc = "Приклеить к предыдущей строке." },
	{ code = "#indent24 Отступ 24px", desc = "Отступ от левого края в пикселях." },
	{ code = "#pad40 Ещё отступ", desc = "Синоним #indent." },
	{ code = "#tab2 Табуляция", desc = "Быстрый отступ: 1 tab = 32px." },
	{ code = "#font18 Крупнее", desc = "Размеры: 12, 14, 16, 18, 30." },
	{ code = "#upper верхний регистр", desc = "Преобразовать строку в UPPERCASE." },
	{ code = "#lower НИЖНИЙ РЕГИСТР", desc = "Преобразовать строку в lowercase." },
	{ code = "#bullet Пункт списка", desc = "Маркер списка перед строкой." },
	{ code = "#br2", desc = "Добавить 2 пустые строки после текущей." },
	{ code = "#hr", desc = "Горизонтальная линия-разделитель." },
	{ code = "#hrff8800", desc = "Линия-разделитель с цветом." },
	{ code = "#iconCOMPASS Навигация", desc = "Иконка FontAwesome7 по имени." },
	{ code = "{myorg} {myorgrang}", desc = "Переменные из tags.lua работают в просмотре." },
}

local function buildBbHelpExamplesText()
	local lines = {}
	for _, entry in ipairs(bb_help_entries) do
		local code = tostring(entry.code or "")
		if code ~= "" then
			lines[#lines + 1] = code
		end
	end
	return table.concat(lines, "\n")
end

local function trimString(value)
	local str = tostring(value or "")
	str = str:gsub("^%s+", "")
	str = str:gsub("%s+$", "")
	return str
end

local function fileExists(path)
	local f = io.open(path, "rb")
	if f then
		f:close()
		return true
	end
	return false
end

local function normalizePath(path)
	path = tostring(path or "")
	path = path:gsub("/", "\\")
	path = path:gsub("\\+", "\\")
	return path
end

local function isAbsolutePath(path)
	path = normalizePath(path)
	return path:match("^%a:[\\]") ~= nil or path:sub(1, 2) == "\\\\"
end

local function ensureDirectory(path)
	path = normalizePath(path)
	if path == "" then
		return false
	end
	if type(doesDirectoryExist) == "function" and doesDirectoryExist(path) then
		return true
	end

	local prefix = ""
	local rest = path
	local drive, tail = path:match("^([A-Za-z]:)\\(.*)$")
	if drive then
		prefix = drive .. "\\"
		rest = tail
	elseif path:sub(1, 2) == "\\\\" then
		local server, share, unc_tail = path:match("^\\\\([^\\]+)\\([^\\]+)\\?(.*)$")
		if server and share then
			prefix = "\\\\" .. server .. "\\" .. share
			rest = unc_tail or ""
		else
			prefix = "\\\\"
			rest = path:sub(3)
		end
	elseif path:sub(1, 1) == "\\" then
		prefix = "\\"
		rest = path:sub(2)
	end

	for part in rest:gmatch("[^\\]+") do
		if prefix == "" or prefix == "\\" then
			prefix = prefix .. part
		elseif prefix:sub(-1) == "\\" then
			prefix = prefix .. part
		else
			prefix = prefix .. "\\" .. part
		end
		if type(doesDirectoryExist) ~= "function" or not doesDirectoryExist(prefix) then
			if type(createDirectory) == "function" then
				createDirectory(prefix)
			end
		end
	end

	if type(doesDirectoryExist) == "function" then
		return doesDirectoryExist(path)
	end
	return true
end

local function ensureBasePath()
	ensureDirectory(base_path)
	ensureDirectory(images_path)
	ensureDirectory(paths.join(images_path, "url"))
end

local function sanitizeRelativeImagePath(path)
	path = normalizePath(path)
	local parts = {}
	for part in path:gmatch("[^\\]+") do
		if part == ".." then
			if #parts > 0 then
				table.remove(parts)
			end
		elseif part ~= "" and part ~= "." then
			parts[#parts + 1] = part
		end
	end
	return table.concat(parts, "\\")
end

local function hashString32(text)
	text = tostring(text or "")
	local hash = 2166136261
	for i = 1, #text do
		hash = (hash * 16777619 + text:byte(i)) % 4294967296
	end
	return string.format("%08x", hash)
end

local function isHttpUrl(source)
	local lower = trimString(source):lower()
	return lower:sub(1, 7) == "http://" or lower:sub(1, 8) == "https://"
end

local function buildUrlImageCachePath(url)
	local clean = tostring(url or ""):gsub("[?#].*$", "")
	local file_name = clean:match("([^/\\]+)$") or "image"
	file_name = file_name:gsub("[^%w%._%-]", "_")
	if file_name == "" then
		file_name = "image"
	end
	if not file_name:find("%.[%w]+$") then
		file_name = file_name .. ".img"
	end
	return paths.join(images_path, "url", hashString32(url) .. "_" .. file_name)
end

local function bump_notes_revision()
	notes_revision = notes_revision + 1
	global_search_cache.key = nil
end

local function bump_tree_revision()
	tree_revision = tree_revision + 1
	global_search_cache.key = nil
	txt_tree_filter_cache.key = nil
	txt_tree_filter_cache.matches = nil
end

local function bump_txt_cache_revision()
	txt_cache_revision = txt_cache_revision + 1
	global_search_cache.key = nil
	txt_tree_filter_cache.key = nil
	txt_tree_filter_cache.matches = nil
end

local function set_txt_cache(path, content)
	txt_cache[path] = content or ""
	bump_txt_cache_revision()
end

local function initNoteFonts()
	if next(note_fonts) then
		return
	end
	local io = imgui.GetIO()
	local ranges = io.Fonts:GetGlyphRangesCyrillic()
	for _, size in ipairs(note_font_sizes) do
		note_fonts_with_icons[size] = false
		for _, path in ipairs(font_candidates) do
			local ok_font, font = pcall(function()
				return io.Fonts:AddFontFromFileTTF(path, size, nil, ranges)
			end)
			if ok_font and font ~= nil then
				note_fonts[size] = font
				break
			end
		end
	end
end

local function saveNotes()
	if config_manager then
		config_manager.markDirty("notepad")
	else
		funcs.saveTableToJson(notes, paths.dataPath(JSON_PATH_REL))
	end
	bump_notes_revision()
end
local function loadNotes()
	if config_manager then
		notes = config_manager.get("notepad") or notes
	else
		notes = funcs.loadTableFromJson(JSON_PATH_REL, {})
	end
	if type(notes) ~= "table" then
		notes = {}
	end
	favorites, history = {}, {}
	for _, note in ipairs(notes) do
		note.title = tostring(note.title or "")
		note.category = tostring(note.category or "")
		note.text = tostring(note.text or "")
		note._fav = note._fav and true or false
		note._ctime = tonumber(note._ctime) or os.time()
		note._mtime = tonumber(note._mtime) or note._ctime
	end
	bump_notes_revision()
end
local function getAllTxtFilesRecursive(path)
	local result = {}
	local function scan(current, rel)
		local handle, file = findFirstFile(current .. "\\*")
		if not handle then
			return
		end
		while file do
			if file ~= "." and file ~= ".." then
				local full = current .. "\\" .. file
				if doesDirectoryExist(full) then
					scan(full, rel .. file .. "\\")
				elseif file:lower():match("%.txt$") then
					table.insert(result, rel .. file)
				end
			end
			file = findNextFile(handle)
		end
		if handle then
			findClose(handle)
		end
	end
	scan(path, "")
	return result
end
local function loadTreeFromFiles()
	ensureBasePath()
	txt_cache = {}
	txt_threads = {}
	bump_txt_cache_revision()
	tree_flat = getAllTxtFilesRecursive(base_path)
	tree_root = {}
	for _, path in ipairs(tree_flat) do
		local node = tree_root
		for part in path:gmatch("([^\\]+)") do
			if not node[part] then
				node[part] = {}
			end
			node = node[part]
		end
		node.__is_file = path
	end
	bump_tree_revision()
end
local function readTxtFile(path, callback)
	if txt_cache[path] then
		return callback(txt_cache[path])
	end
	local full = base_path .. "\\" .. path
	local attr = io.open(full, "rb")
	if attr then
		attr:seek("end")
		local size = attr:seek()
		attr:close()
		if size > 1024 * 1024 then
			if not txt_threads[path] then
				txt_threads[path] = lua_thread.create(function(full, path)
					local f = io.open(full, "r")
					local c = f and f:read("*a") or ""
					if f then
						f:close()
					end
					set_txt_cache(path, c)
				end, full, path)
			end
			return callback("[Загрузка большого файла...]")
		end
	end
	local f = io.open(full, "r")
	local content = ""
	if f then
		content = f:read("*a")
		f:close()
	end
	set_txt_cache(path, content or "")
	callback(txt_cache[path])
end

local max_history = 7
local function addToHistory(idx)
	for i = #history, 1, -1 do
		if history[i] == idx then
			table.remove(history, i)
		end
	end
	table.insert(history, 1, idx)
	if #history > max_history then
		table.remove(history)
	end
end
local function noteMatchesFilter(note, filter_raw)
	if not note then
		return false
	end
	if filter_raw == "" then
		return true
	end
	return passFilter((note.title or "") .. " " .. (note.text or ""), filter_raw)
end
local function selectJsonNode(idx, line_idx)
	local note = notes[idx]
	if not note then
		return
	end
	selectedNode = { isJson = true, idx = idx }
	editingMode[0] = false
	scrollToLine = (line_idx and line_idx > 0) and line_idx or nil
	editingBufSize = math.max(4096, #(note.text or "") + 2048)
	editingText = imgui.new.char[editingBufSize](note.text or "")
end
local function selectTxtNode(path, content, line_idx)
	local text = tostring(content or "")
	selectedNode = { isJson = false, path = path, idx = -1 }
	editingMode[0] = false
	scrollToLine = (line_idx and line_idx > 0) and line_idx or nil
	editingBufSize = math.max(4096, #text + 2048)
	editingText = imgui.new.char[editingBufSize](text)
	imported_original_text = text
end
local function rebuildFadeAfterRemove(removed_idx)
	local shifted = {}
	for idx, alpha in pairs(fade_alpha) do
		if idx < removed_idx then
			shifted[idx] = alpha
		elseif idx > removed_idx then
			shifted[idx - 1] = alpha
		end
	end
	fade_alpha = shifted
end
local function compactHistoryAfterRemove(removed_idx)
	for i = #history, 1, -1 do
		local value = history[i]
		if value == removed_idx then
			table.remove(history, i)
		elseif value > removed_idx then
			history[i] = value - 1
		end
	end
end
local function removeNoteAt(idx, skip_save)
	if not idx or not notes[idx] then
		return false
	end
	table.remove(notes, idx)
	compactHistoryAfterRemove(idx)
	rebuildFadeAfterRemove(idx)
	if selectedNode and selectedNode.isJson then
		if selectedNode.idx == idx then
			selectedNode = nil
			editingMode[0] = false
			editingText = nil
		elseif selectedNode.idx > idx then
			selectedNode.idx = selectedNode.idx - 1
		end
	end
	if not skip_save then
		saveNotes()
	end
	return true
end
local function isDuplicateTitle(title)
	for i, n in ipairs(notes) do
		if n.title == title then
			return true
		end
	end
	return false
end
local function createNote()
	local title = ffi.string(newNoteTitle)
	local cat = ffi.string(newNoteCat)
	if title == "" then
		return
	end
	if isDuplicateTitle(title) then
		imgui.OpenPopup("Дубликат")
		return
	end
	table.insert(
		notes,
		{ title = title, category = cat, text = "", _fav = false, _ctime = os.time(), _mtime = os.time() }
	)
	saveNotes()
	selectJsonNode(#notes)
	imgui.StrCopy(newNoteTitle, "")
	imgui.StrCopy(newNoteCat, "")
	fade_alpha[#notes] = 0
end
local function duplicateNote(idx)
	local note = notes[idx]
	if note then
		local t = note.title .. "_копия"
		table.insert(notes, {
			title = t,
			category = note.category,
			text = note.text,
			_fav = false,
			_ctime = os.time(),
			_mtime = os.time(),
		})
		saveNotes()
	end
end
local function fadeAlpha(idx, speed, isActive)
	fade_alpha[idx] = fade_alpha[idx] or (isActive and 1 or 0.15)
	local delta = imgui.GetIO().DeltaTime or 0.017
	if isActive then
		fade_alpha[idx] = math.min(fade_alpha[idx] + speed * delta * 60, 1)
	else
		fade_alpha[idx] = math.max(fade_alpha[idx] - speed * delta * 60, 0.15)
	end
	return fade_alpha[idx]
end
local function exportNote(note)
	ensureBasePath()
	local name = note.title:gsub("[^%w%d_%-]", "_")
	-- local name = u8:decode(name)
	local out = base_path .. "\\export_" .. name .. ".txt"
	local f = io.open(out, "w+b")
	if f then
		f:write(note.text or "")
		f:close()
	end
end

pcall(ffi.cdef, [[
int MultiByteToWideChar(unsigned int cp, unsigned long fl, const char* src, int cb, wchar_t* dst, int cch);
void* GlobalAlloc(unsigned int uFlags, size_t dwBytes);
void* GlobalLock(void* hMem);
int   GlobalUnlock(void* hMem);
void* GlobalFree(void* hMem);
int   OpenClipboard(void* hWndNewOwner);
int   CloseClipboard(void);
int   EmptyClipboard(void);
void* SetClipboardData(unsigned int uFormat, void* hMem);
enum { GMEM_MOVEABLE = 0x0002, CF_UNICODETEXT = 13 };
]])

function setClipboardTextUTF8(text)
	local str = tostring(text or "")
	local len = 0
	if #str > 0 then
		len = ffi.C.MultiByteToWideChar(65001, 0, str, #str, nil, 0)
		if len < 1 then
			return false
		end
	end
	local hMem = ffi.C.GlobalAlloc(ffi.C.GMEM_MOVEABLE, (len + 1) * 2)
	if hMem == nil then
		return false
	end
	local wstr = ffi.cast("wchar_t*", ffi.C.GlobalLock(hMem))
	if wstr == nil then
		ffi.C.GlobalFree(hMem)
		return false
	end
	if len > 0 then
		ffi.C.MultiByteToWideChar(65001, 0, str, #str, wstr, len)
	end
	wstr[len] = 0
	ffi.C.GlobalUnlock(hMem)
	if ffi.C.OpenClipboard(nil) == 0 then
		ffi.C.GlobalFree(hMem)
		return false
	end
	ffi.C.EmptyClipboard()
	if ffi.C.SetClipboardData(ffi.C.CF_UNICODETEXT, hMem) == nil then
		ffi.C.CloseClipboard()
		ffi.C.GlobalFree(hMem)
		return false
	end
	ffi.C.CloseClipboard()
	return true
end

local function copyToClipboard(str)
	setClipboardTextUTF8(str)
end

local function applyTagVariables(text)
	text = tostring(text or "")
	if text == "" or not tags_module or type(tags_module.change_tags) ~= "function" then
		return text
	end
	if not text:find("{", 1, true) then
		return text
	end

	local now = os.clock()
	if tag_render_cache.raw == text and (now - tag_render_cache.at) < 0.25 then
		return tag_render_cache.rendered
	end

	local rendered = text:gsub("%b{}", function(token)
		local key = token:sub(2, -2)
		if key:match("^[%x][%x][%x][%x][%x][%x]$") then
			return token
		end
		local ok, val = pcall(tags_module.change_tags, token)
		if ok then
			if val == nil then
				return ""
			end
			return tostring(val)
		end
		return token
	end)

	tag_render_cache.raw = text
	tag_render_cache.rendered = rendered
	tag_render_cache.at = now
	return rendered
end

-- === Word-wrap+clipper ===
local function utf8StepFromByte(byte)
	byte = tonumber(byte) or 0
	if byte >= 0xF0 then
		return 4
	elseif byte >= 0xE0 then
		return 3
	elseif byte >= 0xC0 then
		return 2
	end
	return 1
end

local function split_text_wrapped(text)
	text = tostring(text or "")
	if text == "" then
		return { "" }
	end
	local result = {}
	local pFont = imgui.GetFont()
	local scale = 1.0
	local avail = math.max(1, imgui.GetContentRegionAvail().x)

	local start = 1
	while start <= #text do
		local line_end = text:find("\n", start) or (#text + 1)
		local line = text:sub(start, line_end - 1):gsub("\r$", "")
		if line == "" then
			table.insert(result, "")
		else
			local ffi_line = ffi.new("char[?]", #line + 1, line)
			local text_ptr = ffi_line
			local text_end = text_ptr + ffi.sizeof(ffi_line) - 1

			while text_ptr < text_end do
				local endPrevLine = pFont:CalcWordWrapPositionA(scale, text_ptr, text_end, avail)
				local len = tonumber(ffi.cast("intptr_t", endPrevLine) - ffi.cast("intptr_t", text_ptr))
				if len <= 0 then
					local remain = tonumber(ffi.cast("intptr_t", text_end) - ffi.cast("intptr_t", text_ptr))
					local step = utf8StepFromByte(text_ptr[0])
					if step > remain then
						step = math.max(1, remain)
					end
					len = step
					endPrevLine = text_ptr + step
				end
				table.insert(result, ffi.string(text_ptr, len))
				text_ptr = endPrevLine
				while text_ptr < text_end and (text_ptr[0] == 32 or text_ptr[0] == 9) do
					text_ptr = text_ptr + 1
				end
			end
		end

		start = line_end + 1
	end
	if #result == 0 then
		result[1] = ""
	end
	return result
end
local function getWrappedLinesCached(text)
	text = tostring(text or "")
	local avail = math.floor(imgui.GetContentRegionAvail().x)
	local font_size = imgui.GetFontSize()
	if
		wrapped_cache.text == text
		and wrapped_cache.avail == avail
		and wrapped_cache.font_size == font_size
		and wrapped_cache.lines
	then
		return wrapped_cache.lines
	end
	wrapped_cache.text = text
	wrapped_cache.avail = avail
	wrapped_cache.font_size = font_size
	wrapped_cache.lines = split_text_wrapped(text)
	return wrapped_cache.lines
end
local function showLongTextClipper(text)
	local wrapped_lines = getWrappedLinesCached(text)
	local count = #wrapped_lines
	local clipper = imgui.ImGuiListClipper(count)
	while clipper:Step() do
		for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
			imgui.TextUnformatted(wrapped_lines[i])
		end
	end
end

local function shouldUseRichMarkup(text)
	text = tostring(text or "")
	for line in (text .. "\n"):gmatch("(.-)\n") do
		if line:match("^%s*%{[%x][%x][%x][%x][%x][%x]%}") then
			return true
		end
		if line:match("^%s*#") then
			return true
		end
	end
	return false
end
local function parseColorHex(hex)
	if not hex or #hex ~= 6 then
		return nil
	end
	local r = tonumber(hex:sub(1, 2), 16)
	local g = tonumber(hex:sub(3, 4), 16)
	local b = tonumber(hex:sub(5, 6), 16)
	if not r or not g or not b then
		return nil
	end
	return imgui.ImVec4(r / 255, g / 255, b / 255, 1)
end
local function resolveIconGlyph(icon_name)
	if not fa or type(fa) ~= "table" then
		return ""
	end
	local glyph = fa[icon_name:upper()]
	if type(glyph) == "string" and glyph ~= "" then
		return glyph
	end
	return ""
end
local function hasDirectiveBoundary(text, consumed_len)
	local ch = text:sub(consumed_len + 1, consumed_len + 1)
	return ch == "" or ch:match("[%s#]") ~= nil
end

local function splitDirectiveArgs(raw)
	local args = {}
	local depth = 0
	local current = {}
	raw = tostring(raw or "")

	for i = 1, #raw do
		local ch = raw:sub(i, i)
		if ch == "(" then
			depth = depth + 1
			current[#current + 1] = ch
		elseif ch == ")" then
			if depth > 0 then
				depth = depth - 1
			end
			current[#current + 1] = ch
		elseif ch == "," and depth == 0 then
			args[#args + 1] = trimString(table.concat(current))
			current = {}
		else
			current[#current + 1] = ch
		end
	end

	args[#args + 1] = trimString(table.concat(current))
	return args
end

local function parseImagePairArg(arg, name)
	arg = tostring(arg or "")
	local lowered = arg:lower()
	local x, y = lowered:match("^" .. name .. "%s*%(%s*([%-]?%d+)%s*,%s*([%-]?%d+)%s*%)$")
	if not x or not y then
		return nil, nil
	end
	return tonumber(x), tonumber(y)
end

local function parseImageDirectiveArgs(raw_args)
	local args = splitDirectiveArgs(raw_args)
	local source = trimString(args[1] or "")
	if source == "" then
		return nil
	end

	if #source >= 2 then
		local first = source:sub(1, 1)
		local last = source:sub(-1)
		if (first == "'" and last == "'") or (first == '"' and last == '"') then
			source = source:sub(2, -2)
		end
	end

	local spec = { source = source }
	for i = 2, #args do
		local arg = trimString(args[i])
		local w, h = parseImagePairArg(arg, "size")
		if w and h then
			spec.size_w = w
			spec.size_h = h
		else
			local x, y = parseImagePairArg(arg, "pos")
			if x and y then
				spec.pos_x = x
				spec.pos_y = y
			end
		end
	end
	return spec
end

local function parseImageDirectivePrefix(rest)
	local lower = rest:lower()
	if lower:sub(1, 5) ~= "#img(" then
		return nil, nil
	end

	local depth = 0
	local close_idx = nil
	for i = 5, #rest do
		local ch = rest:sub(i, i)
		if ch == "(" then
			depth = depth + 1
		elseif ch == ")" then
			depth = depth - 1
			if depth == 0 then
				close_idx = i
				break
			end
		end
	end

	if not close_idx or not hasDirectiveBoundary(lower, close_idx) then
		return nil, nil
	end

	local spec = parseImageDirectiveArgs(rest:sub(6, close_idx - 1))
	if not spec then
		return nil, nil
	end

	return spec, close_idx
end

local function parseRichSegment(raw_line)
	local segment = {
		align = "left",
		same_line = false,
		color = nil,
		bg_color = nil,
		hr_color = nil,
		alpha = 1,
		font_size = nil,
		indent = 0,
		icon = "",
		bullet = false,
		transform = nil,
		extra_breaks = 0,
		is_hr = false,
		image = nil,
		text = "",
	}
	local original = tostring(raw_line or "")
	local leading_ws = original:match("^(%s*)") or ""
	local rest = original:sub(#leading_ws + 1)
	local used_directive = false

	while true do
		local consumed = false
		local consumed_len = 0
		local rest_lower = rest:lower()
		local hex = rest:match("^%{([%x][%x][%x][%x][%x][%x])%}")
		if hex then
			segment.color = parseColorHex(hex) or segment.color
			consumed = true
			consumed_len = 8
			used_directive = true
		elseif rest_lower:sub(1, 9) == "#sameline" and hasDirectiveBoundary(rest_lower, 9) then
			segment.same_line = true
			consumed = true
			consumed_len = 9
			used_directive = true
		elseif rest_lower:sub(1, 6) == "#right" and hasDirectiveBoundary(rest_lower, 6) then
			segment.align = "right"
			consumed = true
			consumed_len = 6
			used_directive = true
		elseif rest_lower:sub(1, 7) == "#center" and hasDirectiveBoundary(rest_lower, 7) then
			segment.align = "center"
			consumed = true
			consumed_len = 7
			used_directive = true
		elseif rest_lower:sub(1, 5) == "#left" and hasDirectiveBoundary(rest_lower, 5) then
			segment.align = "left"
			consumed = true
			consumed_len = 5
			used_directive = true
		elseif rest_lower:sub(1, 7) == "#bullet" and hasDirectiveBoundary(rest_lower, 7) then
			segment.bullet = true
			consumed = true
			consumed_len = 7
			used_directive = true
		elseif rest_lower:sub(1, 6) == "#upper" and hasDirectiveBoundary(rest_lower, 6) then
			segment.transform = "upper"
			consumed = true
			consumed_len = 6
			used_directive = true
		elseif rest_lower:sub(1, 6) == "#lower" and hasDirectiveBoundary(rest_lower, 6) then
			segment.transform = "lower"
			consumed = true
			consumed_len = 6
			used_directive = true
		else
			local image_spec, image_len = parseImageDirectivePrefix(rest)
			if image_spec then
				segment.image = image_spec
				consumed = true
				consumed_len = image_len
				used_directive = true
			end
		end
		if not consumed then
			local font_digits = rest_lower:match("^#font(%d+)")
			if font_digits then
				local size = tonumber(font_digits)
				local len = 5 + #font_digits
				if supported_font_sizes[size] and hasDirectiveBoundary(rest_lower, len) then
					segment.font_size = size
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local icon_name = rest_lower:match("^#icon([%w_]+)")
				local len = icon_name and (5 + #icon_name) or 0
				if icon_name and icon_name ~= "" and hasDirectiveBoundary(rest_lower, len) then
					local glyph = resolveIconGlyph(icon_name)
					segment.icon = glyph ~= "" and glyph or ("[" .. icon_name:upper() .. "]")
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local color_hex = rest_lower:match("^#color([%x][%x][%x][%x][%x][%x])")
				local len = color_hex and (6 + #color_hex) or 0
				if color_hex and hasDirectiveBoundary(rest_lower, len) then
					segment.color = parseColorHex(color_hex) or segment.color
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local bg_hex = rest_lower:match("^#bg([%x][%x][%x][%x][%x][%x])")
				local len = bg_hex and (3 + #bg_hex) or 0
				if bg_hex and hasDirectiveBoundary(rest_lower, len) then
					segment.bg_color = parseColorHex(bg_hex) or segment.bg_color
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local alpha_digits = rest_lower:match("^#alpha(%d+)")
				local len = alpha_digits and (6 + #alpha_digits) or 0
				if alpha_digits and hasDirectiveBoundary(rest_lower, len) then
					local alpha = tonumber(alpha_digits) or 100
					segment.alpha = math.max(0, math.min(100, alpha)) / 100
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local indent_digits = rest_lower:match("^#indent([%-]?%d+)")
				local len = indent_digits and (7 + #indent_digits) or 0
				if indent_digits and hasDirectiveBoundary(rest_lower, len) then
					local indent = tonumber(indent_digits) or 0
					segment.indent = segment.indent + indent
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local pad_digits = rest_lower:match("^#pad([%-]?%d+)")
				local len = pad_digits and (4 + #pad_digits) or 0
				if pad_digits and hasDirectiveBoundary(rest_lower, len) then
					local indent = tonumber(pad_digits) or 0
					segment.indent = segment.indent + indent
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local tab_digits = rest_lower:match("^#tab(%d*)")
				local len = tab_digits and (4 + #tab_digits) or 0
				if tab_digits ~= nil and hasDirectiveBoundary(rest_lower, len) then
					local tabs = tonumber(tab_digits) or 1
					segment.indent = segment.indent + math.max(0, tabs) * 32
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local br_digits = rest_lower:match("^#br(%d*)")
				local len = br_digits and (3 + #br_digits) or 0
				if br_digits ~= nil and hasDirectiveBoundary(rest_lower, len) then
					local brn = tonumber(br_digits) or 1
					segment.extra_breaks = segment.extra_breaks + math.max(0, brn)
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
			if not consumed then
				local hr_hex = rest_lower:match("^#hr([%x]*)")
				local len = hr_hex and (3 + #hr_hex) or 0
				if hr_hex ~= nil and hasDirectiveBoundary(rest_lower, len) then
					segment.is_hr = true
					if #hr_hex == 6 then
						segment.hr_color = parseColorHex(hr_hex)
					end
					consumed = true
					consumed_len = len
					used_directive = true
				end
			end
		end
		if not consumed then
			break
		end
		rest = rest:sub(consumed_len + 1)
		rest = rest:gsub("^%s+", "")
	end

	if used_directive then
		segment.text = rest
	else
		segment.text = leading_ws .. rest
	end
	return segment
end
local function parseRichTextLines(text)
	text = tostring(text or "")
	if rich_cache.text == text and rich_cache.lines then
		return rich_cache.lines
	end

	local lines = {}
	for raw_line in (text .. "\n"):gmatch("(.-)\n") do
		local segment = parseRichSegment(raw_line)
		if segment.same_line and #lines > 0 then
			table.insert(lines[#lines], segment)
		else
			table.insert(lines, { segment })
		end
	end
	if #lines == 0 then
		lines[1] = {
			{ align = "left", same_line = false, color = nil, font_size = nil, icon = "", image = nil, text = "" },
		}
	end

	rich_cache.text = text
	rich_cache.lines = lines
	return lines
end

local function getU16LE(data, idx)
	local b1, b2 = data:byte(idx, idx + 1)
	if not b1 or not b2 then
		return nil
	end
	return b1 + b2 * 256
end

local function getU16BE(data, idx)
	local b1, b2 = data:byte(idx, idx + 1)
	if not b1 or not b2 then
		return nil
	end
	return b1 * 256 + b2
end

local function getU32LE(data, idx)
	local b1, b2, b3, b4 = data:byte(idx, idx + 3)
	if not b1 or not b2 or not b3 or not b4 then
		return nil
	end
	return b1 + b2 * 256 + b3 * 65536 + b4 * 16777216
end

local function getU32BE(data, idx)
	local b1, b2, b3, b4 = data:byte(idx, idx + 3)
	if not b1 or not b2 or not b3 or not b4 then
		return nil
	end
	return b1 * 16777216 + b2 * 65536 + b3 * 256 + b4
end

local function isJpegSOFMarker(marker)
	return marker == 0xC0
		or marker == 0xC1
		or marker == 0xC2
		or marker == 0xC3
		or marker == 0xC5
		or marker == 0xC6
		or marker == 0xC7
		or marker == 0xC9
		or marker == 0xCA
		or marker == 0xCB
		or marker == 0xCD
		or marker == 0xCE
		or marker == 0xCF
end

local function parseJpegSize(data)
	local len = #data
	if len < 4 or data:byte(1) ~= 0xFF or data:byte(2) ~= 0xD8 then
		return nil, nil
	end

	local i = 3
	while i <= len - 9 do
		local marker_start = data:byte(i)
		if marker_start ~= 0xFF then
			i = i + 1
		else
			local marker = data:byte(i + 1)
			while marker == 0xFF do
				i = i + 1
				marker = data:byte(i + 1)
			end
			if not marker then
				break
			end
			if marker == 0xD8 or marker == 0x01 then
				i = i + 2
			elseif marker >= 0xD0 and marker <= 0xD9 then
				i = i + 2
			else
				local seg_len = getU16BE(data, i + 2)
				if not seg_len or seg_len < 2 then
					break
				end
				if isJpegSOFMarker(marker) then
					local h = getU16BE(data, i + 5)
					local w = getU16BE(data, i + 7)
					if w and h and w > 0 and h > 0 then
						return w, h
					end
					return nil, nil
				end
				i = i + 2 + seg_len
			end
		end
	end
	return nil, nil
end

local function getImageNativeSize(path)
	path = normalizePath(path)
	local cached = image_size_cache[path]
	if cached ~= nil then
		if cached then
			return cached.w, cached.h
		end
		return nil, nil
	end

	local f = io.open(path, "rb")
	if not f then
		image_size_cache[path] = false
		return nil, nil
	end
	local data = f:read(262144) or ""
	f:close()
	if data == "" then
		image_size_cache[path] = false
		return nil, nil
	end

	local width, height = nil, nil
	if #data >= 24 and data:sub(1, 8) == "\137PNG\r\n\26\n" then
		width = getU32BE(data, 17)
		height = getU32BE(data, 21)
	elseif #data >= 10 and data:sub(1, 3) == "GIF" then
		width = getU16LE(data, 7)
		height = getU16LE(data, 9)
	elseif #data >= 26 and data:sub(1, 2) == "BM" then
		width = getU32LE(data, 19)
		height = getU32LE(data, 23)
		if height and height > 2147483647 then
			height = 4294967296 - height
		end
		if height then
			height = math.abs(height)
		end
	elseif #data >= 4 then
		width, height = parseJpegSize(data)
	end

	if width and height and width > 0 and height > 0 then
		image_size_cache[path] = { w = width, h = height }
		return width, height
	end

	image_size_cache[path] = false
	return nil, nil
end

local function startImageDownload(url, state)
	if not state then
		return
	end
	if type(downloadUrlToFile) ~= "function" or not dlstatus then
		state.status = "error"
		state.error = "download_unavailable"
		state.retry_at = os.clock() + 5
		return
	end

	local target = tostring(state.local_path or "")
	if target == "" then
		state.status = "error"
		state.error = "invalid_target"
		state.retry_at = os.clock() + 5
		return
	end

	local parent = target:match("^(.*)\\[^\\]+$")
	if parent and parent ~= "" then
		ensureDirectory(parent)
	end

	state.status = "loading"
	state.error = nil
	downloadUrlToFile(url, target, function(id, status, p1, p2)
		if status == dlstatus.STATUS_ENDDOWNLOADDATA then
			if fileExists(target) then
				state.status = "ready"
				state.error = nil
				state.retry_at = nil
				image_texture_cache[target] = nil
				image_size_cache[target] = nil
			else
				state.status = "error"
				state.error = "file_missing"
				state.retry_at = os.clock() + 5
			end
		elseif status == dlstatus.STATUS_ERROR then
			state.status = "error"
			state.error = "download_error"
			state.retry_at = os.clock() + 5
			os.remove(target)
		end
	end)
end

local function resolveImageFilePath(source)
	source = trimString(source)
	if source == "" then
		return nil, "empty"
	end

	if isHttpUrl(source) then
		ensureBasePath()
		local state = image_url_cache[source]
		if not state then
			state = {
				local_path = buildUrlImageCachePath(source),
				status = "idle",
				error = nil,
				retry_at = nil,
			}
			image_url_cache[source] = state
		end

		if fileExists(state.local_path) then
			state.status = "ready"
			state.error = nil
			state.retry_at = nil
		end

		if state.status ~= "ready" then
			local now = os.clock()
			if state.status ~= "loading" and (not state.retry_at or now >= state.retry_at) then
				startImageDownload(source, state)
			end
		end
		return state.local_path, state.status
	end

	local path = source
	if not isAbsolutePath(path) then
		local relative = sanitizeRelativeImagePath(path)
		if relative == "" then
			return nil, "missing"
		end
		path = paths.join(images_path, relative)
	end
	path = normalizePath(path)
	if fileExists(path) then
		return path, "ready"
	end
	return path, "missing"
end

local function getImageTexture(path)
	path = normalizePath(path)
	if path == "" then
		return nil
	end

	local now = os.clock()
	local cached = image_texture_cache[path]
	if cached and cached.texture then
		return cached.texture
	end
	if cached and cached.retry_at and now < cached.retry_at then
		return nil
	end

	local ok, texture = pcall(imgui.CreateTextureFromFile, path)
	if ok and texture ~= nil then
		image_texture_cache[path] = { texture = texture }
		return texture
	end

	image_texture_cache[path] = { texture = nil, retry_at = now + 1.0 }
	return nil
end

local function computeImageRenderSize(spec, native_w, native_h)
	spec = spec or {}
	local avail = imgui.GetContentRegionAvail()
	local avail_w = math.max(1, tonumber(avail.x) or 1)
	local avail_h = math.max(1, tonumber(avail.y) or 1)
	local region_min = imgui.GetWindowContentRegionMin()
	local region_max = imgui.GetWindowContentRegionMax()
	local full_width = tonumber(region_max.x) - tonumber(region_min.x)
	if full_width and full_width > 1 then
		avail_w = full_width
	end

	local req_w = spec.size_w
	local req_h = spec.size_h
	local width = req_w or native_w or math.max(64, math.min(avail_w, 256))
	local height = req_h or native_h or width

	if req_w == -1 and req_h == -1 then
		width = avail_w
		if native_w and native_h and native_w > 0 and native_h > 0 then
			height = width * native_h / native_w
		else
			height = avail_h
		end
	elseif req_w == -1 then
		if req_h and req_h > 0 and native_w and native_h and native_h > 0 then
			width = req_h * native_w / native_h
		else
			width = avail_w
		end
	elseif req_h == -1 then
		if req_w and req_w > 0 and native_w and native_h and native_w > 0 then
			height = req_w * native_h / native_w
		else
			height = avail_h
		end
	end

	return math.max(1, width or 1), math.max(1, height or 1)
end

local function resolveSegmentImageInfo(segment)
	local spec = segment and segment.image
	if type(spec) ~= "table" then
		return nil
	end

	local source = trimString(spec.source)
	if source == "" then
		return nil
	end

	local path, status = resolveImageFilePath(source)
	local native_w, native_h = nil, nil
	if path and status == "ready" then
		native_w, native_h = getImageNativeSize(path)
	end
	local width, height = computeImageRenderSize(spec, native_w, native_h)

	local texture = nil
	if path and status == "ready" then
		texture = getImageTexture(path)
	end

	local is_absolute = spec.pos_x ~= nil or spec.pos_y ~= nil
	return {
		status = status,
		path = path,
		texture = texture,
		width = width,
		height = height,
		pos_x = spec.pos_x,
		pos_y = spec.pos_y,
		is_absolute = is_absolute,
	}
end

local function withAlpha(color, alpha)
	if not color then
		return nil
	end
	alpha = alpha or 1
	return imgui.ImVec4(color.x, color.y, color.z, math.max(0, math.min(1, color.w * alpha)))
end
local function withSegmentVisualStyle(segment, fn)
	local alpha = segment.alpha or 1
	if segment.color then
		imgui.PushStyleColor(imgui.Col.Text, withAlpha(segment.color, alpha))
	end
	if alpha < 1 then
		imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
	end
	fn()
	if alpha < 1 then
		imgui.PopStyleVar()
	end
	if segment.color then
		imgui.PopStyleColor()
	end
end
local function withSegmentFontStyle(segment, fn)
	local font = segment.font_size and note_fonts[segment.font_size] or nil
	if font then
		imgui.PushFont(font)
	end
	fn()
	if font then
		imgui.PopFont()
	end
end
local function getCurrentWindowFontScale()
	local current_size = imgui.GetFontSize()
	if current_size <= 0 then
		return 1.0
	end
	local io = imgui.GetIO()
	local font = imgui.GetFont()
	local base_font_size = current_size
	local ok_base, base = pcall(function()
		return font.FontSize
	end)
	if ok_base and base and base > 0 then
		base_font_size = base
	end
	local font_scale = 1.0
	local ok_scale, scale = pcall(function()
		return font.Scale
	end)
	if ok_scale and scale and scale > 0 then
		font_scale = scale
	end
	local global_scale = (io and io.FontGlobalScale and io.FontGlobalScale > 0) and io.FontGlobalScale or 1.0
	local denom = base_font_size * font_scale * global_scale
	if denom <= 0 then
		return 1.0
	end
	local window_scale = current_size / denom
	if window_scale <= 0 then
		return 1.0
	end
	return window_scale
end
local function withWindowFontScaleRatio(ratio, fn)
	if not imgui.SetWindowFontScale or not ratio or ratio <= 0 or math.abs(ratio - 1.0) < 0.001 then
		fn()
		return
	end
	local prev_scale = getCurrentWindowFontScale()
	local target_scale = prev_scale * ratio
	if target_scale <= 0 then
		target_scale = prev_scale
	end
	imgui.SetWindowFontScale(target_scale)
	local ok, err = pcall(fn)
	imgui.SetWindowFontScale(prev_scale)
	if not ok then
		error(err, 0)
	end
end
local function withSegmentIconFontStyle(segment, fn)
	local size = segment.font_size
	if size and note_fonts[size] and note_fonts_with_icons[size] then
		imgui.PushFont(note_fonts[size])
		fn()
		imgui.PopFont()
		return
	end
	if size and size > 0 then
		local current_size = imgui.GetFontSize()
		if current_size > 0 then
			withWindowFontScaleRatio(size / current_size, fn)
			return
		end
	end
	fn()
end
local function buildSegmentParts(segment)
	local icon = tostring(segment.icon or "")
	local text = tostring(segment.text or "")
	if segment.transform == "upper" then
		text = text:upper()
	elseif segment.transform == "lower" then
		text = text:lower()
	end
	if segment.bullet then
		text = (text ~= "") and ("* " .. text) or "*"
	end
	return icon, text
end
local function calcSegmentMetrics(segment, icon_text, text)
	if segment.image then
		local image_info = resolveSegmentImageInfo(segment)
		if image_info then
			if image_info.is_absolute then
				return 0, 0
			end
			return image_info.width, image_info.height
		end
		return 0, 0
	end
	local width, line_height = 0, imgui.GetTextLineHeightWithSpacing()
	if icon_text ~= "" then
		withSegmentIconFontStyle(segment, function()
			width = width + imgui.CalcTextSize(icon_text).x
			line_height = math.max(line_height, imgui.GetTextLineHeightWithSpacing())
		end)
	end
	if text ~= "" then
		withSegmentFontStyle(segment, function()
			local prefix = icon_text ~= "" and " " or ""
			width = width + imgui.CalcTextSize(prefix .. text).x
			line_height = math.max(line_height, imgui.GetTextLineHeightWithSpacing())
		end)
	end
	return width, line_height
end
local function drawSegmentText(segment, icon_text, text)
	if segment.image then
		local image_info = resolveSegmentImageInfo(segment)
		if not image_info then
			imgui.TextUnformatted("")
			return
		end

		local draw_size = imgui.ImVec2(image_info.width, image_info.height)
		withSegmentVisualStyle(segment, function()
			if image_info.is_absolute then
				local cursor_pos = imgui.GetCursorPos()
				local window_pos = imgui.GetWindowPos()
				local x = tonumber(image_info.pos_x) or 0
				local y = tonumber(image_info.pos_y) or 0
				imgui.SetCursorScreenPos(imgui.ImVec2(window_pos.x + x, window_pos.y + y))
				if image_info.texture then
					imgui.Image(image_info.texture, draw_size)
				else
					imgui.Dummy(draw_size)
				end
				imgui.SetCursorPosX(cursor_pos.x)
				imgui.SetCursorPosY(cursor_pos.y)
			else
				if image_info.texture then
					imgui.Image(image_info.texture, draw_size)
				else
					imgui.Dummy(draw_size)
				end
			end
		end)
		return
	end

	local has_icon = icon_text ~= ""
	local has_text = text ~= ""
	withSegmentVisualStyle(segment, function()
		if has_icon then
			withSegmentIconFontStyle(segment, function()
				imgui.TextUnformatted(icon_text)
			end)
		end
		if has_text then
			if has_icon then
				imgui.SameLine(0, 0)
			end
			withSegmentFontStyle(segment, function()
				if has_icon then
					imgui.TextUnformatted(" " .. text)
				else
					imgui.TextUnformatted(text)
				end
			end)
		elseif not has_icon then
			imgui.TextUnformatted("")
		end
	end)
end
local function drawRichLine(segments)
	local line_start_x = imgui.GetCursorPosX()
	local line_start_y = imgui.GetCursorPosY()
	local avail = imgui.GetContentRegionAvail().x
	local spacing = imgui.GetStyle().ItemSpacing.x
	local next_left_x = line_start_x
	local line_height = imgui.GetTextLineHeightWithSpacing()
	local drawn = false
	local max_extra_breaks = 0
	local first = segments[1]

	if first and first.is_hr and #segments == 1 and (first.text or "") == "" and first.icon == "" then
		local screen = imgui.GetCursorScreenPos()
		local y = screen.y + line_height * 0.55
		local x1 = screen.x + math.max(0, first.indent or 0)
		local x2 = screen.x + avail
		local line_col = first.hr_color or first.color or imgui.GetStyle().Colors[imgui.Col.Separator]
		line_col = withAlpha(line_col, first.alpha or 1)
		imgui.GetWindowDrawList():AddLine(
			imgui.ImVec2(x1, y),
			imgui.ImVec2(x2, y),
			imgui.GetColorU32Vec4(line_col),
			1.0
		)
		max_extra_breaks = math.max(0, first.extra_breaks or 0)
		imgui.SetCursorPosY(line_start_y + line_height * (1 + max_extra_breaks))
		return
	end

	for _, segment in ipairs(segments) do
		max_extra_breaks = math.max(max_extra_breaks, segment.extra_breaks or 0)
		local icon_text, text = buildSegmentParts(segment)
		local is_absolute_image = segment.image and (segment.image.pos_x ~= nil or segment.image.pos_y ~= nil)
		local width, height = calcSegmentMetrics(segment, icon_text, text)
		if height > line_height then
			line_height = height
		end

		local indent = segment.indent or 0
		local target_x = math.max(next_left_x, line_start_x + indent)
		if segment.align == "center" then
			target_x = line_start_x + math.max((avail - width) * 0.5, 0) + indent
		elseif segment.align == "right" then
			target_x = line_start_x + math.max(avail - width - indent, 0)
		end

		if drawn and target_x < next_left_x then
			target_x = next_left_x
		end

		if drawn then
			imgui.SameLine(target_x, 0)
		else
			imgui.SetCursorPosX(target_x)
		end

		local start_screen = imgui.GetCursorScreenPos()
		if (not is_absolute_image) and segment.bg_color and (segment.image or icon_text ~= "" or text ~= "") then
			local bg = withAlpha(segment.bg_color, segment.alpha or 1)
			local pad_x, pad_y = 4, 1
			imgui.GetWindowDrawList():AddRectFilled(
				imgui.ImVec2(start_screen.x - pad_x, start_screen.y - pad_y),
				imgui.ImVec2(start_screen.x + width + pad_x, start_screen.y + height - 1 + pad_y),
				imgui.GetColorU32Vec4(bg),
				3.0
			)
		end
		drawSegmentText(segment, icon_text, text)
		drawn = true

		if segment.align == "left" and not is_absolute_image then
			-- TextUnformatted moves the cursor to the next line, so cursor X is unreliable
			-- for inline placement. Use the computed segment width from target_x instead.
			local x_after = target_x + width + spacing
			if x_after > next_left_x then
				next_left_x = x_after
			end
		end
	end

	if drawn then
		imgui.SetCursorPosY(line_start_y + line_height * (1 + max_extra_breaks))
	else
		imgui.TextUnformatted("")
	end
end

local function newEmptyRichSegment()
	return {
		align = "left",
		same_line = false,
		color = nil,
		bg_color = nil,
		hr_color = nil,
		alpha = 1,
		font_size = nil,
		indent = 0,
		icon = "",
		bullet = false,
		transform = nil,
		extra_breaks = 0,
		is_hr = false,
		image = nil,
		text = "",
	}
end

local function cloneImageSpec(spec)
	if type(spec) ~= "table" then
		return nil
	end
	return {
		source = tostring(spec.source or ""),
		size_w = spec.size_w,
		size_h = spec.size_h,
		pos_x = spec.pos_x,
		pos_y = spec.pos_y,
	}
end

local function cloneRichSegment(segment)
	return {
		align = segment.align or "left",
		same_line = false,
		color = segment.color,
		bg_color = segment.bg_color,
		hr_color = segment.hr_color,
		alpha = segment.alpha or 1,
		font_size = segment.font_size,
		indent = segment.indent or 0,
		icon = tostring(segment.icon or ""),
		bullet = segment.bullet and true or false,
		transform = segment.transform,
		extra_breaks = segment.extra_breaks or 0,
		is_hr = segment.is_hr and true or false,
		image = cloneImageSpec(segment.image),
		text = tostring(segment.text or ""),
	}
end

local function makeWrappedSegment(source, icon_text, text)
	local seg = cloneRichSegment(source)
	seg.icon = tostring(icon_text or "")
	seg.text = tostring(text or "")
	seg.bullet = false
	seg.transform = nil
	seg.extra_breaks = 0
	seg.image = nil
	return seg
end

local function takeWrappedTextChunk(segment, icon_text, text, max_width)
	text = tostring(text or "")
	if text == "" then
		return "", ""
	end
	max_width = math.max(1, tonumber(max_width) or 1)

	local best_end = 0
	local best_break_end = 0
	local i = 1
	while i <= #text do
		local byte = text:byte(i)
		local step = utf8StepFromByte(byte)
		local j = i + step - 1
		if j > #text then
			j = #text
		end

		local candidate = text:sub(1, j)
		local candidate_width = select(1, calcSegmentMetrics(segment, icon_text, candidate))
		if candidate_width <= max_width or best_end == 0 then
			best_end = j
			local ch = text:sub(i, j)
			if ch:match("[%s%.,;:%-!%?/%)]") then
				best_break_end = j
			end
			i = j + 1
		else
			break
		end
	end

	if best_end <= 0 then
		local first_step = utf8StepFromByte(text:byte(1))
		best_end = math.min(#text, first_step)
	end

	local cut = best_end
	if cut < #text and best_break_end > 0 then
		cut = best_break_end
	end

	local chunk = text:sub(1, cut)
	local rest = text:sub(cut + 1)
	if rest ~= "" then
		local trimmed_chunk = chunk:gsub("%s+$", "")
		if trimmed_chunk ~= "" then
			chunk = trimmed_chunk
		end
		rest = rest:gsub("^%s+", "")
	end

	if chunk == "" and rest ~= "" then
		local step = utf8StepFromByte(rest:byte(1))
		chunk = rest:sub(1, step)
		rest = rest:sub(step + 1)
	end

	return chunk, rest
end

local function wrapRichSegments(lines, avail)
	avail = math.max(1, tonumber(avail) or 1)
	local wrapped = {}
	local spacing = imgui.GetStyle().ItemSpacing.x

	for _, src_line in ipairs(lines) do
		local segments = src_line or {}
		if #segments == 0 then
			wrapped[#wrapped + 1] = { newEmptyRichSegment() }
			goto continue_line
		end

		local only_hr = false
		if #segments == 1 then
			local first = segments[1]
			if first and first.is_hr and (first.text or "") == "" and (first.icon or "") == "" then
				only_hr = true
			end
		end

		local has_image = false
		for _, segment in ipairs(segments) do
			if segment and segment.image then
				has_image = true
				break
			end
		end

		if only_hr or has_image then
			local passthrough = {}
			for i = 1, #segments do
				passthrough[i] = cloneRichSegment(segments[i])
			end
			wrapped[#wrapped + 1] = passthrough
			goto continue_line
		end

		local line_extra_breaks = 0
		for _, segment in ipairs(segments) do
			line_extra_breaks = math.max(line_extra_breaks, segment.extra_breaks or 0)
		end

		local produced_lines = {}
		local current_line = {}
		local current_width = 0

		local function flush_current_line()
			if #current_line == 0 then
				return
			end
			produced_lines[#produced_lines + 1] = current_line
			current_line = {}
			current_width = 0
		end

		local function push_single_piece_line(piece)
			produced_lines[#produced_lines + 1] = { piece }
		end

		local function push_piece(piece)
			local icon_text = tostring(piece.icon or "")
			local piece_text = tostring(piece.text or "")
			local piece_width = select(1, calcSegmentMetrics(piece, icon_text, piece_text))
			local spacing_before = (#current_line > 0) and spacing or 0
			current_line[#current_line + 1] = piece
			current_width = current_width + spacing_before + piece_width
		end

		for _, source in ipairs(segments) do
			local icon_full, text_full = buildSegmentParts(source)
			local rest_text = tostring(text_full or "")
			local use_icon = icon_full ~= ""
			local align = source.align or "left"

			if align ~= "left" then
				flush_current_line()
				if rest_text == "" then
					local piece = makeWrappedSegment(source, use_icon and icon_full or "", "")
					push_single_piece_line(piece)
				else
					while rest_text ~= "" do
						local icon_piece = use_icon and icon_full or ""
						local candidate = makeWrappedSegment(source, icon_piece, rest_text)
						local candidate_width = select(1, calcSegmentMetrics(candidate, icon_piece, rest_text))
						if candidate_width <= avail then
							push_single_piece_line(candidate)
							rest_text = ""
							use_icon = false
						else
							local chunk, rest = takeWrappedTextChunk(candidate, icon_piece, rest_text, avail)
							local piece = makeWrappedSegment(source, icon_piece, chunk)
							push_single_piece_line(piece)
							rest_text = rest
							use_icon = false
						end
					end
				end
				goto continue_segment
			end

			if rest_text == "" then
				local piece = makeWrappedSegment(source, use_icon and icon_full or "", "")
				local piece_width = select(1, calcSegmentMetrics(piece, piece.icon, piece.text))
				local spacing_before = (#current_line > 0) and spacing or 0
				if #current_line > 0 and (current_width + spacing_before + piece_width) > avail then
					flush_current_line()
				end
				push_piece(piece)
			else
				while rest_text ~= "" do
					local icon_piece = use_icon and icon_full or ""
					local candidate = makeWrappedSegment(source, icon_piece, rest_text)
					local candidate_width = select(1, calcSegmentMetrics(candidate, icon_piece, rest_text))
					local spacing_before = (#current_line > 0) and spacing or 0
					local fit_width = avail - current_width - spacing_before
					if fit_width < 1 then
						fit_width = 1
					end

					if candidate_width <= fit_width then
						push_piece(candidate)
						rest_text = ""
						use_icon = false
					else
						if #current_line > 0 then
							flush_current_line()
						else
							local chunk, rest = takeWrappedTextChunk(candidate, icon_piece, rest_text, avail)
							local piece = makeWrappedSegment(source, icon_piece, chunk)
							push_piece(piece)
							flush_current_line()
							rest_text = rest
							use_icon = false
						end
					end
				end
			end

			::continue_segment::
		end

		flush_current_line()
		if #produced_lines == 0 then
			produced_lines[1] = { newEmptyRichSegment() }
		end

		local last_line = produced_lines[#produced_lines]
		if last_line and #last_line > 0 then
			last_line[#last_line].extra_breaks = math.max(last_line[#last_line].extra_breaks or 0, line_extra_breaks)
		end

		for i = 1, #produced_lines do
			wrapped[#wrapped + 1] = produced_lines[i]
		end

		::continue_line::
	end

	if #wrapped == 0 then
		wrapped[1] = { newEmptyRichSegment() }
	end
	return wrapped
end

local function getWrappedRichLinesCached(text)
	text = tostring(text or "")
	local avail = math.max(1, math.floor(imgui.GetContentRegionAvail().x))
	local font_size = imgui.GetFontSize()
	if
		rich_wrapped_cache.text == text
		and rich_wrapped_cache.avail == avail
		and rich_wrapped_cache.font_size == font_size
		and rich_wrapped_cache.lines
	then
		return rich_wrapped_cache.lines
	end

	local lines = parseRichTextLines(text)
	rich_wrapped_cache.text = text
	rich_wrapped_cache.avail = avail
	rich_wrapped_cache.font_size = font_size
	rich_wrapped_cache.lines = wrapRichSegments(lines, avail)
	return rich_wrapped_cache.lines
end

local function showRichText(text)
	local lines = getWrappedRichLinesCached(text)
	for _, segments in ipairs(lines) do
		drawRichLine(segments)
	end
end
local function showNotePreviewText(text)
	text = applyTagVariables(tostring(text or ""))
	if text == "" then
		imgui.TextUnformatted("")
		return
	end
	if shouldUseRichMarkup(text) then
		showRichText(text)
	else
		showLongTextClipper(text)
	end
end
local function drawBbHelpPopup()
	if popup_bb_help[0] then
		imgui.OpenPopup("NotepadBbHelp")
		popup_bb_help[0] = false
	end
	if imgui.BeginPopupModal("NotepadBbHelp", nil, imgui.WindowFlags.AlwaysAutoResize) then
		imgui.TextUnformatted("BB-code и #img справка (кнопка копирует шаблон):")
		imgui.Separator()
		imgui.BeginChild("bb_help_items", imgui.ImVec2(650, 330), true)
		for i, entry in ipairs(bb_help_entries) do
			imgui.PushIDInt(9000 + i)
			if imgui.SmallButton("Копировать") then
				copyToClipboard(entry.code)
			end
			imgui.SameLine()
			imgui.TextUnformatted(entry.code)
			imgui.TextDisabled(escape_imgui_text(entry.desc))
			imgui.Separator()
			imgui.PopID()
		end
		imgui.EndChild()
		if imgui.Button("Закрыть##bbhelp") then
			imgui.CloseCurrentPopup()
		end
		local copy_all_label = "Скопировать всё##bbhelp_copy_all"
		local copy_all_size = imgui.CalcTextSize(copy_all_label)
		local copy_all_width = copy_all_size.x + imgui.GetStyle().FramePadding.x * 2
		local right_x = imgui.GetWindowContentRegionMax().x - copy_all_width
		imgui.SameLine()
		if right_x > imgui.GetCursorPosX() then
			imgui.SetCursorPosX(right_x)
		end
		if imgui.SmallButton(copy_all_label) then
			copyToClipboard(buildBbHelpExamplesText())
		end
		imgui.EndPopup()
	end
end

local function renderTree(node, prefix, filterRaw, draw_cb)
	prefix = prefix or ""
	for name, sub in pairs(node) do
		if name ~= "__is_file" then
			local id = prefix .. name
			if sub.__is_file then
				draw_cb(sub.__is_file, id)
			else
				if imgui.TreeNodeExStr(folder_icon .. " " .. u8(name), 0) then
					renderTree(sub, id .. "\\", filterRaw, draw_cb)
					imgui.TreePop()
				end
			end
		end
	end
end

-- ========== Глобальный поиск по заметкам и txt ===========
local function searchAllNotesAndTxt(filterRaw)
	if not filterRaw or filterRaw == "" then
		return {}
	end
	local results = {}
	for i, note in ipairs(notes) do
		if passFilter(note.title, filterRaw) then
			table.insert(
				results,
				{ idx = i, line_idx = 0, line = "", is_title = true, category = note.category, isJson = true }
			)
		end
		local line_num = 1
		for line in (note.text or ""):gmatch("([^\n]*)\n?") do
			if passFilter(line, filterRaw) then
				table.insert(
					results,
					{ idx = i, line_idx = line_num, line = line, is_title = false, category = note.category, isJson = true }
				)
			end
			line_num = line_num + 1
		end
	end
	for _, path in ipairs(tree_flat) do
		local name = path:match("([^\\]+)$")
		if passFilter(name, filterRaw) then
			table.insert(results, { path = path, line_idx = 0, line = "", is_title = true, isJson = false })
		end
		local content = txt_cache[path]
		if content then
			local line_num = 1
			for line in content:gmatch("([^\n]*)\n?") do
				if passFilter(line, filterRaw) then
					table.insert(
						results,
						{ path = path, line_idx = line_num, line = line, is_title = false, isJson = false }
					)
				end
				line_num = line_num + 1
			end
		end
	end
	return results
end

local function getSearchResultsCached(filterRaw)
	if not filterRaw or filterRaw == "" then
		return {}
	end
	local key = ("%s|%d|%d|%d"):format(string.lower(filterRaw), notes_revision, tree_revision, txt_cache_revision)
	if global_search_cache.key == key then
		return global_search_cache.results
	end
	local results = searchAllNotesAndTxt(filterRaw)
	global_search_cache.key = key
	global_search_cache.results = results
	return results
end

local function buildTxtTreeMatches(filterRaw)
	local matches = {}
	for _, path in ipairs(tree_flat) do
		local name = path:match("([^\\]+)$")
		if passFilter(name, filterRaw) then
			matches[path] = true
		else
			local content = txt_cache[path]
			if content == nil then
				readTxtFile(path, function() end)
				content = txt_cache[path]
			end
			if content and passFilter(name .. " " .. (content or ""), filterRaw) then
				matches[path] = true
			end
		end
	end
	return matches
end

local function getTxtTreeMatchesCached(filterRaw)
	if not filterRaw or filterRaw == "" then
		return nil
	end
	local key = ("%s|%d|%d"):format(string.lower(filterRaw), tree_revision, txt_cache_revision)
	if txt_tree_filter_cache.key == key and txt_tree_filter_cache.matches then
		return txt_tree_filter_cache.matches
	end
	local matches = buildTxtTreeMatches(filterRaw)
	txt_tree_filter_cache.key = key
	txt_tree_filter_cache.matches = matches
	return matches
end

local function drawCat(cat, arr, star)
	local tree_open = imgui.TreeNodeExStr(folder_icon .. " " .. cat .. (star and (" " .. star_icon) or ""), 0)
	if imgui.BeginPopupContextItem(cat .. "popupcat") then
		if imgui.MenuItemBool("Переименовать папку") then
			imgui.StrCopy(rename_buf, cat)
			popup_rename[0] = true
			current_folder = cat
		end
		if imgui.MenuItemBool("Удалить все заметки в папке") then
			local removed = false
			for i = #notes, 1, -1 do
				if notes[i].category == cat then
					removed = removeNoteAt(i, true) or removed
				end
			end
			if removed then
				saveNotes()
			end
		end
		if imgui.MenuItemBool("Создать вложенную папку") then
			imgui.StrCopy(newfolder_buf, "")
			popup_newfolder[0] = true
			current_folder = cat
		end
		imgui.EndPopup()
	end
	if tree_open then
		local clipper2 = imgui.ImGuiListClipper(#arr)
		while clipper2:Step() do
			for j = clipper2.DisplayStart + 1, clipper2.DisplayEnd do
				local v = arr[j]
				local isSel = selectedNode and selectedNode.isJson and selectedNode.idx == v.idx
				local alpha = fadeAlpha(v.idx, 0.13, isSel)
				imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
				if isSel then
					imgui.PushStyleColor(imgui.Col.Header, imgui.ImVec4(0.2, 0.52, 1, 1))
				end
				imgui.SetCursorPosY(imgui.GetCursorPosY() + 2)
				if
					imgui.Selectable(
						(star and star_icon or note_icon or "") .. " " .. v.note.title,
						isSel,
						0,
						imgui.ImVec2(0, imgui.GetFontSize() + 8)
					)
				then
					selectJsonNode(v.idx)
				end
				if imgui.BeginPopupContextItem("note" .. v.idx) then
					if imgui.MenuItemBool("Переименовать") then
						imgui.StrCopy(rename_buf, v.note.title)
						popup_rename[0] = true
						current_folder = nil
						selectJsonNode(v.idx)
					end
					if imgui.MenuItemBool("Удалить") then
						removeNoteAt(v.idx)
					end
					if imgui.MenuItemBool("Дублировать") then
						duplicateNote(v.idx)
					end
					if imgui.MenuItemBool("Экспорт") then
						exportNote(v.note)
					end
					if
						imgui.MenuItemBool(
							v.note._fav and "Убрать из избранного" or "В избранное"
						)
					then
						v.note._fav = not v.note._fav
						saveNotes()
					end
					imgui.EndPopup()
				end
				imgui.PopStyleVar()
				if isSel then
					imgui.PopStyleColor()
				end
			end
		end
		imgui.TreePop()
	end
end

local function drawLeftPanel()
	imgui.BeginChild("notepad_list", imgui.ImVec2(288, 0), true)
	if imgui.Button((folder_plus_icon or "+") .. " Создать папку") then
		imgui.StrCopy(newfolder_buf, "")
		popup_newfolder[0] = true
		current_folder = nil
	end
	imgui.SameLine()
	imgui.TextDisabled("ПКМ: папка — меню, заметка — меню")

	imgui.Text(escape_imgui_text((plus_icon or "+") .. " Новая заметка"))
	imgui.InputTextWithHint("##ntitle", "Заголовок", newNoteTitle, ffi.sizeof(newNoteTitle))
	imgui.InputTextWithHint("##ncat", "Категория/папка", newNoteCat, ffi.sizeof(newNoteCat))
	imgui.SameLine()
	if imgui.Button((plus_icon or "+") .. "##addnote", imgui.ImVec2(28, 22)) then
		createNote()
	end
	imgui.Spacing()
	imgui.Separator()
	imgui.PushItemWidth(-38)
	filter:Draw((search_icon or "") .. " Поиск...", 190)
	imgui.SameLine()
	if filter:IsActive() then
		if imgui.Button(close_icon or "[X]", imgui.ImVec2(25, 0)) then
			filter:Clear()
		end
	end
	imgui.PopItemWidth()
	local filterRaw = ffi.string(filter.InputBuf)
	local hasFilter = filterRaw ~= ""
	local txtFilterMatches = hasFilter and getTxtTreeMatchesCached(filterRaw) or nil
	imgui.Spacing()
	if star_icon ~= "" then
		imgui.Text(escape_imgui_text(star_icon .. " Последние"))
	else
		imgui.Text("Последние")
	end
	local fixed_height = imgui.GetFontSize() + 8
	local clipper = imgui.ImGuiListClipper(#history)
	while clipper:Step() do
		for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
			local idx = history[i]
			local note = notes[idx]
			if note and noteMatchesFilter(note, filterRaw) then
				local isSel = selectedNode and selectedNode.isJson and selectedNode.idx == idx
				local alpha = fadeAlpha(idx, 0.18, isSel)
				imgui.PushStyleVarFloat(imgui.StyleVar.Alpha, alpha)
				if isSel then
					imgui.PushStyleColor(imgui.Col.Header, imgui.ImVec4(0.16, 0.41, 0.78, 1))
				end
				imgui.SetCursorPosY(imgui.GetCursorPosY() + 2)
				if
					imgui.Selectable((note_icon or "") .. " " .. note.title, isSel, 0, imgui.ImVec2(0, fixed_height))
				then
					selectJsonNode(idx)
				end
				if imgui.BeginPopupContextItem("note" .. idx) then
					if imgui.MenuItemBool("Переименовать") then
						imgui.StrCopy(rename_buf, note.title)
						popup_rename[0] = true
						current_folder = nil
						selectJsonNode(idx)
					end
					if imgui.MenuItemBool("Удалить") then
						removeNoteAt(idx)
					end
					if imgui.MenuItemBool("Дублировать") then
						duplicateNote(idx)
					end
					if imgui.MenuItemBool("Экспорт") then
						exportNote(note)
					end
					if
						imgui.MenuItemBool(
							note._fav and "Убрать из избранного" or "В избранное"
						)
					then
						note._fav = not note._fav
						saveNotes()
					end
					imgui.EndPopup()
				end
				if isSel then
					imgui.PopStyleColor()
				end
				imgui.PopStyleVar()
			end
		end
	end
	imgui.Spacing()
	local categories, pinned = {}, {}
	for i, note in ipairs(notes) do
		if not noteMatchesFilter(note, filterRaw) then
			goto continue
		end
		local cat = note.category or "Без категории"
		if note._fav then
			pinned[cat] = pinned[cat] or {}
			table.insert(pinned[cat], { note = note, idx = i })
		else
			categories[cat] = categories[cat] or {}
			table.insert(categories[cat], { note = note, idx = i })
		end
		::continue::
	end
	for cat, arr in pairs(pinned) do
		drawCat(cat, arr, true)
	end
	for cat, arr in pairs(categories) do
		drawCat(cat, arr, false)
	end
	imgui.Spacing()
	imgui.Separator()
	imgui.Text(escape_imgui_text((folder_icon or "") .. " Импортированные .txt"))
	renderTree(tree_root, "", nil, function(path, id)
		local name = path:match("([^\\]+)$")
		local name = u8(name)
		if not hasFilter then
			local isSel = selectedNode and not selectedNode.isJson and selectedNode.path == path
			if imgui.Selectable((file_icon or "") .. " " .. name, isSel, 0, imgui.ImVec2(0, fixed_height)) then
				readTxtFile(path, function(content)
					selectTxtNode(path, content)
				end)
			end
			return
		end
		if not (txtFilterMatches and txtFilterMatches[path]) then
			return
		end
		local isSel = selectedNode and not selectedNode.isJson and selectedNode.path == path
		if imgui.Selectable((file_icon or "") .. " " .. name, isSel, 0, imgui.ImVec2(0, fixed_height)) then
			readTxtFile(path, function(content)
				selectTxtNode(path, content)
			end)
		end
	end)
	imgui.EndChild()
end

local function getDynamicTextHeight(txt)
	local lines = 0
	for _ in txt:gmatch("[^\n]*\n?") do
		lines = lines + 1
	end
	return math.max(120, math.min(800, lines * 22))
end

local function showCopyModeText(raw_text, id_suffix)
	local text = applyTagVariables(tostring(raw_text or ""))
	local required = math.max(4096, #text + 2048)
	if (not copy_mode_buf) or copy_mode_buf_size < required then
		copy_mode_buf_size = required
		copy_mode_buf = imgui.new.char[copy_mode_buf_size](text)
		copy_mode_buf_text = text
	elseif copy_mode_buf_text ~= text then
		imgui.StrCopy(copy_mode_buf, text)
		copy_mode_buf_text = text
	end
	imgui.TextDisabled("Режим копирования: выделите текст мышью и нажмите Ctrl+C.")
	imgui.InputTextMultiline(
		"##copy_mode_text_" .. tostring(id_suffix or ""),
		copy_mode_buf,
		copy_mode_buf_size,
		imgui.ImVec2(-1, getDynamicTextHeight(text)),
		imgui.InputTextFlags.ReadOnly
	)
end

local function buildCopyModeLines(raw_text)
	local text = applyTagVariables(tostring(raw_text or ""))
	if copy_mode_lines_cache.raw == text and copy_mode_lines_cache.lines then
		return copy_mode_lines_cache.lines, text
	end
	local groups = parseRichTextLines(text)
	local lines = {}
	for _, segments in ipairs(groups) do
		local row = {}
		local max_extra_breaks = 0
		for _, segment in ipairs(segments) do
			max_extra_breaks = math.max(max_extra_breaks, segment.extra_breaks or 0)
			if not segment.is_hr then
				local icon_text, part_text = buildSegmentParts(segment)
				local piece = ""
				if icon_text ~= "" and part_text ~= "" then
					piece = icon_text .. " " .. part_text
				elseif part_text ~= "" then
					piece = part_text
				elseif icon_text ~= "" then
					piece = icon_text
				end
				if piece ~= "" then
					piece = piece:gsub("%{[%x][%x][%x][%x][%x][%x]%}", "")
					row[#row + 1] = piece
				end
			end
		end
		lines[#lines + 1] = table.concat(row, " ")
		if max_extra_breaks > 0 then
			for _ = 1, max_extra_breaks do
				lines[#lines + 1] = ""
			end
		end
	end
	if #lines == 0 then
		lines[1] = ""
	end
	copy_mode_lines_cache.raw = text
	copy_mode_lines_cache.lines = lines
	return lines, text
end

local function showCopyModeClickLines(raw_text, id_suffix)
	local lines, text = buildCopyModeLines(raw_text)
	imgui.TextDisabled("Быстрое копирование: ЛКМ по строке копирует строку без BB-code.")
	imgui.BeginChild(
		"##copy_mode_lines_" .. tostring(id_suffix or ""),
		imgui.ImVec2(-1, getDynamicTextHeight(text)),
		true
	)
	local clipper = imgui.ImGuiListClipper(#lines)
	while clipper:Step() do
		for i = clipper.DisplayStart + 1, clipper.DisplayEnd do
			local line = lines[i] or ""
			imgui.PushIDInt(50000 + i)
			local label = (line ~= "") and line or " "
			if imgui.Selectable(label, false) then
				copyToClipboard(line)
			end
			imgui.PopID()
		end
	end
	imgui.EndChild()
end

local function drawCopyModeSwitch(id_suffix)
	if not copyModeEnabled[0] then
		return
	end
	imgui.SameLine()
	if mimgui_funcs and type(mimgui_funcs.ItemSelector) == "function" then
		imgui.PushIDStr("copy_mode_selector_" .. tostring(id_suffix or ""))
		mimgui_funcs.ItemSelector("", { "Выделение", "ЛКМ строки" }, copyModeKind, 80, false)
		imgui.PopID()
	else
		if imgui.SmallButton((copyModeKind[0] == 1 and "[Выделение]" or "Выделение") .. "##copymode_sel_" .. tostring(id_suffix)) then
			copyModeKind[0] = 1
		end
		imgui.SameLine()
		if imgui.SmallButton((copyModeKind[0] == 2 and "[ЛКМ строки]" or "ЛКМ строки") .. "##copymode_line_" .. tostring(id_suffix)) then
			copyModeKind[0] = 2
		end
	end
end

local function showCopyModeView(raw_text, id_suffix)
	if copyModeKind[0] == 2 then
		showCopyModeClickLines(raw_text, id_suffix)
	else
		showCopyModeText(raw_text, id_suffix)
	end
end

local function drawSearchResults(results)
	imgui.BeginChild("notepad_search", imgui.ImVec2(0, 0), true)
	imgui.TextDisabled(("Найдено: %d совпадений"):format(#results))
	imgui.Separator()
	for i, res in ipairs(results) do
		if res.isJson then
			local note = notes[res.idx]
			local cat = note and (note.category or "Без категории") or ""
			local title = note and note.title or "?"
			local title = u8(title)
			imgui.PushIDInt(i)
			if res.is_title then
				if imgui.Selectable(string.format("%s  [Заголовок]  [%s]", title, cat), false) then
					selectJsonNode(res.idx, 0)
				end
			else
				local label = string.format("%s [%s]  строка %d:  %s", title, cat, res.line_idx, res.line)
				if imgui.Selectable(label, false) then
					copyToClipboard(res.line)
					selectJsonNode(res.idx, res.line_idx)
				end
			end
			imgui.PopID()
		else
			local path = res.path
			local name = path:match("([^\\]+)$")
			local name = u8(name)
			local cat = path:match("^(.-)[^\\]+$") or ""
			imgui.PushIDInt(i)
			if res.is_title then
				if imgui.Selectable(string.format("%s [файл: %s]", name, cat), false) then
					readTxtFile(path, function(content)
						selectTxtNode(path, content, 0)
					end)
				end
			else
				local label = string.format("%s (строка %d): %s", name, res.line_idx, res.line)
				if imgui.Selectable(label, false) then
					copyToClipboard(res.line)
					readTxtFile(path, function(content)
						selectTxtNode(path, content, res.line_idx)
					end)
				end
			end
			imgui.PopID()
		end
	end
	imgui.EndChild()
end

local function drawRightPanel()
	imgui.BeginChild("notepad_content", imgui.ImVec2(0, 0), true)
	local filterRaw = ffi.string(filter.InputBuf)
	if filterRaw ~= "" then
		local search_results = getSearchResultsCached(filterRaw)
		drawSearchResults(search_results)
	elseif not selectedNode then
		imgui.TextColored(imgui.ImVec4(0.6, 0.7, 0.9, 1), "Выберите или создайте заметку.")
	elseif selectedNode.isJson then
		local idx = selectedNode.idx
		local note = notes[idx]
		if note then
			addToHistory(idx)
			imgui.Text(escape_imgui_text(note.title .. "  "))
			imgui.SameLine()
			if note._fav then
				imgui.TextColored(imgui.ImVec4(1, 1, 0.1, 1), escape_imgui_text(star_icon))
				imgui.SameLine()
			end
			if imgui.Button(star_icon .. (note._fav and " Открепить" or " Закрепить")) then
				note._fav = not note._fav
				saveNotes()
			end
			imgui.SameLine()
			if imgui.Button(exp_icon .. " Экспорт") then
				exportNote(note)
			end
			imgui.SameLine()
			imgui.Checkbox("Режим копирования##note_copy_mode", copyModeEnabled)
			drawCopyModeSwitch("json")
			imgui.SameLine()
			if imgui.Button((arrows_icon or "<->") .. " Переместить") then
				imgui.StrCopy(move_cat_buf, note.category or "")
				popup_move[0] = true
				imgui.OpenPopup("moveNote")
			end
			imgui.Spacing()
			if not editingMode[0] then
				imgui.Separator()
				if scrollToLine and scrollToLine > 0 then
					for i = 1, scrollToLine - 1 do
						imgui.TextUnformatted("")
					end
					scrollToLine = nil
				end
				if copyModeEnabled[0] then
					showCopyModeView(note.text or "", "json")
				else
					showNotePreviewText(note.text or "")
				end
				imgui.Spacing()
				imgui.TextDisabled(("Символов: %d"):format(#(note.text or "")))
				local dateInfo = os.date("Создано: %d.%m.%Y %H:%M", note._ctime)
					.. " | "
					.. os.date("Изменено: %d.%m.%Y %H:%M", note._mtime)
				imgui.SameLine()
				local rightX = imgui.GetWindowContentRegionMax().x - imgui.CalcTextSize(dateInfo).x
				if rightX > imgui.GetCursorPosX() then
					imgui.SetCursorPosX(rightX)
				end
				imgui.TextDisabled(dateInfo)
				if imgui.Button((edit_icon or "") .. " Редактировать") then
					editingMode[0] = true
					editingBufSize = math.max(4096, #(note.text or "") + 2048)
					editingText = imgui.new.char[editingBufSize](note.text or "")
				end
			else
				imgui.Separator()
				local savePressed = imgui.Button((save_icon or "") .. " Сохранить   (Ctrl+S)")
					or (imgui.IsKeyDown(0x11) and imgui.IsKeyPressed(0x53))
				if savePressed then
					local len = #ffi.string(editingText)
					if len > 65535 then
						popup_warning[0] = true
						imgui.OpenPopup("longTextWarn")
					else
						note.text = ffi.string(editingText)
						note._mtime = os.time()
						saveNotes()
						editingMode[0] = false
					end
				end
				imgui.SameLine()
				if imgui.Button((cancel_icon or "") .. " Отмена") then
					editingMode[0] = false
				end
				imgui.SameLine()
				if imgui.Button((delete_icon or "") .. " Удалить") then
					removeNoteAt(idx)
					return
				end
				imgui.SameLine()
				if imgui.Button("Помощь##bbhelp_edit_note") then
					popup_bb_help[0] = true
				end
				imgui.InputTextMultiline(
					"##editnotetext",
					editingText,
					editingBufSize,
					imgui.ImVec2(-1, getDynamicTextHeight(ffi.string(editingText)))
				)
				imgui.TextDisabled(("Символов: %d"):format(#ffi.string(editingText)))
			end
		else
			selectedNode = nil
			editingMode[0] = false
		end
	else
		if not selectedNode.path then
			selectedNode = nil
			editingMode[0] = false
			imgui.TextColored(imgui.ImVec4(0.6, 0.7, 0.9, 1), "Выберите или создайте заметку.")
		else
			local name = selectedNode.path:match("([^\\]+)$")
			local name = u8(name)
			local content = editingText and ffi.string(editingText) or imported_original_text
			if not editingText then
				editingBufSize = math.max(4096, #content + 2048)
				editingText = imgui.new.char[editingBufSize](content)
			end
			imgui.Text(escape_imgui_text(name))
			imgui.SameLine()
			if imgui.Button(exp_icon .. " Экспорт") then
				exportNote({ title = name, text = content })
			end
			imgui.SameLine()
			imgui.Checkbox("Режим копирования##txt_copy_mode", copyModeEnabled)
			drawCopyModeSwitch("txt")
			imgui.Separator()
			if not editingMode[0] then
				if scrollToLine and scrollToLine > 0 then
					for i = 1, scrollToLine - 1 do
						imgui.TextUnformatted("")
					end
					scrollToLine = nil
				end
				if copyModeEnabled[0] then
					showCopyModeView(content, "txt")
				else
					showNotePreviewText(content)
				end
				imgui.Spacing()
				imgui.TextDisabled(("Символов: %d"):format(#content))
				if imgui.Button((edit_icon or "") .. " Редактировать") then
					editingMode[0] = true
				end
			else
				imgui.Separator()
				if imgui.Button((save_icon or "") .. " Сохранить как новую") then
					table.insert(
						notes,
						{
							title = name,
							category = "Импорт",
							text = content,
							_fav = false,
							_ctime = os.time(),
							_mtime = os.time(),
						}
					)
					saveNotes()
					editingMode[0] = false
				end
				imgui.SameLine()
				if imgui.Button((cancel_icon or "") .. " Отмена") then
					editingMode[0] = false
					editingBufSize = math.max(4096, #imported_original_text + 2048)
					editingText = imgui.new.char[editingBufSize](imported_original_text)
				end
				imgui.SameLine()
				if imgui.Button("Помощь##bbhelp_edit_import") then
					popup_bb_help[0] = true
				end
				imgui.InputTextMultiline(
					"##editimptxt",
					editingText,
					editingBufSize,
					imgui.ImVec2(-1, getDynamicTextHeight(ffi.string(editingText)))
				)
				imgui.TextDisabled(("Символов: %d"):format(#ffi.string(editingText)))
			end
		end
	end

	drawBbHelpPopup()

	if imgui.BeginPopup("moveNote") then
		imgui.InputTextWithHint("##movecat", "Новая категория", move_cat_buf, ffi.sizeof(move_cat_buf))
		if imgui.Button("OK##movecat") then
			local idx = selectedNode and selectedNode.idx
			if idx and notes[idx] then
				notes[idx].category = ffi.string(move_cat_buf)
				saveNotes()
			end
			imgui.CloseCurrentPopup()
			popup_move[0] = false
		end
		imgui.SameLine()
		if imgui.Button("Отмена##movecat") then
			imgui.CloseCurrentPopup()
			popup_move[0] = false
		end
		imgui.EndPopup()
	end
	if imgui.BeginPopup("longTextWarn") then
		imgui.TextColored(
			imgui.ImVec4(1, 0.45, 0.2, 1),
			"Слишком длинный текст!\nОграничение: 65535 символов."
		)
		if imgui.Button("OK##longwarn") then
			imgui.CloseCurrentPopup()
			popup_warning[0] = false
		end
		imgui.EndPopup()
	end
	if popup_rename[0] then
		imgui.OpenPopup("RenamePopup")
		popup_rename[0] = false
	end
	if imgui.BeginPopupModal("RenamePopup", nil, imgui.WindowFlags.AlwaysAutoResize) then
		imgui.InputTextWithHint("##rename", "Новое имя", rename_buf, ffi.sizeof(rename_buf))
		if imgui.Button("OK##rename") then
			local new_name = ffi.string(rename_buf)
			if current_folder then
				for i, note in ipairs(notes) do
					if note.category == current_folder then
						note.category = new_name
					end
				end
			else
				if selectedNode and selectedNode.idx and notes[selectedNode.idx] then
					notes[selectedNode.idx].title = new_name
				end
			end
			saveNotes()
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##rename") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
	if popup_newfolder[0] then
		imgui.OpenPopup("NewFolderPopup")
		popup_newfolder[0] = false
	end
	if imgui.BeginPopupModal("NewFolderPopup", nil, imgui.WindowFlags.AlwaysAutoResize) then
		imgui.InputTextWithHint("##newfolder", "Имя папки", newfolder_buf, ffi.sizeof(newfolder_buf))
		if imgui.Button("OK##newfolder") then
			local new_cat = ffi.string(newfolder_buf)
			if current_folder then
				new_cat = current_folder .. "/" .. new_cat
			end
			table.insert(
				notes,
				{
					title = "Новая заметка",
					category = new_cat,
					text = "",
					_fav = false,
					_ctime = os.time(),
					_mtime = os.time(),
				}
			)
			saveNotes()
			imgui.CloseCurrentPopup()
		end
		imgui.SameLine()
		if imgui.Button("Отмена##newfolder") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
	imgui.EndChild()
end

function module.drawNotepadPanel()
	imgui.BeginChild("notepad_main", imgui.ImVec2(0, 0), false)
	drawLeftPanel()
	imgui.SameLine()
	drawRightPanel()
	imgui.EndChild()
	if imgui.BeginPopup("Дубликат") then
		imgui.Text("Заметка с таким заголовком уже есть!")
		if imgui.Button("OK") then
			imgui.CloseCurrentPopup()
		end
		imgui.EndPopup()
	end
end

local _initSub = imgui.OnInitialize(function()
	ensureBasePath()
	initNoteFonts()
	loadNotes()
	loadTreeFromFiles()
end)

function module.attachModules(mod)
	syncDependencies(mod)
	config_manager = mod.config_manager
	event_bus = mod.event_bus
	if event_bus then
		event_bus.offByOwner("notepad")
	end
	if config_manager then
		notes = config_manager.register("notepad", {
			path = JSON_PATH_REL,
			defaults = {},
		})
		if type(notes) ~= "table" then
			notes = {}
		end
		bump_notes_revision()
	end
end

function module.onTerminate()
	if _initSub and type(_initSub.Unsubscribe) == "function" then
		pcall(_initSub.Unsubscribe, _initSub)
		_initSub = nil
	end
	if event_bus then
		event_bus.offByOwner("notepad")
	end
end

return module
