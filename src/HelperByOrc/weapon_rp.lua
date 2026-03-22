local language = require("language")
local function L(key, params)
	return language.getText(key, params)
end

local M = {}
local imgui = require("mimgui")
local ffi = require("ffi")
local encoding = require("encoding")
local paths = require("HelperByOrc.paths")
encoding.default = "CP1251"
local u8 = encoding.UTF8

-- ===== FFI: иконки оружия напрямую из игры =====
pcall(ffi.cdef, [[
	typedef unsigned char RwUInt8;
	typedef int RwInt32;
	typedef short RwInt16;
	typedef struct RwRaster RwRaster;
	struct RwRaster {
		struct RwRaster*            parent;
		RwUInt8*                    cpPixels;
		RwUInt8*                    palette;
		RwInt32                     width, height, depth;
		RwInt32                     stride;
		RwInt16                     nOffsetX, nOffsetY;
		RwUInt8                     cType;
		RwUInt8                     cFlags;
		RwUInt8                     privateFlags;
		RwUInt8                     cFormat;
		RwUInt8*                    originalPixels;
		RwInt32                     originalWidth;
		RwInt32                     originalHeight;
		RwInt32                     originalStride;
		void*                       texture_ptr;
	};
	typedef struct RwLLLink RwLLLink;
	struct RwLLLink { void *next; void *prev; };
	typedef struct RwLinkList RwLinkList;
	struct RwLinkList { struct RwLLLink link; };
	typedef struct RwObject RwObject;
	struct RwObject {
		char type; char subType; char flags; char privateFlags;
		struct RwFrame *parent;
	};
	typedef struct RwTexDictionary RwTexDictionary;
	struct RwTexDictionary {
		RwObject object;
		RwLinkList texturesInDict;
		RwLLLink lInInstance;
	};
	typedef struct CBaseModelInfo_vtbl CBaseModelInfo_vtbl;
	struct CBaseModelInfo_vtbl {
		void* destructor;
		void* AsAtomicModelInfoPtr;
		void* AsDamageAtomicModelInfoPtr;
		void* AsLodAtomicModelInfoPtr;
		char(__thiscall* GetModelType)(struct CBaseModelInfo*);
	};
	typedef struct CBaseModelInfo CBaseModelInfo;
	struct CBaseModelInfo {
		CBaseModelInfo_vtbl* vtbl;
		unsigned int m_dwKey;
		short m_wUsageCount;
		short m_wTxdIndex;
		char m_nAlpha; char m_n2dfxCount;
		short m_w2dfxIndex; short m_wObjectInfoIndex;
		unsigned short m_nMdlFlags;
		struct CColModel* m_pColModel;
		float m_fDrawDistance;
		struct RpClump* m_pRwObject;
	};
	typedef struct TxdDef TxdDef;
	struct TxdDef {
		RwTexDictionary *m_pRwDictionary;
		unsigned short m_wRefsCount;
		short m_wParentIndex;
		unsigned int m_hash;
	};
	typedef struct CPool CPool;
	struct CPool {
		TxdDef* m_pObjects;
		uint8_t* m_byteMap;
		int m_nSize; int top;
		char m_bOwnsAllocations; char bLocked; short _pad;
	};
	typedef struct RwTexture RwTexture;
	struct RwTexture { RwRaster* raster; };
	typedef struct CSprite2d CSprite2d;
	struct CSprite2d { RwTexture* m_pTexture; };
	typedef struct CWeaponInfo CWeaponInfo;
	struct CWeaponInfo {
		int m_eFireType;
		float targetRange; float m_fWeaponRange;
		int dwModelId1; int dwModelId2;
		int nSlot; int m_nFlags; int AssocGroupId;
		short ammoClip; short damage;
		float* fireOffset;
		int skillLevel; int reqStatLevelToGetThisWeaponSkilLevel;
		float m_fAccuracy; float moveSpeed;
		float animLoopStart; float animLoopEnd; int animLoopFire;
		float animLoop2Start; float animLoop2End; int animLoop2Fire;
		float breakoutTime; float speed; int radius;
		float lifespan; float spread;
		char AssocGroupId2; char field_6D; char baseCombo; char m_nNumCombos;
	};
]])
local _wicon_GetWeaponInfo       = ffi.cast("CWeaponInfo*(__cdecl*)(uint8_t, uint8_t)", 0x743C60)
local _wicon_AppendStringToKey   = ffi.cast("unsigned int(__cdecl*)(unsigned int, char*)", 0x53CF70)
local _wicon_FindHashNamedTexture = ffi.cast("RwTexture*(__cdecl*)(RwTexDictionary*, unsigned int)", 0x734E50)

local function getWeaponIconTexture(nWeaponModelId)
	local ok, result = pcall(function()
		local pTexture = ffi.new("RwTexDictionary*")
		local pModelInfo = ffi.new("CBaseModelInfo*")
		local pWeaponInfo = _wicon_GetWeaponInfo(nWeaponModelId, 1)
		if pWeaponInfo.dwModelId1 > 0 then
			pModelInfo = ffi.cast("CBaseModelInfo**", 0xA9B0C8)[pWeaponInfo.dwModelId1]
			local nTxdIndex = pModelInfo.m_wTxdIndex
			local pTxdPool = ffi.cast("CPool**", 0xC8800C)[0]
			if ffi.cast("uint8_t", pTxdPool.m_byteMap + nTxdIndex) >= 0 then
				pTexture = pTxdPool.m_pObjects[nTxdIndex].m_pRwDictionary
			end
			if pTexture ~= nil then
				local nAppended = _wicon_AppendStringToKey(pModelInfo.m_dwKey, ffi.cast("char*", "ICON"))
				local texture = _wicon_FindHashNamedTexture(pTexture, nAppended)
				if texture ~= nil then
					return texture.raster.texture_ptr
				end
			end
		else
			local fistSprite = ffi.cast("CSprite2d*", 0xBAB1FC)[0]
			return fistSprite.m_pTexture.raster.texture_ptr
		end
		return nil
	end)
	if ok then return result end
	return nil
end

-- ===== Кеш иконок и предзагрузка =====
-- Режимы weapon_icon_mode:
--   0 = стандартный файл (drawWeaponZoom, всегда работает)
--   2 = FFI + предзагрузка, держать модели — указатели стабильны
--   3 = FFI + предзагрузка, освободить модели — экономия памяти, но TXD может выгрузиться
-- При промахе кеша в режимах 2/3 → фоллбэк на стандартный файл (для неизвестных id — кулак)

local weapon_icon_cache = {}  -- [weapon_id] -> texture_ptr

local icon_preload_status = {
	running = false,
	done    = false,
	loaded  = 0,
	failed  = 0,
	total   = 0,
	thr     = nil,
}
-- Флаг единоразовой авто-предзагрузки при первом открытии вкладки «Оружие».
-- Сбрасывается при hot-reload вместе с остальным состоянием модуля.
local icon_auto_preload_triggered = false

local function clear_icon_cache()
	weapon_icon_cache = {}
	icon_preload_status.running = false
	icon_preload_status.done    = false
	icon_preload_status.loaded  = 0
	icon_preload_status.failed  = 0
	icon_preload_status.total   = 0
	if icon_preload_status.thr and icon_preload_status.thr:status() ~= "dead" then
		icon_preload_status.thr:terminate()
	end
	icon_preload_status.thr = nil
end

local function start_preload_weapon_icons(keep_models)
	if icon_preload_status.running then return end
	clear_icon_cache()
	icon_preload_status.running = true

	icon_preload_status.thr = lua_thread.create(function()
		local entries = {}
		for id, _ in pairs(M.config.weapons) do
			local ok, info = pcall(function() return _wicon_GetWeaponInfo(id, 1) end)
			if ok and info then
				local mid = info.dwModelId1
				table.insert(entries, { wid = id, mid = mid })
				if mid > 0 then
					requestModel(mid)
				end
			end
		end
		icon_preload_status.total = #entries

		loadAllModelsNow()
		wait(0)

		for _, e in ipairs(entries) do
			local tex = getWeaponIconTexture(e.wid)
			if tex ~= nil then
				weapon_icon_cache[e.wid] = tex
				icon_preload_status.loaded = icon_preload_status.loaded + 1
			else
				icon_preload_status.failed = icon_preload_status.failed + 1
			end
			if not keep_models and e.mid > 0 then
				markModelAsNoLongerNeeded(e.mid)
			end
		end

		icon_preload_status.running = false
		icon_preload_status.done    = true
	end)
end

-- Инициализация иконок Font Awesome через mimgui_funcs (если доступно)
local ICON_SAVE = ""
local function init_icons()
	if mimgui_funcs and type(mimgui_funcs.ensureFontAwesomeSolidMerged) == "function" then
		mimgui_funcs.ensureFontAwesomeSolidMerged()
	end
	local ok_fa, fa = pcall(require, "fAwesome7")
	ICON_SAVE = (ok_fa and fa and fa.ICON and fa.ICON.FLOPPY_DISK) or "\xef\x83\x87"
end

local funcs
local mimgui_funcs
local has_sprites = false
local tooltip
local ui_state = {
	dirty = false,
	last_rp_line = "",
	weapon_sort_mode = 0, -- 0=ID, 1=Имя, 2=Вкл сначала, 3=Кастом сначала
	sort_ascending = true,
	filter_enabled_only = false,
	filter_custom_only = false,
	selected_weapon_id = nil,
	scroll_to_weapon_id = nil,
	scroll_highlight_id = nil,
	scroll_highlight_time = 0,
	popup_weapon_id = nil,
	popup_focus_weapon_id = nil,
	current_tab = 0, -- 0=Основное, 1=Слова, 2=Оружие, 3=Расширенное
	debounce_save_time = 0,
}

local function syncDependencies(mod)
	mod = mod or {}
	funcs = mod.funcs or funcs or require("HelperByOrc.funcs")
	mimgui_funcs = mod.mimgui_funcs or mimgui_funcs or require("HelperByOrc.mimgui_funcs")
	has_sprites = type(mimgui_funcs) == "table" and mimgui_funcs.drawWeaponZoom ~= nil
	tooltip = mimgui_funcs.imgui_hover_tooltip_safe
	init_icons()
end

syncDependencies()

-- ===== базовая конфигурация =====
M.config = {
	-- детектор
	tick_ms = 80,
	stable_need = 3,
	cooldown_frames = 10,

	-- режимы
	auto_mode = 0, -- 0 = авто /me, 1 = по ПКМ
	change_as_two_lines = false, -- смена всегда в одну строку (одна /me)
	ignore_knuckles = true,
	weapon_icon_mode = 0, -- 0=стандарт файл, 2=FFI+предзагрузка держать, 3=FFI+предзагрузка освободить

	-- вывод
	prefix = "/me",
	min_me_gap_ms = 1000,
	max_len = 96,

	-- стиль фраз
	flavor_level = 2, -- 1 сдержанно, 2 поярче, 3 разнообразнее

	-- общий набор наречий и соединителей (для всех оружий)
	adverbs_show = {
		L("weapon_rp.text.text"),
		L("weapon_rp.text.text_1"),
		L("weapon_rp.text.text_2"),
		L("weapon_rp.text.text_3"),
		L("weapon_rp.text.text_4"),
	},
	adverbs_hide = {
		L("weapon_rp.text.text_5"),
		L("weapon_rp.text.text_6"),
		L("weapon_rp.text.text_7"),
		L("weapon_rp.text.text_8"),
		L("weapon_rp.text.text_9"),
	},
	connectors_full = {
		L("weapon_rp.text.text_10"),
		L("weapon_rp.text.text_11"),
		L("weapon_rp.text.text_12"),
		L("weapon_rp.text.text_13"),
	},
	connectors_short = { L("weapon_rp.text.text_10") },

	-- отправитель (nil -> sampSendChat)
	sender = nil,

	-- единая таблица оружий
	weapons = {
		-- ближний бой
		[1] = { name = L("weapon_rp.text.text_14"), short = L("weapon_rp.text.text_14") },
		[2] = {
			name = L("weapon_rp.text.text_15"),
			short = L("weapon_rp.text.text_16"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[3] = {
			name = L("weapon_rp.text.text_19"),
			short = L("weapon_rp.text.text_19"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[4] = { name = L("weapon_rp.text.text_20"), short = L("weapon_rp.text.text_20"), from = L("weapon_rp.text.text_21"), to = L("weapon_rp.text.text_22") },
		[5] = {
			name = L("weapon_rp.text.text_23"),
			short = L("weapon_rp.text.text_24"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[6] = {
			name = L("weapon_rp.text.text_25"),
			short = L("weapon_rp.text.text_25"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[7] = { name = L("weapon_rp.text.text_26"), short = L("weapon_rp.text.text_26"), from = L("weapon_rp.text.text_17"), to = L("weapon_rp.text.text_18") },
		[8] = {
			name = L("weapon_rp.text.text_27"),
			short = L("weapon_rp.text.text_27"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[9] = {
			name = L("weapon_rp.text.text_28"),
			short = L("weapon_rp.text.text_28"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[10] = { name = L("weapon_rp.text.text_29"), short = L("weapon_rp.text.text_30") },
		[11] = { name = L("weapon_rp.text.text_30"), short = L("weapon_rp.text.text_30") },
		[12] = { name = L("weapon_rp.text.text_31"), short = L("weapon_rp.text.text_31") },
		[13] = { name = L("weapon_rp.text.text_32"), short = L("weapon_rp.text.text_31") },
		[14] = { name = L("weapon_rp.text.text_33"), short = L("weapon_rp.text.text_33") },
		[15] = { name = L("weapon_rp.text.text_34"), short = L("weapon_rp.text.text_34") },

		-- взрывчатка и гранаты
		[16] = { name = L("weapon_rp.text.text_35"), short = L("weapon_rp.text.text_35") },
		[17] = { name = L("weapon_rp.text.text_36"), short = L("weapon_rp.text.text_36") },
		[18] = { name = L("weapon_rp.text.text_37"), short = L("weapon_rp.text.text_38") },
		[39] = {
			name = L("weapon_rp.text.text_39"),
			short = L("weapon_rp.text.text_40"),
			from = L("weapon_rp.text.text_41"),
			to = L("weapon_rp.text.text_42"),
		},
		[40] = { name = L("weapon_rp.text.text_43"), short = L("weapon_rp.text.text_43") },

		-- пистолеты
		[22] = {
			name = L("weapon_rp.text.text_9mm"),
			short = L("weapon_rp.text.short_9mm"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[23] = {
			name = L("weapon_rp.text.text_46"),
			short = L("weapon_rp.text.text_9mm_47"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[24] = {
			name = L("weapon_rp.text.desert_eagle"),
			short = L("weapon_rp.text.desert_eagle_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},

		-- дробовики
		[25] = {
			name = L("weapon_rp.text.text_48"),
			short = L("weapon_rp.text.text_48"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[26] = {
			name = L("weapon_rp.text.text_49"),
			short = L("weapon_rp.text.text_49"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[27] = {
			name = L("weapon_rp.text.text_50"),
			short = L("weapon_rp.text.spas_12_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},

		-- пистолеты-пулемёты
		[28] = {
			name = L("weapon_rp.text.micro_uzi"),
			short = L("weapon_rp.text.uzi_short"),
			from = L("weapon_rp.text.text_51"),
			to = L("weapon_rp.text.text_52"),
		},
		[29] = {
			name = L("weapon_rp.text.mp5_name"),
			short = L("weapon_rp.text.mp5_short"),
			from = L("weapon_rp.text.text_51"),
			to = L("weapon_rp.text.text_52"),
		},
		[32] = {
			name = L("weapon_rp.text.tec_9_name"),
			short = L("weapon_rp.text.tec_9_short"),
			from = L("weapon_rp.text.text_51"),
			to = L("weapon_rp.text.text_52"),
		},

		-- автоматы и винтовки
		[30] = {
			name = L("weapon_rp.text.ak_47_name"),
			short = L("weapon_rp.text.ak_47_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[31] = {
			name = L("weapon_rp.text.m4_name"),
			short = L("weapon_rp.text.m4_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[33] = {
			name = L("weapon_rp.text.text_53"),
			short = L("weapon_rp.text.text_53"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[34] = {
			name = L("weapon_rp.text.text_54"),
			short = L("weapon_rp.text.text_53"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},

		-- тяжёлое
		[35] = { name = L("weapon_rp.text.text_55"), short = L("weapon_rp.text.text_55"), from = L("weapon_rp.text.text_17"), to = L("weapon_rp.text.text_18") },
		[36] = {
			name = L("weapon_rp.text.text_56"),
			short = L("weapon_rp.text.text_55"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[37] = {
			name = L("weapon_rp.text.text_57"),
			short = L("weapon_rp.text.text_57"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[38] = {
			name = L("weapon_rp.text.text_58"),
			short = L("weapon_rp.text.text_58"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},

		-- утилиты
		[41] = {
			name = L("weapon_rp.text.text_59"),
			short = L("weapon_rp.text.text_60"),
			from = L("weapon_rp.text.text_61"),
			to = L("weapon_rp.text.text_62"),
		},
		[42] = {
			name = L("weapon_rp.text.text_63"),
			short = L("weapon_rp.text.text_63"),
			from = L("weapon_rp.text.text_61"),
			to = L("weapon_rp.text.text_62"),
		},
		[43] = {
			name = L("weapon_rp.text.text_64"),
			short = L("weapon_rp.text.text_65"),
			from = L("weapon_rp.text.text_61"),
			to = L("weapon_rp.text.text_62"),
		},
		[44] = {
			name = L("weapon_rp.text.text_66"),
			short = L("weapon_rp.text.text_67"),
			from = L("weapon_rp.text.text_61"),
			to = L("weapon_rp.text.text_62"),
		},
		[45] = {
			name = L("weapon_rp.text.text_68"),
			short = L("weapon_rp.text.text_68"),
			from = L("weapon_rp.text.text_61"),
			to = L("weapon_rp.text.text_62"),
		},
		[46] = {
			name = L("weapon_rp.text.text_69"),
			short = L("weapon_rp.text.text_69"),
			from = L("weapon_rp.text.text_61"),
			to = L("weapon_rp.text.text_62"),
		},

		-- ARZ кастом
		[71] = {
			name = L("weapon_rp.text.desert_eagle_steel"),
			short = L("weapon_rp.text.desert_eagle_steel_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[72] = {
			name = L("weapon_rp.text.desert_eagle_gold"),
			short = L("weapon_rp.text.desert_eagle_gold_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[73] = {
			name = L("weapon_rp.text.glock_gradient"),
			short = L("weapon_rp.text.glock_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[74] = {
			name = L("weapon_rp.text.desert_eagle_flame"),
			short = L("weapon_rp.text.desert_eagle_flame_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[75] = {
			name = L("weapon_rp.text.python_royal"),
			short = L("weapon_rp.text.python_royal_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
		[76] = {
			name = L("weapon_rp.text.python_silver"),
			short = L("weapon_rp.text.python_silver_short"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},

		[77] = {
			name = L("weapon_rp.text.ak_47_roses"),
			short = L("weapon_rp.text.ak_47_roses_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[78] = {
			name = L("weapon_rp.text.ak_47_gold"),
			short = L("weapon_rp.text.ak_47_gold_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},

		[79] = {
			name = L("weapon_rp.text.m249_graffiti"),
			short = L("weapon_rp.text.m249_graffiti_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[80] = {
			name = L("weapon_rp.text.text_70"),
			short = L("weapon_rp.text.gold"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[81] = {
			name = L("weapon_rp.text.standard"),
			short = L("weapon_rp.text.std"),
			from = L("weapon_rp.text.text_51"),
			to = L("weapon_rp.text.text_52"),
		},
		[82] = {
			name = L("weapon_rp.text.m249"),
			short = L("weapon_rp.text.m249_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[83] = {
			name = L("weapon_rp.text.skorpion"),
			short = L("weapon_rp.text.skorpion_short"),
			from = L("weapon_rp.text.text_51"),
			to = L("weapon_rp.text.text_52"),
		},

		[84] = {
			name = L("weapon_rp.text.aks_74"),
			short = L("weapon_rp.text.aks_74_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[85] = {
			name = L("weapon_rp.text.ak_47"),
			short = L("weapon_rp.text.ak_47_camo_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[86] = {
			name = L("weapon_rp.text.rebecca"),
			short = L("weapon_rp.text.rebecca_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},

		[87] = {
			name = L("weapon_rp.text.text_71"),
			short = L("weapon_rp.text.text_72"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[88] = {
			name = L("weapon_rp.text.text_73"),
			short = L("weapon_rp.text.text_73"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[89] = {
			name = L("weapon_rp.text.text_71"),
			short = L("weapon_rp.text.text_72"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},

		[90] = { name = L("weapon_rp.text.text_74"), short = L("weapon_rp.text.text_75") },
		[91] = { name = L("weapon_rp.text.text_76"), short = L("weapon_rp.text.text_77") },

		[92] = {
			name = L("weapon_rp.text.mcmillan_tac_50"),
			short = L("weapon_rp.text.mcmillan_tac_50_short"),
			from = L("weapon_rp.text.text_17"),
			to = L("weapon_rp.text.text_18"),
		},
		[93] = {
			name = L("weapon_rp.text.text_78"),
			short = L("weapon_rp.text.text_79"),
			from = L("weapon_rp.text.text_44"),
			to = L("weapon_rp.text.text_45"),
		},
	},
}

local DEFAULT_WORDS = {
	adverbs_show = funcs.deepcopy(M.config.adverbs_show),
	adverbs_hide = funcs.deepcopy(M.config.adverbs_hide),
	connectors_full = funcs.deepcopy(M.config.connectors_full),
	connectors_short = funcs.deepcopy(M.config.connectors_short),
}
local DEFAULT_WEAPONS = funcs.deepcopy(M.config.weapons)

local CONFIG_PATH_REL = "weapon_rp.json"
local config_manager
local event_bus
local module_enabled = true

-- ===== внутренняя служебка =====

-- дефолт для неизвестного оружия
local DEFAULT_WEAPON = {
	enable = true,
	name = nil, -- будет "оружие %d"
	short = nil, -- будет "оружие %d"
	from = L("weapon_rp.text.text_80"),
	to = L("weapon_rp.text.text_81"),
	verbs = { show = { L("weapon_rp.text.text_82") }, hide = { L("weapon_rp.text.text_83") } },
}

local function normalize_weapon(id, w)
	w = w or {}
	local r = {}
	r.enable = (w.enable ~= false)
	r.name = w.name or (L("weapon_rp.text.number")):format(id)
	r.short = w.short or r.name
	r.from = w.from or DEFAULT_WEAPON.from
	r.to = w.to or DEFAULT_WEAPON.to
	r.verbs = r.verbs or {}
	r.verbs.show = (w.verbs and w.verbs.show) or funcs.deepcopy(DEFAULT_WEAPON.verbs.show)
	r.verbs.hide = (w.verbs and w.verbs.hide) or funcs.deepcopy(DEFAULT_WEAPON.verbs.hide)
	return r
end

-- ===== загрузка/сохранение =====
local running, thr = false, nil
local prev_weapon, candidate_weapon, stable_count, cooldown = -1, -1, 0, 0
local pending_old, pending_new
local cb_any, cb_show, cb_hide, cb_change
local reset_state

local function persist_main_enabled_state(state, force_flush)
	if not config_manager then
		return false
	end
	local main_config = config_manager.get("main")
	if type(main_config) ~= "table" then
		return false
	end
	if type(main_config.moduleStates) ~= "table" then
		main_config.moduleStates = {}
	end
	local normalized = not not state
	local changed = main_config.moduleStates.weapon_rp ~= normalized
	main_config.moduleStates.weapon_rp = normalized
	if changed then
		config_manager.markDirty("main")
	end
	if force_flush and type(config_manager.flush) == "function" then
		config_manager.flush("main", true)
	end
	return true
end

local function normalize_weapon_rp_config(tbl)
	if type(tbl) ~= "table" then
		tbl = {}
	end
	funcs.applyKnownKeys(M.config, tbl, true)
	local out = {}
	for id, w in pairs(M.config.weapons or {}) do
		out[tonumber(id) or id] = normalize_weapon(tonumber(id) or id, w)
	end
	M.config.weapons = out
	return M.config
end

local function serialize_weapon_rp_config(data)
	local cfg_copy = funcs.deepcopy(data)
	if cfg_copy.weapons then
		local weapons = {}
		for id, w in pairs(cfg_copy.weapons) do
			weapons[tostring(id)] = w
		end
		cfg_copy.weapons = weapons
	end
	return cfg_copy
end

local function ensure_config_manager_registration()
	if not config_manager then
		return nil
	end

	local data = config_manager.get("weapon_rp")
	if type(data) ~= "table" then
		local snapshot = serialize_weapon_rp_config(M.config)
		data = config_manager.register("weapon_rp", {
			path = CONFIG_PATH_REL,
			defaults = snapshot,
			normalize = normalize_weapon_rp_config,
			serialize = serialize_weapon_rp_config,
			-- weapon_rp already loads JSON before attachModules(); seed the manager
			-- from the current in-memory config so hot-reload/init order can't drop changes.
			loader = function(_, defaults)
				return funcs.deepcopy(defaults or {})
			end,
		})
	end

	if type(data) == "table" then
		M.config = data
		M._cfg_loaded = true
	end

	return data
end

local function load_cfg()
	if config_manager then
		local data = ensure_config_manager_registration()
		if data then
			M.config = data
		end
	else
		local tbl = funcs.loadTableFromJson(CONFIG_PATH_REL)
		normalize_weapon_rp_config(tbl)
	end
	if running then
		reset_state()
	end
end

local function save_cfg(force_flush)
	if config_manager then
		ensure_config_manager_registration()
		config_manager.markDirty("weapon_rp")
		if force_flush and type(config_manager.flush) == "function" then
			config_manager.flush("weapon_rp", true)
		end
	else
		funcs.saveTableToJson(serialize_weapon_rp_config(M.config), paths.dataPath(CONFIG_PATH_REL))
	end
end

local function clear_ui_dirty()
	ui_state.dirty = false
end

local function save_cfg_with_ui_state()
	save_cfg(true)
	clear_ui_dirty()
end

local function load_cfg_with_ui_state()
	load_cfg()
	clear_ui_dirty()
end

M.save = save_cfg_with_ui_state
M.reload = load_cfg_with_ui_state

load_cfg_with_ui_state()
M._cfg_loaded = true

local settings_ui_cache = {
	bools = {},
	ints = {},
	chars = {},
	chars_text = {},
	multiline_chars = {},
	multiline_chars_text = {},
}
local mode_labels = { L("weapon_rp.text.me"), L("weapon_rp.text.text_84") }
local mode_labels_ffi = imgui.new["const char*"][#mode_labels](mode_labels)
local weapon_sort_labels = { L("weapon_rp.text.id"), L("weapon_rp.text.text_85"), L("weapon_rp.text.text_86"), L("weapon_rp.text.text_87") }
local weapon_sort_labels_ffi = imgui.new["const char*"][#weapon_sort_labels](weapon_sort_labels)
local words_presets = {
	{
		id = "base",
		title = L("weapon_rp.text.text_88"),
		data = DEFAULT_WORDS,
	},
	{
		id = "calm",
		title = L("weapon_rp.text.text_89"),
		data = {
			adverbs_show = { L("weapon_rp.text.text"), L("weapon_rp.text.text_6"), L("weapon_rp.text.text_4") },
			adverbs_hide = { L("weapon_rp.text.text_5"), L("weapon_rp.text.text_6") },
			connectors_full = { L("weapon_rp.text.text_10") },
			connectors_short = { L("weapon_rp.text.text_10") },
		},
	},
	{
		id = "rich",
		title = L("weapon_rp.text.text_90"),
		data = {
			adverbs_show = {
				L("weapon_rp.text.text"),
				L("weapon_rp.text.text_1"),
				L("weapon_rp.text.text_2"),
				L("weapon_rp.text.text_3"),
				L("weapon_rp.text.text_4"),
				L("weapon_rp.text.text_91"),
				L("weapon_rp.text.text_92"),
			},
			adverbs_hide = {
				L("weapon_rp.text.text_5"),
				L("weapon_rp.text.text_6"),
				L("weapon_rp.text.text_7"),
				L("weapon_rp.text.text_8"),
				L("weapon_rp.text.text_9"),
				L("weapon_rp.text.text_93"),
			},
			connectors_full = {
				L("weapon_rp.text.text_10"),
				L("weapon_rp.text.text_11"),
				L("weapon_rp.text.text_12"),
				L("weapon_rp.text.text_13"),
				L("weapon_rp.text.text_94"),
			},
			connectors_short = { L("weapon_rp.text.text_10"), L("weapon_rp.text.text_95") },
		},
	},
}

local function mark_dirty()
	ui_state.dirty = true
end

local function ui_bool(id, value)
	local b = settings_ui_cache.bools[id]
	if not b then
		b = imgui.new.bool(false)
		settings_ui_cache.bools[id] = b
	end
	b[0] = value and true or false
	return b
end

local function ui_int(id, value)
	local b = settings_ui_cache.ints[id]
	if not b then
		b = imgui.new.int(0)
		settings_ui_cache.ints[id] = b
	end
	b[0] = math.floor(tonumber(value) or 0)
	return b
end

local INPUTTEXT_CALLBACK_RESIZE = imgui.InputTextFlags and imgui.InputTextFlags.CallbackResize or nil
local SINGLELINE_BUF_PAD = 128
local MULTILINE_BUF_PAD = 512

local function calc_dynamic_buffer_size(current_size, required_size)
	local size = math.max(2, math.floor(tonumber(current_size) or 2))
	required_size = math.max(2, math.floor(tonumber(required_size) or 2))
	while size < required_size do
		size = size * 2
	end
	return size
end

local function has_flag(flags, flag)
	flags = math.max(0, math.floor(tonumber(flags) or 0))
	flag = math.max(0, math.floor(tonumber(flag) or 0))
	if flag == 0 then
		return false
	end
	return (flags % (flag * 2)) >= flag
end

local function add_flag(flags, flag)
	flags = math.max(0, math.floor(tonumber(flags) or 0))
	flag = math.max(0, math.floor(tonumber(flag) or 0))
	if flag == 0 then
		return flags
	end
	if not has_flag(flags, flag) then
		flags = flags + flag
	end
	return flags
end

local function ensure_dynamic_char_buffer(cache, text_cache, id, value, min_size)
	min_size = math.max(2, math.floor(tonumber(min_size) or 2))
	local text = tostring(value or "")
	local required_size = math.max(min_size, #text + 1)
	local buf = cache[id]
	local current_size = buf and ffi.sizeof(buf) or 0
	if current_size < required_size then
		local new_size = calc_dynamic_buffer_size(math.max(current_size, min_size), required_size)
		buf = imgui.new.char[new_size](text)
		cache[id] = buf
		text_cache[id] = text
		return buf
	end
	if text_cache[id] ~= text then
		ffi.fill(buf, ffi.sizeof(buf))
		local copy_len = math.min(#text, ffi.sizeof(buf) - 1)
		if copy_len > 0 then
			ffi.copy(buf, text, copy_len)
		end
		text_cache[id] = text
	end
	return buf
end

local function grow_dynamic_char_buffer(cache, text_cache, id, min_size, required_size, text)
	min_size = math.max(2, math.floor(tonumber(min_size) or 2))
	required_size = math.max(min_size, math.floor(tonumber(required_size) or min_size))
	local current = cache[id]
	local current_size = current and ffi.sizeof(current) or 0
	if current_size >= required_size then
		return current
	end
	local new_size = calc_dynamic_buffer_size(math.max(current_size, min_size), required_size)
	local new_buf = imgui.new.char[new_size](tostring(text or ""))
	cache[id] = new_buf
	text_cache[id] = ffi.string(new_buf)
	return new_buf
end

local current_dynamic_input_ctx = nil
local dynamic_input_resize_callback_ptr = nil

if INPUTTEXT_CALLBACK_RESIZE then
	local function dynamic_input_resize_callback(data)
		local ctx = current_dynamic_input_ctx
		if not ctx or data.EventFlag ~= INPUTTEXT_CALLBACK_RESIZE then
			return 0
		end

		local len = data.BufTextLen or 0
		local text = ffi.string(data.Buf, len)
		local required_size = math.max(ctx.min_size, len + 1 + ctx.pad)
		local new_buf = grow_dynamic_char_buffer(
			ctx.cache,
			ctx.text_cache,
			ctx.id,
			ctx.min_size,
			required_size,
			text
		)
		data.Buf = new_buf
		data.BufSize = ffi.sizeof(new_buf)
		return 0
	end

	dynamic_input_resize_callback_ptr = ffi.cast(
		"int (*)(ImGuiInputTextCallbackData*)",
		dynamic_input_resize_callback
	)
end

local function ui_char(id, value, min_size)
	return ensure_dynamic_char_buffer(
		settings_ui_cache.chars,
		settings_ui_cache.chars_text,
		id,
		value,
		min_size
	)
end

local function ui_char_multiline(id, value, min_size)
	return ensure_dynamic_char_buffer(
		settings_ui_cache.multiline_chars,
		settings_ui_cache.multiline_chars_text,
		id,
		value,
		min_size
	)
end

local function input_text_dynamic(label, id, value, min_size, flags)
	min_size = math.max(2, math.floor(tonumber(min_size) or 2))
	local buf = ui_char(id, value, min_size)
	local use_flags = math.max(0, math.floor(tonumber(flags) or 0))
	local changed
	if dynamic_input_resize_callback_ptr and INPUTTEXT_CALLBACK_RESIZE then
		use_flags = add_flag(use_flags, INPUTTEXT_CALLBACK_RESIZE)
		current_dynamic_input_ctx = {
			cache = settings_ui_cache.chars,
			text_cache = settings_ui_cache.chars_text,
			id = id,
			min_size = min_size,
			pad = SINGLELINE_BUF_PAD,
		}
		changed = imgui.InputText(label, buf, ffi.sizeof(buf), use_flags, dynamic_input_resize_callback_ptr)
		current_dynamic_input_ctx = nil
	else
		changed = imgui.InputText(label, buf, ffi.sizeof(buf), use_flags)
	end

	local active_buf = settings_ui_cache.chars[id] or buf
	local active_text = ffi.string(active_buf)
	if (not dynamic_input_resize_callback_ptr) and #active_text >= (ffi.sizeof(active_buf) - 1) then
		active_buf = grow_dynamic_char_buffer(
			settings_ui_cache.chars,
			settings_ui_cache.chars_text,
			id,
			min_size,
			ffi.sizeof(active_buf) + SINGLELINE_BUF_PAD,
			active_text
		)
		active_text = ffi.string(active_buf)
	end
	if changed then
		settings_ui_cache.chars_text[id] = active_text
	end
	return changed, active_buf
end

local function input_text_multiline_dynamic(label, id, value, min_size, size, flags)
	min_size = math.max(2, math.floor(tonumber(min_size) or 2))
	local buf = ui_char_multiline(id, value, min_size)
	local use_flags = math.max(0, math.floor(tonumber(flags) or 0))
	local changed
	if dynamic_input_resize_callback_ptr and INPUTTEXT_CALLBACK_RESIZE then
		use_flags = add_flag(use_flags, INPUTTEXT_CALLBACK_RESIZE)
		current_dynamic_input_ctx = {
			cache = settings_ui_cache.multiline_chars,
			text_cache = settings_ui_cache.multiline_chars_text,
			id = id,
			min_size = min_size,
			pad = MULTILINE_BUF_PAD,
		}
		changed = imgui.InputTextMultiline(
			label,
			buf,
			ffi.sizeof(buf),
			size or imgui.ImVec2(0, 0),
			use_flags,
			dynamic_input_resize_callback_ptr
		)
		current_dynamic_input_ctx = nil
	else
		changed = imgui.InputTextMultiline(
			label,
			buf,
			ffi.sizeof(buf),
			size or imgui.ImVec2(0, 0),
			use_flags
		)
	end

	local active_buf = settings_ui_cache.multiline_chars[id] or buf
	local active_text = ffi.string(active_buf)
	if (not dynamic_input_resize_callback_ptr) and #active_text >= (ffi.sizeof(active_buf) - 1) then
		active_buf = grow_dynamic_char_buffer(
			settings_ui_cache.multiline_chars,
			settings_ui_cache.multiline_chars_text,
			id,
			min_size,
			ffi.sizeof(active_buf) + MULTILINE_BUF_PAD,
			active_text
		)
		active_text = ffi.string(active_buf)
	end
	if changed then
		settings_ui_cache.multiline_chars_text[id] = active_text
	end
	return changed, active_buf
end

-- ===== события/детектор =====

function reset_state()
	local ped = PLAYER_PED
	local w = 0
	if ped then
		w = getCurrentCharWeapon(ped) or 0
	end
	prev_weapon, candidate_weapon, stable_count, cooldown = w, w, 0, 0
	pending_old, pending_new = nil, nil
end

function M.attachModules(mod)
	syncDependencies(mod)
	config_manager = mod.config_manager
	event_bus = mod.event_bus
	if event_bus then
		event_bus.offByOwner("weapon_rp")
	end
	if config_manager then
		ensure_config_manager_registration()
	elseif not M._cfg_loaded then
		load_cfg()
		M._cfg_loaded = true
	end
end

function M.isEnabled()
	return module_enabled
end

function M.setEnabled(state, opts)
	opts = opts or {}
	module_enabled = not not state
	if opts.save_project ~= false then
		persist_main_enabled_state(module_enabled, opts.flush_project)
	end
	if opts.apply_runtime ~= false then
		if module_enabled then
			M.start()
		else
			M.stop()
		end
	end
	return module_enabled
end

function M.onAny(fn)
	cb_any = fn
end
function M.onShow(fn)
	cb_show = fn
end
function M.onHide(fn)
	cb_hide = fn
end
function M.onChange(fn)
	cb_change = fn
end

-- ===== очередь /me с паузой =====
local send_queue, send_thr = {}, nil
local SEND_IDLE_WAIT_MS = 120
local SEND_EMPTY_ITEM_WAIT_MS = 30
local function ensure_send_thread()
	if send_thr and send_thr:status() ~= "dead" then
		return
	end
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
					wait(SEND_EMPTY_ITEM_WAIT_MS)
				end
			else
				wait(SEND_IDLE_WAIT_MS)
			end
		end
	end)
end

local function send_chat(line)
	if not line or line == "" then
		return
	end
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
	if type(t) ~= "table" or #t == 0 then
		return nil
	end
	if fixed_by_level then
		-- flavor_level 1 — берём первый
		return t[1]
	end
	local x = math.floor((os.clock() * 1000 + (salt or 0))) % #t + 1
	return t[x]
end

local function is_empty_weapon(id)
	if id == -1 or id == 0 then
		return true
	end
	if id == 1 and M.config.ignore_knuckles then
		return true
	end
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
	if w.enable == false then
		return nil
	end
	local verbs = (kind == "show") and w.verbs.show or w.verbs.hide
	local verb = pick(verbs, id * 7 + (kind == "show" and 1 or 2), opts.plain)
		or (kind == "show" and L("weapon_rp.text.text_82") or L("weapon_rp.text.text_83"))
	local advs = (kind == "show") and M.config.adverbs_show or M.config.adverbs_hide
	local adv = (opts.use_adv and M.config.flavor_level >= 2) and (pick(advs, id * 11) or "") or ""
	if adv ~= "" then
		adv = adv .. " "
	end
	local place = (kind == "show") and w.from or w.to
	if opts.no_place then
		place = ""
	end
	local name = (opts.short_names and w.short) or w.name
	if place ~= "" then
		return ("%s %s%s %s %s"):format(M.config.prefix, adv, verb, place, name)
	else
		return ("%s %s%s %s"):format(M.config.prefix, adv, verb, name)
	end
end

local function join_changed(hide_line, show_line, connector)
	local s1 = hide_line:gsub("^/me%s*", ""):gsub("%s+$", "")
	local s2 = show_line:gsub("^/me%s*", ""):gsub("^%s+", "")
	local conn = tostring(connector or ""):gsub("^%s+", ""):gsub("%s+$", "")
	local before = conn:match("^[,;:%.!?%)%]%}]") and "" or " "
	if conn == "" then
		return ("%s %s %s"):format(M.config.prefix, s1, s2)
	end
	return ("%s %s%s%s %s"):format(M.config.prefix, s1, before, conn, s2)
end

-- попытки ужатия строки
local function try_build(kind, newId, oldId, level)
	local opts = {
		use_adv = (level <= 1),
		plain = (level >= 2),
		short_names = (level >= 3),
		no_place = (level >= 4),
	}
	local conns = (level >= 1) and M.config.connectors_short or M.config.connectors_full
	local connector = pick(conns, (newId or 0) + (oldId or 0), M.config.flavor_level == 1) or L("weapon_rp.text.text_10")

	if kind == "shown" then
		return build_part("show", newId, opts)
	elseif kind == "hidden" then
		return build_part("hide", oldId, opts)
	else
		local h = build_part("hide", oldId, opts)
		local s = build_part("show", newId, opts)
		if h and s then
			return join_changed(h, s, connector)
		end
		return s or h
	end
end

function M.makeRpLine(kind, newId, oldId)
	-- уровни: 0 максимум украшений -> 4 максимально компактно
	local line
	for lvl = 0, 4 do
		line = try_build(kind, newId, oldId, lvl)
		if line and str_len(line) <= M.config.max_len then
			return line
		end
	end
	-- край: отрез по слову + многоточие
	if line and str_len(line) > M.config.max_len then
		local s = line
		while str_len(s) > M.config.max_len - 1 do
			s = s:match("^(.*)%s+[^%s]+$") or s:sub(1, M.config.max_len - 1)
		end
		return s .. "…"
	end
	return line
end

-- авто-RP/ПКМ
local function auto_rp(kind, newW, oldW)
	if M.config.auto_mode == 0 then
		local line = M.makeRpLine(kind, newW, oldW)
		if line and line ~= "" then
			ui_state.last_rp_line = line
			send_chat(line)
		end
	else
		if not pending_old then
			pending_old = oldW
		end
		pending_new = newW
	end
end

local function fire(kind, newW, oldW)
	if type(cb_any) == "function" then
		pcall(cb_any, kind, newW, oldW)
	end
	if kind == "shown" and type(cb_show) == "function" then
		pcall(cb_show, newW, oldW)
	elseif kind == "hidden" and type(cb_hide) == "function" then
		pcall(cb_hide, newW, oldW)
	elseif kind == "changed" and type(cb_change) == "function" then
		pcall(cb_change, newW, oldW)
	end
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
			if line and line ~= "" then
				ui_state.last_rp_line = line
				send_chat(line)
			end
		end
		pending_old, pending_new = nil, nil
	end

	local ped = PLAYER_PED
	if not ped then
		return
	end
	local w = getCurrentCharWeapon(ped)
	if w == nil then
		return
	end

	if w == candidate_weapon then
		if stable_count < M.config.stable_need then
			stable_count = stable_count + 1
		end
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
	if not module_enabled then
		return
	end
	if not M._cfg_loaded then
		load_cfg()
		M._cfg_loaded = true
	end
	if running then
		return
	end
	running = true
	reset_state()
	if interval_ms then
		M.config.tick_ms = interval_ms
	end
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
	if thr and thr:status() ~= "dead" then
		thr:terminate()
	end
	thr = nil
end

-- ======= UI =======
-- лёгкая таблица с поиском + попап-редактор
-- иконки из стандартного списка (опционально)
local weaponsID = {
	0,
	1,
	2,
	3,
	4,
	5,
	6,
	7,
	8,
	9,
	22,
	23,
	24,
	25,
	26,
	27,
	28,
	29,
	32,
	30,
	31,
	33,
	34,
	35,
	36,
	37,
	38,
	16,
	17,
	18,
	39,
	41,
	42,
	43,
	10,
	11,
	12,
	13,
	14,
	15,
	44,
	45,
	46,
	40,
}
local sprite_idx_by_weapon, unknown_sprite_idx = {}, 1
for idx, id in ipairs(weaponsID) do
	sprite_idx_by_weapon[id] = idx
end

-- ===== UI вспомогательные функции =====

-- Проверка является ли оружие кастомным (не в дефолтном списке)
local function is_custom_weapon(id)
	return DEFAULT_WEAPONS[id] == nil
end

-- Получить отсортированный список ID оружий
local function get_sorted_weapon_ids()
	local ids = {}
	for id, _ in pairs(M.config.weapons) do
		table.insert(ids, id)
	end
	local asc = ui_state.sort_ascending
	local mode = ui_state.weapon_sort_mode
	if mode == 0 then
		-- По ID
		if asc then
			table.sort(ids, function(a, b) return a < b end)
		else
			table.sort(ids, function(a, b) return a > b end)
		end
	elseif mode == 1 then
		-- По имени
		table.sort(ids, function(a, b)
			local na = (M.config.weapons[a].short or M.config.weapons[a].name or ""):lower()
			local nb = (M.config.weapons[b].short or M.config.weapons[b].name or ""):lower()
			if na == nb then return a < b end
			if asc then return na < nb else return na > nb end
		end)
	elseif mode == 2 then
		-- Вкл сначала
		table.sort(ids, function(a, b)
			local ea = (M.config.weapons[a].enable ~= false) and 1 or 0
			local eb = (M.config.weapons[b].enable ~= false) and 1 or 0
			if ea ~= eb then
				if asc then return ea > eb else return ea < eb end
			end
			return a < b
		end)
	else
		-- Кастом сначала (mode == 3)
		table.sort(ids, function(a, b)
			local ca = is_custom_weapon(a) and 1 or 0
			local cb = is_custom_weapon(b) and 1 or 0
			if ca ~= cb then
				if asc then return ca > cb else return ca < cb end
			end
			return a < b
		end)
	end
	return ids
end

-- Проверка соответствия оружия фильтрам
local function weapon_matches_filters(id, w, query)
	-- Проверка поиска
	if query and query ~= "" then
		local label = (w.short or w.name or L("weapon_rp.text.weapon_fallback", { id = id })):lower()
		local id_str = tostring(id)
		if not (id_str:find(query, 1, true) or label:find(query, 1, true)) then
			return false
		end
	end

	-- Фильтр "только включенные"
	if ui_state.filter_enabled_only and not w.enable then
		return false
	end

	-- Фильтр "только кастомные"
	if ui_state.filter_custom_only and not is_custom_weapon(id) then
		return false
	end

	return true
end

-- Получить текущее оружие игрока
local function get_current_weapon_id()
	local ped = PLAYER_PED
	if ped then
		return getCurrentCharWeapon(ped)
	end
	return nil
end

-- Получить статус модуля
local function get_status_text()
	if not running then
		return L("weapon_rp.text.text_96"), imgui.ImVec4(0.7, 0.7, 0.7, 1.0)
	end

	if M.config.auto_mode == 1 and pending_new then
		return L("weapon_rp.text.text_97"), imgui.ImVec4(1.0, 0.8, 0.2, 1.0)
	end

	return L("weapon_rp.text.text_98"), imgui.ImVec4(0.2, 0.8, 0.2, 1.0)
end

-- Сброс оружия к дефолту
local function reset_weapon_to_default(id)
	if DEFAULT_WEAPONS[id] then
		M.config.weapons[id] = normalize_weapon(id, funcs.deepcopy(DEFAULT_WEAPONS[id]))
	else
		-- Если дефолта нет, сбросить к базовому шаблону
		M.config.weapons[id] = normalize_weapon(id, {})
	end
	mark_dirty()
end

-- ===== Константы отрисовки строки =====
local ROW_H       = 26  -- высота строки оружия в пикселях
local ICON_CELL_W = 44  -- ширина ячейки иконки

-- Нарисовать иконку с aspect-fit в фиксированной ячейке ICON_CELL_W x ROW_H.
-- Режим 0: стандартный файл (горизонтальный спрайт).
-- Режимы 2/3: FFI текстура (квадратная); при промахе кеша — стандартный файл.
local function draw_icon_cell(dl, id, cell_x, cell_y)
	local ico_mode = M.config.weapon_icon_mode
	local used_ffi = false
	if ico_mode == 2 or ico_mode == 3 then
		local tex = weapon_icon_cache[id]
		if tex ~= nil then
			-- FFI ICON-текстуры квадратные; вписываем в ячейку с aspect-fit
			local icon_size = math.min(ICON_CELL_W - 4, ROW_H - 2)
			local off_x = math.floor((ICON_CELL_W - icon_size) / 2)
			local off_y = math.floor((ROW_H    - icon_size) / 2)
			dl:AddImage(tex,
				imgui.ImVec2(cell_x + off_x,             cell_y + off_y),
				imgui.ImVec2(cell_x + off_x + icon_size, cell_y + off_y + icon_size))
			used_ffi = true
		end
	end
	if not used_ffi and has_sprites then
		-- Стандартный файл: горизонтальные спрайты (~3:1).
		-- Вписываем drawWeaponZoom в ячейку с сохранением пропорций.
		local spr    = sprite_idx_by_weapon[id] or unknown_sprite_idx
		local draw_w = ICON_CELL_W - 4           -- 40 px
		local draw_h = math.floor(draw_w / 3)    -- ~13 px (3:1)
		local off_y  = math.floor((ROW_H - draw_h) / 2)
		imgui.SetCursorScreenPos(imgui.ImVec2(cell_x + 2, cell_y + off_y))
		mimgui_funcs.drawWeaponZoom(mimgui_funcs.weapon_standard, spr, imgui.ImVec2(draw_w, draw_h), 1.0)
	end
end

-- попап редактирования одного оружия
local function draw_weapon_popup(id)
	local popup = ("weapon_edit_%d"):format(id)
	local w = winfo(id)
	if imgui.BeginPopup(popup) then
		imgui.SetWindowSizeVec2(imgui.ImVec2(500, 0), imgui.Cond.FirstUseEver)

		local prefix = "popup_" .. tostring(id) .. "_"

		-- Автофокус на первое поле при первом открытии
		if ui_state.popup_focus_weapon_id == id then
			imgui.SetKeyboardFocusHere()
			ui_state.popup_focus_weapon_id = nil
		end

		-- Превью RP-строк (с фиксированным выбором для стабильности)
		imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0.1, 0.1, 0.15, 0.9))
		imgui.BeginChild("##preview_child", imgui.ImVec2(0, 120), true)

		-- Сохраняем текущий flavor_level и временно устанавливаем 1 для стабильного превью
		local saved_flavor = M.config.flavor_level
		M.config.flavor_level = 1
		local line_shown = M.makeRpLine("shown", id, 0)
		local line_hidden = M.makeRpLine("hidden", 0, id)
		local line_changed = M.makeRpLine("changed", id, 22) -- 22 для примера (9mm)
		M.config.flavor_level = saved_flavor

		imgui.TextColored(imgui.ImVec4(0.5, 0.9, 0.5, 1.0), L("weapon_rp.text.text_99"))
		imgui.SameLine()
		if line_shown then
			local len_shown = str_len(line_shown)
			local color = len_shown > M.config.max_len and imgui.ImVec4(1.0, 0.4, 0.4, 1.0) or imgui.ImVec4(1.0, 1.0, 1.0, 1.0)
			local line_shown_utf8 = line_shown
			imgui.TextColored(color, line_shown_utf8:gsub("%%", "%%%%"))
			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), ("(%d)"):format(len_shown))
		else
			imgui.TextDisabled("—")
		end

		imgui.TextColored(imgui.ImVec4(0.9, 0.5, 0.5, 1.0), L("weapon_rp.text.text_100"))
		imgui.SameLine()
		if line_hidden then
			local len_hidden = str_len(line_hidden)
			local color = len_hidden > M.config.max_len and imgui.ImVec4(1.0, 0.4, 0.4, 1.0) or imgui.ImVec4(1.0, 1.0, 1.0, 1.0)
			local line_hidden_utf8 = line_hidden
			imgui.TextColored(color, line_hidden_utf8:gsub("%%", "%%%%"))
			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), ("(%d)"):format(len_hidden))
		else
			imgui.TextDisabled("—")
		end

		imgui.TextColored(imgui.ImVec4(0.5, 0.7, 0.9, 1.0), L("weapon_rp.text.text_101"))
		imgui.SameLine()
		if line_changed then
			local len_changed = str_len(line_changed)
			local color = len_changed > M.config.max_len and imgui.ImVec4(1.0, 0.4, 0.4, 1.0) or imgui.ImVec4(1.0, 1.0, 1.0, 1.0)
			local line_changed_utf8 = line_changed
			imgui.TextColored(color, line_changed_utf8:gsub("%%", "%%%%"))
			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), ("(%d)"):format(len_changed))
		else
			imgui.TextDisabled("—")
		end

		imgui.EndChild()
		imgui.PopStyleColor()

		imgui.Separator()

		-- Основные настройки
		local en = ui_bool(prefix .. "enable", w.enable ~= false)
		if imgui.Checkbox(L("weapon_rp.text.text_102"), en) then
			w.enable = en[0]
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.rp"))

		local changed_nm, nm = input_text_dynamic(L("weapon_rp.text.text_103"), prefix .. "name", w.name or "", 96)
		if changed_nm then
			w.name = ffi.string(nm)
			if (w.short or "") == "" then
				w.short = w.name
			end
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.desert_eagle_104"))

		local changed_sh, sh = input_text_dynamic(L("weapon_rp.text.text_105"), prefix .. "short", (w.short or ""), 48)
		if changed_sh then
			w.short = ffi.string(sh)
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.deagle"))

		local changed_fr, fr = input_text_dynamic(L("weapon_rp.text.text_106"), prefix .. "from", w.from or "", 48)
		if changed_fr then
			w.from = ffi.string(fr)
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.text_107"))

		local changed_to, to = input_text_dynamic(L("weapon_rp.text.text_108"), prefix .. "to", w.to or "", 48)
		if changed_to then
			w.to = ffi.string(to)
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.text_109"))

		local changed_showv, showv = input_text_dynamic(
			L("weapon_rp.text.text_110"),
			prefix .. "show_verbs",
			(table.concat(w.verbs.show or {}, ",")),
			160
		)
		if changed_showv then
			local utf8_verbs = funcs.parseList(ffi.string(showv))
			w.verbs.show = {}
			for _, verb in ipairs(utf8_verbs) do
				table.insert(w.verbs.show, (verb))
			end
			if #w.verbs.show == 0 then
				w.verbs.show = { L("weapon_rp.text.text_82") }
			end
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.text_111"))

		local changed_hidev, hidev = input_text_dynamic(
			L("weapon_rp.text.text_112"),
			prefix .. "hide_verbs",
			(table.concat(w.verbs.hide or {}, ",")),
			160
		)
		if changed_hidev then
			local utf8_verbs = funcs.parseList(ffi.string(hidev))
			w.verbs.hide = {}
			for _, verb in ipairs(utf8_verbs) do
				table.insert(w.verbs.hide, (verb))
			end
			if #w.verbs.hide == 0 then
				w.verbs.hide = { L("weapon_rp.text.text_83") }
			end
			M.config.weapons[id] = w
			mark_dirty()
		end
		tooltip(L("weapon_rp.text.text_113"))

		imgui.Separator()

		-- Кнопки управления
		if DEFAULT_WEAPONS[id] then
			if imgui.Button(L("weapon_rp.text.text_114")) then
				reset_weapon_to_default(id)
			end
			tooltip(L("weapon_rp.text.text_115"))
			imgui.SameLine()
		end

		if imgui.Button(L("weapon_rp.text.text_116")) then
			imgui.CloseCurrentPopup()
		end

		imgui.EndPopup()
	end
end

-- строка одного оружия (мастер-список)
-- row_idx используется для зебра-окраски (1-based)
local function draw_weapon_row(id, w, is_current, row_idx)
	local dl      = imgui.GetWindowDrawList()
	local start   = imgui.GetCursorScreenPos()
	local avail_w = imgui.GetContentRegionAvail().x
	local is_selected = (ui_state.selected_weapon_id == id)

	-- === Цвет фона (зебра / текущее / выбранное / вспышка скролла) ===
	local now  = os.clock()
	local hl_t = 0
	if ui_state.scroll_highlight_id == id then
		hl_t = math.max(0, 1.0 - (now - ui_state.scroll_highlight_time) / 1.5)
		if hl_t <= 0 then ui_state.scroll_highlight_id = nil end
	end

	local bg
	if hl_t > 0 then
		bg = imgui.ImVec4(0.7 * hl_t + 0.12, 0.6 * hl_t + 0.14, 0.1 * hl_t + 0.18, 0.92)
	elseif is_selected then
		bg = imgui.ImVec4(0.18, 0.32, 0.55, 0.90)
	elseif is_current then
		bg = imgui.ImVec4(0.10, 0.26, 0.10, 0.75)
	elseif row_idx % 2 == 0 then
		bg = imgui.ImVec4(0.15, 0.15, 0.18, 0.40)
	else
		bg = imgui.ImVec4(0.10, 0.10, 0.13, 0.20)
	end
	dl:AddRectFilled(
		start,
		imgui.ImVec2(start.x + avail_w, start.y + ROW_H),
		imgui.GetColorU32Vec4(bg), 3)

	-- === 1. Чекбокс (обрабатывается первым → приоритет клика) ===
	local chk_y = start.y + math.floor((ROW_H - 14) / 2)
	imgui.SetCursorScreenPos(imgui.ImVec2(start.x + ICON_CELL_W + 4, chk_y))
	local en = ui_bool("row_en_" .. tostring(id), w.enable ~= false)
	if imgui.Checkbox(("##en_%d"):format(id), en) then
		w.enable = en[0]
		M.config.weapons[id] = w
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.text_117"))

	-- === 2. Selectable (текст + ПКМ-меню) ===
	local text_x  = ICON_CELL_W + 26
	local sel_w   = math.max(10, avail_w - text_x)
	imgui.SetCursorScreenPos(imgui.ImVec2(start.x + text_x, start.y))

	local weapon_name = w.short or w.name or L("weapon_rp.text.weapon_fallback", { id = id })
	local label_raw   = ("%s [%d]%s"):format(weapon_name, id, is_custom_weapon(id) and (" " .. L("weapon_rp.text.filter_custom_toggle")) or "")
	-- Selectable не использует printf-форматирование → экранировать %% не нужно
	local label_disp  = label_raw .. ("##sel_%d"):format(id)

	-- Серый цвет для выключенного оружия
	local dim = not (w.enable ~= false)
	if dim then imgui.PushStyleColor(imgui.Col.Text, imgui.ImVec4(0.50, 0.50, 0.50, 0.80)) end

	-- Прозрачный Selectable: подсветку рисуем сами через DrawList
	imgui.PushStyleColor(imgui.Col.Header,        imgui.ImVec4(0, 0, 0, 0))
	imgui.PushStyleColor(imgui.Col.HeaderHovered, imgui.ImVec4(0, 0, 0, 0))
	imgui.PushStyleColor(imgui.Col.HeaderActive,  imgui.ImVec4(0, 0, 0, 0))
	local clicked = imgui.Selectable(label_disp, is_selected, 0, imgui.ImVec2(sel_w, ROW_H))
	imgui.PopStyleColor(3)
	if dim then imgui.PopStyleColor() end

	if clicked then
		ui_state.selected_weapon_id = id
	end
	local sel_hovered = imgui.IsItemHovered(0)

	-- Контекстное меню по ПКМ на Selectable
	if imgui.BeginPopupContextItem(("##ctx_%d"):format(id), 1) then
		if imgui.MenuItemBool(L("weapon_rp.text.text_118"), "", false, true) then
			ui_state.selected_weapon_id = id
		end
		imgui.Separator()
		if imgui.MenuItemBool(L("weapon_rp.text.id_119"), "", false, true) then
			imgui.SetClipboardText(tostring(id))
		end
		if imgui.MenuItemBool(L("weapon_rp.text.text_120"), "", false, true) then
			imgui.SetClipboardText(weapon_name)
		end
		if DEFAULT_WEAPONS[id] ~= nil then
			imgui.Separator()
			if imgui.MenuItemBool(L("weapon_rp.text.text_114"), "", false, true) then
				reset_weapon_to_default(id)
			end
		end
		imgui.EndPopup()
	end

	-- === 3. Иконка (только DrawList, без интерактива) ===
	draw_icon_cell(dl, id, start.x + 2, start.y)

	-- === 4. Hover-подсветка ===
	if sel_hovered then
		dl:AddRectFilled(
			start,
			imgui.ImVec2(start.x + avail_w, start.y + ROW_H),
			imgui.GetColorU32Vec4(imgui.ImVec4(1, 1, 1, 0.04)), 3)
	end

	-- === Маркер «текущее оружие» ===
	if is_current then
		local marker_x = start.x + avail_w - 14
		local marker_y = start.y + math.floor((ROW_H - imgui.GetTextLineHeight()) / 2)
		dl:AddText(
			imgui.ImVec2(marker_x, marker_y),
			imgui.GetColorU32Vec4(imgui.ImVec4(0.35, 0.90, 0.35, 0.85)),
			">")
	end

	-- === Продвинуть курсор на следующую строку ===
	imgui.SetCursorScreenPos(imgui.ImVec2(start.x, start.y + ROW_H + 2))
end

-- Панель редактирования выбранного оружия (правая часть мастер-деталь).
-- Содержит превью RP-строк и все настройки оружия.
local function draw_weapon_detail_panel(id)
	local w      = winfo(id)
	local prefix = "detail_" .. tostring(id) .. "_"

	-- Заголовок
	imgui.TextColored(imgui.ImVec4(0.80, 0.80, 0.50, 1.0), (L("weapon_rp.text.id_number")):format(id))
	if is_custom_weapon(id) then
		imgui.SameLine()
		imgui.TextColored(imgui.ImVec4(0.50, 0.85, 0.50, 1.0), L("weapon_rp.text.filter_custom_toggle"))
	end
	imgui.Separator()

	-- Превью RP-строк
	imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0.10, 0.10, 0.15, 0.90))
	imgui.BeginChild("##detail_preview", imgui.ImVec2(0, 86), true)

	local saved_flavor = M.config.flavor_level
	M.config.flavor_level = 1
	local line_shown   = M.makeRpLine("shown",   id, 0)
	local line_hidden  = M.makeRpLine("hidden", 0, id)
	local line_changed = M.makeRpLine("changed", id, 22)
	M.config.flavor_level = saved_flavor

	local function rp_preview_line(tag, tag_col, line)
		imgui.TextColored(tag_col, tag)
		imgui.SameLine()
		if line then
			local len   = str_len(line)
			local col   = len > M.config.max_len
				and imgui.ImVec4(1.0, 0.40, 0.40, 1.0)
				or  imgui.ImVec4(1.0, 1.0,  1.0,  1.0)
			imgui.TextColored(col, line:gsub("%%", "%%%%"))
			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.65, 0.65, 0.65, 1.0), ("(%d)"):format(len))
		else
			imgui.TextDisabled("---")
		end
	end
	rp_preview_line(L("weapon_rp.text.text_99"), imgui.ImVec4(0.50, 0.90, 0.50, 1.0), line_shown)
	rp_preview_line(L("weapon_rp.text.text_100"),  imgui.ImVec4(0.90, 0.50, 0.50, 1.0), line_hidden)
	rp_preview_line(L("weapon_rp.text.text_101"), imgui.ImVec4(0.50, 0.70, 0.90, 1.0), line_changed)

	imgui.EndChild()
	imgui.PopStyleColor()

	imgui.Separator()

	-- Включить/выключить
	local en = ui_bool(prefix .. "enable", w.enable ~= false)
	if imgui.Checkbox(L("weapon_rp.text.d"), en) then
		w.enable = en[0]
		M.config.weapons[id] = w
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.rp"))

	-- Полное имя
	local changed_nm, nm = input_text_dynamic(L("weapon_rp.text.d_121"), prefix .. "name", w.name or "", 96)
	if changed_nm then
		w.name = ffi.string(nm)
		if (w.short or "") == "" then w.short = w.name end
		M.config.weapons[id] = w
		mark_dirty()
	end

	-- Короткое имя
	local changed_sh, sh = input_text_dynamic(L("weapon_rp.text.d_122"), prefix .. "short", w.short or "", 48)
	if changed_sh then
		w.short = ffi.string(sh)
		M.config.weapons[id] = w
		mark_dirty()
	end

	-- Откуда / Куда
	local changed_fr, fr = input_text_dynamic(L("weapon_rp.text.dfrom"), prefix .. "from", w.from or "", 48)
	if changed_fr then
		w.from = ffi.string(fr)
		M.config.weapons[id] = w
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.text_107"))

	local changed_to, to = input_text_dynamic(L("weapon_rp.text.dto"), prefix .. "to", w.to or "", 48)
	if changed_to then
		w.to = ffi.string(to)
		M.config.weapons[id] = w
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.text_123"))

	-- Глаголы
	local changed_showv, showv = input_text_dynamic(
		L("weapon_rp.text.d_124"),
		prefix .. "show_verbs",
		table.concat(w.verbs.show or {}, ","),
		160
	)
	if changed_showv then
		local vv = funcs.parseList(ffi.string(showv))
		w.verbs.show = #vv > 0 and vv or { L("weapon_rp.text.text_82") }
		M.config.weapons[id] = w
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.text_111"))

	local changed_hidev, hidev = input_text_dynamic(
		L("weapon_rp.text.d_125"),
		prefix .. "hide_verbs",
		table.concat(w.verbs.hide or {}, ","),
		160
	)
	if changed_hidev then
		local vv = funcs.parseList(ffi.string(hidev))
		w.verbs.hide = #vv > 0 and vv or { L("weapon_rp.text.text_83") }
		M.config.weapons[id] = w
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.text_113"))

	imgui.Separator()

	-- Кнопки управления
	if DEFAULT_WEAPONS[id] then
		if imgui.Button(L("weapon_rp.text.d_126")) then
			reset_weapon_to_default(id)
		end
		tooltip(L("weapon_rp.text.text_115"))
		imgui.SameLine()
	end
	if imgui.Button(L("weapon_rp.text.d_127")) then
		ui_state.selected_weapon_id = nil
	end
	tooltip(L("weapon_rp.text.text_128"))
end

-- основная панель
function M.DrawSettingsInline()
	-- ===== СТАТУС-ПАНЕЛЬ =====
	local status_text, status_color = get_status_text()
	imgui.PushStyleColor(imgui.Col.ChildBg, imgui.ImVec4(0.12, 0.12, 0.18, 0.95))
	imgui.BeginChild("##status_panel", imgui.ImVec2(0, 90), true)

	-- Статус модуля
	imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), L("weapon_rp.text.text_129"))
	imgui.SameLine()
	imgui.TextColored(status_color, status_text)

	-- Текущее оружие
	local current_weapon = get_current_weapon_id()
	imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), L("weapon_rp.text.text_130"))
	imgui.SameLine()
	if current_weapon and current_weapon ~= 0 then
		local w = winfo(current_weapon)
		local weapon_name = w.name ~= nil and w.name or "-"
		imgui.Text(L("weapon_rp.text.current_weapon", {
			id = current_weapon,
			name = weapon_name,
		}))
	else
		imgui.TextDisabled(L("weapon_rp.text.text_131"))
	end

	-- Последняя RP-строка
	imgui.TextColored(imgui.ImVec4(0.7, 0.7, 0.7, 1.0), L("weapon_rp.text.rp_132"))
	imgui.SameLine()
	if ui_state.last_rp_line and ui_state.last_rp_line ~= "" then
		local last_rp = ui_state.last_rp_line
		imgui.TextWrapped(last_rp:gsub("%%", "%%%%"))
	else
		imgui.TextDisabled("—")
	end

	imgui.EndChild()
	imgui.PopStyleColor()

	-- ===== КНОПКИ УПРАВЛЕНИЯ =====
	local run = ui_bool("running", M.isEnabled())
	if imgui.Checkbox(L("weapon_rp.text.text_133"), run) then
		M.setEnabled(run[0], { flush_project = true })
	end
	tooltip(L("weapon_rp.text.text_134"))

	imgui.SameLine()
	local mode = ui_int("auto_mode", M.config.auto_mode)
	if imgui.Combo(L("weapon_rp.text.text_135"), mode, mode_labels_ffi, #mode_labels) then
		M.config.auto_mode = mode[0]
		mark_dirty()
	end
	tooltip(L("weapon_rp.text.rp_136"))

	imgui.SameLine()
	if ui_state.dirty then
		imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.2, 0.7, 0.2, 0.8))
		if imgui.Button(ICON_SAVE .. L("weapon_rp.text.text_137")) then
			save_cfg_with_ui_state()
		end
		imgui.PopStyleColor()
		tooltip(L("weapon_rp.text.text_138"))
	else
		imgui.PushStyleColor(imgui.Col.Button, imgui.ImVec4(0.3, 0.3, 0.3, 0.5))
		imgui.Button(ICON_SAVE .. L("weapon_rp.text.text_139"))
		imgui.PopStyleColor()
	end

	imgui.Separator()

	-- ===== TABBAR =====
	if imgui.BeginTabBar("##weapon_rp_tabs") then
		-- ВКЛАДКА: Основное
		if imgui.BeginTabItem(L("weapon_rp.text.text_140")) then
			local ign = ui_bool("ignore_knuckles", M.config.ignore_knuckles)
			if imgui.Checkbox(L("weapon_rp.text.text_141"), ign) then
				M.config.ignore_knuckles = ign[0]
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.rp_id_1"))

			local changed_prefix, prefix = input_text_dynamic(L("weapon_rp.text.text_142"), "prefix", (M.config.prefix or ""), 16)
			if changed_prefix then
				M.config.prefix = ffi.string(prefix)
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.me_do_try"))

			local flavor = ui_int("flavor_level", M.config.flavor_level)
			if imgui.SliderInt(L("weapon_rp.text.text_143"), flavor, 1, 3) then
				M.config.flavor_level = flavor[0]
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_1_2_3"))

			local max_len = ui_int("max_len", M.config.max_len)
			if imgui.InputInt(L("weapon_rp.text.rp_144"), max_len) then
				M.config.max_len = math.max(30, max_len[0])
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_96_145"))

			local min_gap = ui_int("min_me_gap_ms", M.config.min_me_gap_ms)
			if imgui.InputInt(L("weapon_rp.text.me_146"), min_gap) then
				M.config.min_me_gap_ms = math.max(0, min_gap[0])
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.rp_147"))

			imgui.EndTabItem()
		end

		-- ВКЛАДКА: Слова
		if imgui.BeginTabItem(L("weapon_rp.text.text_148")) then
			imgui.Text(L("weapon_rp.text.text_149"))
			for _, preset in ipairs(words_presets) do
				if imgui.Button(preset.title) then
					M.config.adverbs_show = funcs.deepcopy(preset.data.adverbs_show)
					M.config.adverbs_hide = funcs.deepcopy(preset.data.adverbs_hide)
					M.config.connectors_full = funcs.deepcopy(preset.data.connectors_full)
					M.config.connectors_short = funcs.deepcopy(preset.data.connectors_short)
					mark_dirty()
				end
				imgui.SameLine()
			end
			imgui.NewLine()

			imgui.Separator()

			local changed_adv_show, adv_show = input_text_multiline_dynamic(
				L("weapon_rp.text.text_150"),
				"adverbs_show",
				(table.concat(M.config.adverbs_show or {}, "\n")),
				1024,
				imgui.ImVec2(0, 80)
			)
			if changed_adv_show then
				local utf8_lines = funcs.parseLines(ffi.string(adv_show))
				M.config.adverbs_show = {}
				for _, line in ipairs(utf8_lines) do
					table.insert(M.config.adverbs_show, (line))
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_151"))

			local changed_adv_hide, adv_hide = input_text_multiline_dynamic(
				L("weapon_rp.text.text_152"),
				"adverbs_hide",
				(table.concat(M.config.adverbs_hide or {}, "\n")),
				1024,
				imgui.ImVec2(0, 80)
			)
			if changed_adv_hide then
				local utf8_lines = funcs.parseLines(ffi.string(adv_hide))
				M.config.adverbs_hide = {}
				for _, line in ipairs(utf8_lines) do
					table.insert(M.config.adverbs_hide, line)
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_153"))

			local changed_conn_full, conn_full = input_text_multiline_dynamic(
				L("weapon_rp.text.text_154"),
				"connectors_full",
				(table.concat(M.config.connectors_full or {}, "\n")),
				512,
				imgui.ImVec2(0, 80)
			)
			if changed_conn_full then
				local utf8_lines = funcs.parseLines(ffi.string(conn_full), true)
				M.config.connectors_full = {}
				for _, line in ipairs(utf8_lines) do
					table.insert(M.config.connectors_full, line)
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_155"))

			local changed_conn_short, conn_short = input_text_multiline_dynamic(
				L("weapon_rp.text.text_156"),
				"connectors_short",
				(table.concat(M.config.connectors_short or {}, "\n")),
				512,
				imgui.ImVec2(0, 80)
			)
			if changed_conn_short then
				local utf8_lines = funcs.parseLines(ffi.string(conn_short), true)
				M.config.connectors_short = {}
				for _, line in ipairs(utf8_lines) do
					table.insert(M.config.connectors_short, (line))
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_157"))

			imgui.EndTabItem()
		end

		-- ВКЛАДКА: Оружие
		if imgui.BeginTabItem(L("weapon_rp.text.text_158")) then

			-- Авто-предзагрузка иконок при первом открытии вкладки (FFI-режимы 2/3).
			if not icon_auto_preload_triggered then
				icon_auto_preload_triggered = true
				local m = M.config.weapon_icon_mode
				if (m == 2 or m == 3) and not icon_preload_status.done and not icon_preload_status.running then
					start_preload_weapon_icons(m == 2)
				end
			end

			-- === СТРОКА ПОИСКА ===
			local line_w = imgui.GetContentRegionAvail().x
			M._search_text = M._search_text or ""
			imgui.SetNextItemWidth(math.max(60, line_w - 100))
			local _, search_buf = input_text_dynamic("##search", "weapon_search", M._search_text, 64)
			M._search_text = ffi.string(search_buf)
			tooltip(L("weapon_rp.text.id_159"))
			local query = M._search_text:lower()

			imgui.SameLine()
			if imgui.Button(L("common.clear_compact") .. "##clrsrch", imgui.ImVec2(22, 0)) then
				M._search_text = ""
			end
			tooltip(L("weapon_rp.text.text_160"))

			-- === СТРОКА СОРТИРОВКИ И ФИЛЬТРОВ ===
			imgui.SetNextItemWidth(132)
			local sort_mode = ui_int("weapon_sort_mode", ui_state.weapon_sort_mode)
			if imgui.Combo("##sort", sort_mode, weapon_sort_labels_ffi, #weapon_sort_labels) then
				ui_state.weapon_sort_mode = sort_mode[0]
			end
			tooltip(L("weapon_rp.text.text_161"))

			imgui.SameLine()
			if imgui.Button(ui_state.sort_ascending and "Asc##sd" or "Dsc##sd", imgui.ImVec2(36, 0)) then
				ui_state.sort_ascending = not ui_state.sort_ascending
			end
			tooltip(L("weapon_rp.text.text_162"))

			-- Фильтры-чипы
			imgui.SameLine()
			imgui.PushStyleColor(imgui.Col.Button,
				ui_state.filter_enabled_only
					and imgui.ImVec4(0.18, 0.60, 0.18, 0.90)
					or  imgui.ImVec4(0.25, 0.25, 0.28, 0.70))
			if imgui.Button(L("weapon_rp.text.fen"), imgui.ImVec2(40, 0)) then
				ui_state.filter_enabled_only = not ui_state.filter_enabled_only
			end
			imgui.PopStyleColor()
			tooltip(L("weapon_rp.text.text_163"))

			imgui.SameLine()
			imgui.PushStyleColor(imgui.Col.Button,
				ui_state.filter_custom_only
					and imgui.ImVec4(0.18, 0.42, 0.65, 0.90)
					or  imgui.ImVec4(0.25, 0.25, 0.28, 0.70))
			if imgui.Button(L("weapon_rp.text.filter_custom_toggle") .. "##fcu", imgui.ImVec2(34, 0)) then
				ui_state.filter_custom_only = not ui_state.filter_custom_only
			end
			imgui.PopStyleColor()
			tooltip(L("weapon_rp.text.text_164"))

			imgui.SameLine()
			if imgui.Button(L("weapon_rp.text.cur")) then
				local cw = get_current_weapon_id()
				if cw then
					ui_state.scroll_to_weapon_id  = cw
					ui_state.selected_weapon_id   = cw
					ui_state.scroll_highlight_id  = cw
					ui_state.scroll_highlight_time = os.clock()
				end
			end
			tooltip(L("weapon_rp.text.text_165"))

			-- Строим видимый список (нужен для счётчика и массовых действий)
			local sorted_ids = get_sorted_weapon_ids()
			local visible_weapons = {}
			for _, wid in ipairs(sorted_ids) do
				local ww = M.config.weapons[wid]
				if weapon_matches_filters(wid, ww, query) then
					table.insert(visible_weapons, { id = wid, w = ww })
				end
			end
			local total_count = 0
			for _ in pairs(M.config.weapons) do total_count = total_count + 1 end

			imgui.SameLine()
			imgui.TextColored(imgui.ImVec4(0.55, 0.55, 0.60, 1.0),
				("%d/%d"):format(#visible_weapons, total_count))

			-- === МАССОВЫЕ ДЕЙСТВИЯ ===
			if imgui.Button(L("weapon_rp.text.mass"), imgui.ImVec2(72, 0)) then
				for _, entry in ipairs(visible_weapons) do
					entry.w.enable = true
					M.config.weapons[entry.id] = entry.w
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_166"))

			imgui.SameLine()
			if imgui.Button(L("weapon_rp.text.mass_167"), imgui.ImVec2(76, 0)) then
				for _, entry in ipairs(visible_weapons) do
					entry.w.enable = false
					M.config.weapons[entry.id] = entry.w
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_168"))

			imgui.SameLine()
			if imgui.Button(L("weapon_rp.text.mass_169"), imgui.ImVec2(68, 0)) then
				for _, entry in ipairs(visible_weapons) do
					entry.w.enable = not (entry.w.enable ~= false)
					M.config.weapons[entry.id] = entry.w
				end
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_170"))

			imgui.SameLine()
			if imgui.Button(L("weapon_rp.text.addw")) then
				imgui.OpenPopup("weapon_add_popup")
			end

			-- Попап добавления нового оружия (без изменений)
			if imgui.BeginPopup("weapon_add_popup") then
				M._new_id    = M._new_id    or imgui.new.int(0)
				M._new_name_text  = M._new_name_text  or ""
				M._new_short_text = M._new_short_text or ""
				M._new_from_text  = M._new_from_text  or L("weapon_rp.text.text_80")
				M._new_to_text    = M._new_to_text    or L("weapon_rp.text.text_81")
				M._new_show_text  = M._new_show_text  or L("weapon_rp.text.text_171")
				M._new_hide_text  = M._new_hide_text  or L("weapon_rp.text.text_172")

				imgui.InputInt(L("common.id"), M._new_id)
				imgui.SameLine()
				if imgui.Button(L("weapon_rp.text.text_173")) then
					local cw = get_current_weapon_id()
					if cw then M._new_id[0] = cw end
				end
				local _, new_name_buf = input_text_dynamic(L("weapon_rp.text.text_174"), "new_weapon_name", M._new_name_text, 64)
				local _, new_short_buf = input_text_dynamic(L("weapon_rp.text.text_175"), "new_weapon_short", M._new_short_text, 32)
				local _, new_from_buf = input_text_dynamic(L("weapon_rp.text.text_106"), "new_weapon_from", M._new_from_text, 32)
				local _, new_to_buf = input_text_dynamic(L("weapon_rp.text.text_108"), "new_weapon_to", M._new_to_text, 32)
				local _, new_show_buf = input_text_dynamic(L("weapon_rp.text.text_110"), "new_weapon_show", M._new_show_text, 128)
				local _, new_hide_buf = input_text_dynamic(L("weapon_rp.text.text_112"), "new_weapon_hide", M._new_hide_text, 128)
				M._new_name_text = ffi.string(new_name_buf)
				M._new_short_text = ffi.string(new_short_buf)
				M._new_from_text = ffi.string(new_from_buf)
				M._new_to_text = ffi.string(new_to_buf)
				M._new_show_text = ffi.string(new_show_buf)
				M._new_hide_text = ffi.string(new_hide_buf)

				if imgui.Button(L("common.ok") .. "##addw") then
					local nid    = M._new_id[0]
					local nm     = M._new_name_text or ""
					local sh     = M._new_short_text or ""
					local fr     = M._new_from_text or ""
					local to     = M._new_to_text or ""
					local sv     = funcs.parseList(M._new_show_text or "")
					local hv     = funcs.parseList(M._new_hide_text or "")
					if nm ~= "" then
						M.config.weapons[nid] = normalize_weapon(nid, {
							enable = true, name = nm,
							short  = (sh ~= "" and sh or nm),
							from   = fr, to = to,
							verbs  = {
								show = #sv > 0 and sv or { L("weapon_rp.text.text_82") },
								hide = #hv > 0 and hv or { L("weapon_rp.text.text_83") },
							},
						})
						mark_dirty()
					end
					imgui.CloseCurrentPopup()
				end
				imgui.SameLine()
				if imgui.Button(L("weapon_rp.text.addw_176")) then imgui.CloseCurrentPopup() end
				imgui.EndPopup()
			end

			imgui.Separator()

			-- === МАСТЕР-ДЕТАЛЬ ===
			local sel_id       = ui_state.selected_weapon_id
			local has_sel      = sel_id ~= nil and M.config.weapons[sel_id] ~= nil
			local content      = imgui.GetContentRegionAvail()
			local list_w       = has_sel and math.floor(content.x * 0.46) or content.x

			-- Список слева
			imgui.BeginChild("##weapon_list", imgui.ImVec2(list_w, 0), false)
			for row_idx, entry in ipairs(visible_weapons) do
				local wid    = entry.id
				local ww     = entry.w
				local is_cur = (current_weapon == wid)

				if ui_state.scroll_to_weapon_id == wid then
					imgui.SetScrollHereY(0.5)
					ui_state.scroll_to_weapon_id = nil
				end

				draw_weapon_row(wid, ww, is_cur, row_idx)
			end
			if #visible_weapons == 0 then
				imgui.TextDisabled(L("weapon_rp.text.text_177"))
			end
			imgui.EndChild()

			-- Панель редактирования справа
			if has_sel then
				imgui.SameLine()
				imgui.BeginChild("##weapon_detail", imgui.ImVec2(0, 0), true)
				draw_weapon_detail_panel(sel_id)
				imgui.EndChild()
			end

			imgui.EndTabItem()
		end

		-- ВКЛАДКА: Расширенное
		if imgui.BeginTabItem(L("weapon_rp.text.text_178")) then
			imgui.TextWrapped(L("weapon_rp.text.text_179"))
			imgui.Separator()

			-- Режим иконок оружия
			imgui.Text(L("weapon_rp.text.text_180"))
			local ico_ptr = ui_int("weapon_icon_mode_ui", M.config.weapon_icon_mode)

			if imgui.RadioButtonIntPtr(L("weapon_rp.text.ico"), ico_ptr, 0) then
				M.config.weapon_icon_mode = 0
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.ffi"))

			if imgui.RadioButtonIntPtr(L("weapon_rp.text.ffi_ico"), ico_ptr, 2) then
				M.config.weapon_icon_mode = 2
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_181"))

			if imgui.RadioButtonIntPtr(L("weapon_rp.text.ffi_ico_182"), ico_ptr, 3) then
				M.config.weapon_icon_mode = 3
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.txd"))

			-- Кнопка предзагрузки и статус для режимов 2 и 3
			if M.config.weapon_icon_mode == 2 or M.config.weapon_icon_mode == 3 then
				imgui.Spacing()
				if icon_preload_status.running then
					imgui.TextColored(imgui.ImVec4(1.0, 0.8, 0.2, 1.0),
						(L("weapon_rp.text.number_number")):format(
							icon_preload_status.loaded + icon_preload_status.failed,
							icon_preload_status.total))
				else
					local btn_label = icon_preload_status.done and L("weapon_rp.text.preload") or L("weapon_rp.text.preload_183")
					if imgui.Button(btn_label) then
						start_preload_weapon_icons(M.config.weapon_icon_mode == 2)
					end
					if icon_preload_status.done then
						imgui.SameLine()
						imgui.TextColored(
							imgui.ImVec4(0.3, 0.9, 0.3, 1.0),
							L("weapon_rp.text.preload_ok", { count = icon_preload_status.loaded })
						)
						if icon_preload_status.failed > 0 then
							imgui.SameLine()
							imgui.TextColored(imgui.ImVec4(1.0, 0.4, 0.4, 1.0),
								(L("weapon_rp.text.number_184")):format(icon_preload_status.failed))
						end
					end
				end

				if not icon_preload_status.running and (icon_preload_status.loaded + icon_preload_status.failed) > 0 then
					imgui.SameLine()
					if imgui.Button(L("weapon_rp.text.preload_185")) then
						clear_icon_cache()
					end
				end
			end

			imgui.Separator()

			local tick = ui_int("tick_ms", M.config.tick_ms)
			if imgui.InputInt(L("weapon_rp.text.text_186"), tick) then
				M.config.tick_ms = math.max(10, tick[0])
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_80_187"))

			local stable = ui_int("stable_need", M.config.stable_need)
			if imgui.InputInt(L("weapon_rp.text.text_188"), stable) then
				M.config.stable_need = math.max(1, stable[0])
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_3_189"))

			local cooldown = ui_int("cooldown_frames", M.config.cooldown_frames)
			if imgui.InputInt(L("weapon_rp.text.text_190"), cooldown) then
				M.config.cooldown_frames = math.max(0, cooldown[0])
				mark_dirty()
			end
			tooltip(L("weapon_rp.text.text_10_191"))

			imgui.EndTabItem()
		end

		imgui.EndTabBar()
	end
end

-- ===== публичные сеттеры (сохранение) =====
function M.setAutoRp(b)
	M.config.auto_mode = b and 0 or 1
	save_cfg()
end
function M.setSender(fn)
	M.config.sender = fn
	save_cfg()
end
function M.setPrefix(p)
	M.config.prefix = tostring(p or "/me")
	save_cfg()
end
function M.setStableNeed(n)
	M.config.stable_need = math.max(1, tonumber(n) or 3)
	save_cfg()
end
function M.setCooldownFrames(n)
	M.config.cooldown_frames = math.max(0, tonumber(n) or 0)
	save_cfg()
end
function M.setIgnoreKnuckles(b)
	M.config.ignore_knuckles = not not b
	save_cfg()
end
function M.setMinMeGapMs(n)
	M.config.min_me_gap_ms = math.max(0, tonumber(n) or 0)
	save_cfg()
end
function M.setFlavorLevel(n)
	M.config.flavor_level = math.max(1, math.min(3, tonumber(n) or 2))
	save_cfg()
end
function M.setMaxLen(n)
	M.config.max_len = math.max(30, tonumber(n) or 96)
	save_cfg()
end

function M.onTerminate(reason)
	M.stop()
	if event_bus then
		event_bus.offByOwner("weapon_rp")
	end
	clear_icon_cache()
	if dynamic_input_resize_callback_ptr and type(dynamic_input_resize_callback_ptr.free) == "function" then
		pcall(dynamic_input_resize_callback_ptr.free, dynamic_input_resize_callback_ptr)
		dynamic_input_resize_callback_ptr = nil
	end
end

return M
