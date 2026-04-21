# -*- coding: utf-8 -*-
"""Генерация расширенного тестового HelperByOrc.json для биндера (русские подписи)."""
import json
from copy import deepcopy

OUT = r"C:\Games\SAMP\GTA SAMP\scripts\HelperByOrc.json"

FALSE9 = [False] * 9

def T(*, label, folder, keys=None, hotkey_mode="modifier_trigger", messages=None, command="",
      command_enabled=False, conditions=FALSE9, quick_conditions=FALSE9,
      conditions_combine="require_all", quick_conditions_combine="require_all",
      quick_menu=False,
      enabled=True, repeat_mode=False, repeat_interval_ms=500,
      text_trigger=None, text_confirmation=None, command_confirmation=None, inputs=None):
    if keys is None:
        keys = []
    if messages is None:
        messages = [{"interval_ms": 0, "method": 9, "text": label}]
    if text_trigger is None:
        text_trigger = {"enabled": False, "pattern": False, "text": ""}
    if text_confirmation is None:
        text_confirmation = {
            "enabled": False, "key": 49, "cancel_key": 50, "wait_for_resolution": True,
        }
    if command_confirmation is None:
        command_confirmation = {"enabled": False, "wait_for_resolution": True}
    if inputs is None:
        inputs = []
    return {
        "label": label,
        "folder_path": folder,
        "keys": keys,
        "hotkey_mode": hotkey_mode,
        "messages": messages,
        "conditions": list(conditions),
        "quick_conditions": list(quick_conditions),
        "conditions_combine": conditions_combine,
        "quick_conditions_combine": quick_conditions_combine,
        "quick_menu": quick_menu,
        "enabled": enabled,
        "repeat_mode": repeat_mode,
        "repeat_interval_ms": repeat_interval_ms,
        "command": command,
        "command_enabled": command_enabled,
        "text_trigger": text_trigger,
        "text_confirmation": text_confirmation,
        "command_confirmation": command_confirmation,
        "inputs": inputs,
    }


def folder_node(name, children, quick_menu=True, quick_conditions=FALSE9,
                quick_conditions_combine="require_all"):
    return {
        "name": name,
        "children": children,
        "quick_menu": quick_menu,
        "quick_conditions": list(quick_conditions),
        "quick_conditions_combine": quick_conditions_combine,
    }


METHOD_NAMES_RU = {
    0: "Локальный чат",
    1: "Через SA:MP",
    2: "Прямая отправка",
    3: "Только сценарий (без отправки)",
    4: "Вставить в чат",
    5: "Открыть чат с текстом",
    6: "Диалог",
    7: "Буфер обмена",
    8: "Лог / отладка",
    9: "Тост (уведомление)",
}


def main():
    root_folder_name = "Тесты_HelperByOrc"
    base = [root_folder_name]

    folders = [
        folder_node(
            "Тесты_HelperByOrc",
            [
                folder_node("01_Горячие_клавиши_и_режимы", []),
                folder_node("02_Все_методы_отправки_шагов", []),
                folder_node("03_Текстовые_триггеры", []),
                folder_node("04_Подтверждения", []),
                folder_node("05_Команды_и_аргументы", []),
                folder_node("06_Теги_сценарии_задержки", []),
                folder_node("07_Диалоги_ввода", []),
                folder_node("08_Условия_игры", []),
                folder_node("09_Quick_меню", [
                    folder_node("Видны_всегда", []),
                    # Семантика «скрыть пока чат/диалог открыт» без инверсии условий в JSON не выразить;
                    # папки оставлены для ручной настройки или будущих отрицаний.
                    folder_node("Скрыть_если_чат", []),
                    folder_node("Скрыть_если_диалог", []),
                ]),
                folder_node("10_Стресс_и_объём", []),
                folder_node("11_Граничные_случаи", []),
            ],
        )
    ]

    hotkeys = []

    # —— 01 Горячие клавиши
    p01 = base + ["01_Горячие_клавиши_и_режимы"]
    hotkeys += [
        T(label="Модификатор+клавиша: Ctrl+1 — тост проверки", folder=p01, keys=[17, 49],
          messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Модификатор-триггер: Ctrl+1. Время: [timef(%H:%M:%S;)]"}]),
        T(label="Порядковое комбо: Q→E→R (ordered)", folder=p01, keys=[81, 69, 82],
          hotkey_mode="ordered_combo",
          messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Упорядоченное комбо Q→E→R выполнено."}]),
        T(label="Повтор с интервалом: Ctrl+R удерживать", folder=p01, keys=[17, 82],
          repeat_mode=True, repeat_interval_ms=200,
          messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Повтор [timef(%H:%M:%S;)]"}]),
        T(label="Отключённый бинд (не должен срабатывать)", folder=p01, keys=[17, 57],
          enabled=False,
          messages=[{"interval_ms": 0, "method": 9, "text": "Ошибка: отключённый бинд сработал"}]),
    ]

    # —— 02 Каждый method 0..9
    p02 = base + ["02_Все_методы_отправки_шагов"]
    # Ctrl+F1 … Ctrl+F10 — без пересечения с другими Ctrl-цифрами
    for m in range(10):
        vk_f = 112 + m  # VK_F1 = 112 … VK_F10 = 121
        hotkeys.append(
            T(
                label=f"Метод шага {m}: {METHOD_NAMES_RU[m]}",
                folder=p02,
                keys=[17, vk_f],
                messages=[
                    {
                        "interval_ms": 0,
                        "method": m,
                        "text": (
                            f"[ТЕСТ] Метод {m} — {METHOD_NAMES_RU[m]}. "
                            "Проверьте, что сообщение ушло ожидаемым каналом (чат / SA:MP / тост / лог и т.д.)."
                        ),
                    }
                ],
            )
        )

    # —— 03 Текстовые триггеры
    p03 = base + ["03_Текстовые_триггеры"]
    hotkeys += [
        T(label="Входящий чат: точное совпадение «тест тигр»", folder=p03, keys=[],
            text_trigger={"enabled": True, "pattern": False, "text": "тест тигр"},
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Сработало точное совпадение входящего текста."}]),
        T(label="Входящий чат: регекс по маске числа", folder=p03, keys=[],
            text_trigger={"enabled": True, "pattern": True, "text": r"\d{3,}"},
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Регекс нашёл число из 3+ цифр."}]),
        T(label="Исходящий чат: триггер по своему сообщению", folder=p03, keys=[],
            text_trigger={"enabled": True, "pattern": False, "text": "hb_out_ping"},
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Ответ на ваш исходящий триггер."}]),
    ]

    # —— 04 Подтверждения
    p04 = base + ["04_Подтверждения"]
    hotkeys += [
        T(label="Подтверждение текста: клавиши 1/2 (текстовое)", folder=p04, keys=[16, 89],  # Shift+Y
            text_confirmation={"enabled": True, "key": 49, "cancel_key": 50, "wait_for_resolution": True},
            messages=[{"interval_ms": 0, "method": 0, "text": "[ТЕСТ] После подтверждения ключом 1 это уйдёт в локальный чат."}]),
        T(label="Подтверждение команды без текста: только команда", folder=p04, keys=[16, 85],
            command="/hb_confirm_cmd",
            command_enabled=True,
            command_confirmation={"enabled": True, "wait_for_resolution": True},
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Ожидание подтверждения команды."}]),
        T(label="Подтверждение: ждать явного отклонения", folder=p04, keys=[16, 73],
            text_confirmation={"enabled": True, "key": 49, "cancel_key": 50, "wait_for_resolution": True},
            command_confirmation={"enabled": False, "wait_for_resolution": True},
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Режим ожидания разрешения или отмены."}]),
    ]

    # —— 05 Команды
    p05 = base + ["05_Команды_и_аргументы"]
    hotkeys += [
        T(label="Команда с аргументами /hb_args а б в", folder=p05, keys=[], command="/hb_args", command_enabled=True,
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] arg1=[paramcmd(1)] arg2=[paramcmd(2)] хвост=[paramcmd(1+)]"}]),
        T(label="Слэш-команда в чат (если поддерживается сервером)", folder=p05, keys=[], command="/me тестирует биндер", command_enabled=True,
            messages=[{"interval_ms": 0, "method": 1, "text": ""}]),
    ]

    # —— 06 Теги, wait, orchestration
    p06 = base + ["06_Теги_сценарии_задержки"]
    hotkeys += [
        T(label="Время и дата в тосте", folder=p06, keys=[17, 84],
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] [timef(%H:%M:%S;)] | [timef(%Y-%m-%d;)]"}]),
        T(label="Сценарий: тост → пауза → тост", folder=p06, keys=[17, 71],
            messages=[
                {"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Шаг 1 из 3"},
                {"interval_ms": 0, "method": 3, "text": "[wait(400)]"},
                {"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Шаг 2 из 3"},
                {"interval_ms": 0, "method": 3, "text": "[wait(400)]"},
                {"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Шаг 3 из 3 — готово."},
            ]),
        T(label="Условный тег ifandor (пример)", folder=p06, keys=[],
            command="/hb_if_demo",
            command_enabled=True,
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Условие: [ifandor(1>0?да:нет)]"}]),
        T(
            label="Пользовательская переменная из тегов (custom_vars)",
            folder=p06,
            keys=[17, 99],  # Ctrl+Numpad3
            messages=[
                {
                    "interval_ms": 0,
                    "method": 9,
                    "text": "[ТЕСТ] Секция tags.custom_vars: {{тест_переменная}}",
                }
            ],
        ),
    ]

    # —— 07 Ввод: текст + кнопки + каскад (минимальные JSON-примеры)
    p07 = base + ["07_Диалоги_ввода"]
    hotkeys += [
        T(
            label="Ввод: одно текстовое поле",
            folder=p07,
            keys=[16, 49],  # Shift+1
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Вы ввели: «{{a}}»"}],
            inputs=[
                {
                    "key": "a",
                    "label": "Подпись поля",
                    "hint": "Введите произвольный текст для проверки UTF-8",
                    "mode": "text",
                    "multi_select": False,
                    "multi_separator": ", ",
                    "cascade_parent_key": "",
                    "buttons": [],
                }
            ],
        ),
        T(
            label="Ввод: список кнопок",
            folder=p07,
            keys=[16, 50],
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Выбран вариант: метка «{{choice}}»"}],
            inputs=[
                {
                    "key": "choice",
                    "label": "Выбор из списка",
                    "hint": "",
                    "mode": "buttons_list",
                    "multi_select": False,
                    "multi_separator": ", ",
                    "cascade_parent_key": "",
                    "buttons": [
                        {"label": "Вариант А", "text": "alpha", "hint": "Первая опция", "when": ""},
                        {"label": "Вариант Б", "text": "beta", "hint": "Вторая опция", "when": ""},
                        {"label": "Вариант В", "text": "gamma", "hint": "", "when": ""},
                    ],
                }
            ],
        ),
        T(
            label="Ввод: кнопки + доп. текст",
            folder=p07,
            keys=[16, 51],
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Кнопка «{{c}}», комментарий: {{note}}"}],
            inputs=[
                {
                    "key": "c",
                    "label": "Каскад/кнопки",
                    "hint": "",
                    "mode": "buttons_list_text",
                    "multi_select": False,
                    "multi_separator": ", ",
                    "cascade_parent_key": "",
                    "buttons": [
                        {"label": "Уровень 1", "text": "l1", "hint": "", "when": ""},
                        {"label": "Уровень 2", "text": "l2", "hint": "", "when": ""},
                    ],
                },
                {
                    "key": "note",
                    "label": "Комментарий",
                    "hint": "Поле после выбора",
                    "mode": "text",
                    "multi_select": False,
                    "multi_separator": ", ",
                    "cascade_parent_key": "",
                    "buttons": [],
                },
            ],
        ),
    ]

    # —— 08 Условия по одному флагу
    p08 = base + ["08_Условия_игры"]
    cond_names = [
        "В воде",
        "Мёртв",
        "В воздухе",
        "В транспорте",
        "Без оружия",
        "С оружием",
        "Пешком",
        "Чат открыт",
        "Диалог открыт",
    ]
    for i, title in enumerate(cond_names):
        c = [False] * 9
        c[i] = True
        hotkeys.append(
            T(
                label=f"Условие: только если — {title}",
                folder=p08,
                keys=[17, 65 + i],  # Ctrl+A, B, ...
                conditions=c,
                messages=[
                    {
                        "interval_ms": 0,
                        "method": 9,
                        "text": f"[ТЕСТ] Условие «{title}» выполнено; если не сработало — смените состояние персонажа.",
                    }
                ],
            )
        )

    # —— 09 Quick menu
    p09a = base + ["09_Quick_меню", "Видны_всегда"]
    p09b = base + ["09_Quick_меню", "Скрыть_если_чат"]
    p09c = base + ["09_Quick_меню", "Скрыть_если_диалог"]
    hotkeys += [
        T(label="Быстрое меню: всегда виден пункт", folder=p09a, keys=[], quick_menu=True,
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Quick-меню, папка «видны всегда»."}]),
        T(label="Быстрое меню: папка «чат» (условия папки вручную)", folder=p09b, keys=[], quick_menu=True,
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Настройте quick_conditions папки под новую схему «требовать»."}]),
        T(label="Быстрое меню: папка «диалог» (условия папки вручную)", folder=p09c, keys=[], quick_menu=True,
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Настройте quick_conditions папки под новую схему «требовать»."}]),
    ]

    # —— 10 Стресс: много шагов, длинные строки, интервалы
    p10 = base + ["10_Стресс_и_объём"]
    long_text = "Длинная строка для проверки буферов и кодировки: " + ("слово " * 120)
    stress_messages = [{"interval_ms": i * 15, "method": 9, "text": f"[СТРЕСС] пакет {i + 1}/25"} for i in range(25)]
    hotkeys += [
        T(
            label="Стресс: 25 тостов подряд с интервалом",
            folder=p10,
            keys=[16, 17, 122],  # Shift+Ctrl+F11
            messages=stress_messages,
        ),
        T(
            label="Стресс: очень длинный текст в одном шаге (локальный чат)",
            folder=p10,
            keys=[16, 17, 123],  # Shift+Ctrl+F12
            messages=[{"interval_ms": 0, "method": 0, "text": long_text}],
        ),
        T(
            label="Стресс: цепочка методов смешано",
            folder=p10,
            keys=[16, 17, 121],  # Shift+Ctrl+F10 (не пересекается с Ctrl+F10 как два ключа)
            messages=[
                {"interval_ms": 0, "method": 9, "text": "[СТРЕСС] начало цепочки"},
                {"interval_ms": 0, "method": 3, "text": "[wait(300)]"},
                {"interval_ms": 0, "method": 8, "text": "[СТРЕСС] запись в лог"},
                {"interval_ms": 0, "method": 9, "text": "[СТРЕСС] конец"},
            ],
        ),
    ]

    # —— 11 Граничные
    p11 = base + ["11_Граничные_случаи"]
    hotkeys += [
        T(
            label="Граница: минимальная задержка сценария [wait(1)]",
            folder=p11,
            keys=[17, 96],  # Ctrl+Numpad0
            messages=[{"interval_ms": 0, "method": 3, "text": "[wait(1)]"}],
        ),
        T(
            label="Граница: Unicode и спецсимволы в тосте",
            folder=p11,
            keys=[17, 97],  # Ctrl+Numpad1
            messages=[
                {
                    "interval_ms": 0,
                    "method": 9,
                    "text": "[ТЕСТ] Кавычки «ёлка», эмодзи 😀, символы §#%_& проверка",
                }
            ],
        ),
        T(
            label="Граница: повтор одного и того же текста тоста",
            folder=p11,
            keys=[17, 98],  # Ctrl+Numpad2
            messages=[{"interval_ms": 0, "method": 9, "text": "[ТЕСТ] Повторяющееся уведомление для проверки очереди"}],
        ),
    ]

    # Дополнительный стресс: серия однотипных биндов для прогона очереди и UI
    for i in range(1, 31):
        hotkeys.append(
            T(
                label=f"Стресс-серия №{i}: короткий тост",
                folder=p10,
                keys=[],  # только через быстрое меню / поиск по имени
                quick_menu=True,
                messages=[
                    {
                        "interval_ms": 0,
                        "method": 9,
                        "text": f"[СТРЕСС-{i:02d}] Дубликат для нагрузочного меню; время [timef(%H:%M:%S;)]",
                    }
                ],
            )
        )

    doc = {
        "schema_version": 1,
        "ui": {
            "language": "ru",
            "auto_scale": True,
            "scale_multiplier": 1.0,
            "open_menu_hotkey": [17, 90],
        },
        "tags": {
            "custom_vars": {
                "тест_переменная": "значение_RU",
                "stress_counter": "0",
                "player_ping_placeholder": "{{ping}}",
            }
        },
        "binder": {
            "quick_menu_hotkey": [18, 88],  # Alt+X — удобно для теста; смените при конфликте
            "quick_menu_activation_mode": "toggle",
            "folders": folders,
            "hotkeys": hotkeys,
        },
    }

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print("Written", OUT, "hotkeys:", len(hotkeys))


if __name__ == "__main__":
    main()
