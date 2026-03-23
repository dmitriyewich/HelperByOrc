local module = {}

local fallback_code = "ru"
local current_code = fallback_code
local generation = 0

local supported_languages = {
	{ code = "ru", label = "Русский" },
	{ code = "en", label = "English" },
}

local translations = {
	ru = require("language.ru"),
	en = require("language.en"),
}

local supported_lookup = {}
for i = 1, #supported_languages do
	local item = supported_languages[i]
	supported_lookup[item.code] = item.label
end

local function normalize_code(code)
	code = tostring(code or ""):lower()
	if translations[code] then
		return code
	end
	return fallback_code
end

local function get_raw_value(path, code)
	path = tostring(path or "")
	if path == "" then
		return nil
	end

	local selected_code = normalize_code(code or current_code)
	local selected_translations = translations[selected_code]
	local fallback_translations = translations[fallback_code]
	local value = selected_translations and selected_translations[path] or nil
	if value == nil or value == "" then
		value = fallback_translations and fallback_translations[path] or nil
	end
	return value
end

local function apply_params(text, params)
	if type(text) ~= "string" or type(params) ~= "table" or text == "" then
		return text
	end

	return (text:gsub("{([%w_%.%-]+)}", function(key)
		local value = params[key]
		if value == nil then
			return "{" .. key .. "}"
		end
		return tostring(value)
	end))
end

function module.normalizeCode(code)
	return normalize_code(code)
end

function module.getDefaultCode()
	return fallback_code
end

function module.getLanguage()
	return current_code
end

function module.getGeneration()
	return generation
end

function module.setLanguage(code)
	local normalized = normalize_code(code)
	if current_code ~= normalized then
		current_code = normalized
		generation = generation + 1
	end
	return current_code
end

function module.getSupportedLanguages()
	return supported_languages
end

function module.getLanguageLabel(code)
	code = normalize_code(code)
	return supported_lookup[code] or code
end

function module.getValue(path, code)
	return get_raw_value(path, code)
end

function module.getTable(path, code)
	local value = get_raw_value(path, code)
	if type(value) == "table" then
		return value
	end
	return {}
end

function module.getText(path, params, code)
	local value = get_raw_value(path, code)
	if type(value) ~= "string" or value == "" then
		value = tostring(path or "")
	end
	return apply_params(value, params)
end

module.t = module.getText

return module
