#include "notepad_builtin_instruction.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <string>

namespace notepadbuiltin {
namespace {

constexpr std::string_view kDefaultId = "builtin:notepad-instruction";
constexpr std::string_view kTitle = "Инструкция";

constexpr std::string_view kInstruction = R"NOTEPAD(#center #font30 #color4FA3FF HelperByOrc — Блокнот
#center #font16 #colorB8C4D6 Встроенная инструкция
#hr4FA3FF

#quote Инструкция встроена в HelperByOrc, всегда доступна в корне Блокнота и не зависит от внешнего файла.

#font18 #colorFFD166 1. Возможности

#todo Создавай обычные заметки и папки любой вложенности.
#todo Клади живые TXT-файлы в HelperByOrc\notepad и редактируй их из игры или внешнего редактора.
#todo Используй поле слева для поиска строк по всем заметкам, а Ctrl+F — внутри открытой заметки.
#todo Закрепляй важные заметки в избранном и меняй порядок перетаскиванием.
#todo Редактируй исходный текст рядом с живым предпросмотром.
#todo Копируй raw-текст, очищенный текст или отдельные отображаемые строки.
#todo Импортируй и экспортируй TXT, добавляй локальные изображения и иконки.

#font18 #colorFFD166 2. Где хранятся данные

#color7DDBA3 GTA San Andreas User Files\HelperByOrc\notepad

#bullet index.json — метаданные, обычные заметки, порядок и выбранная заметка.
#bullet *.txt и подпапки — живые TXT-заметки.
#bullet images — локальные изображения.
#bullet export — экспортированные копии.
#bullet Блокнот общий для всех профилей HelperByOrc.

#quote При первом запуске новой версии данные активного профиля безопасно копируются в общую папку. Старые данные не удаляются и остаются резервной копией.

#font18 #colorFFD166 3. Интерфейс

#color4FA3FF Панель слева:
#bullet Плюс — создать заметку.
#bullet Папка — создать папку.
#bullet Импорт — импортировать TXT как обычную заметку.
#bullet Круговая стрелка — пересканировать живые TXT.
#bullet Строка поиска — искать по содержимому всех заметок.
#bullet Результат показывает заметку, папку, номер строки и найденный текст.
#bullet Клик по результату открывает и выделяет строку; кнопка копирования справа копирует её текст.

#color4FA3FF Панель заметки:
#bullet Изменить — открыть редактор.
#bullet Экспорт .txt — сохранить копию в notepad\export.
#bullet Подставлять теги — раскрывать теги HelperByOrc только для отображения.
#bullet Копировать raw — копировать исходник вместе с разметкой.
#bullet Копировать текст — копировать очищенный отображаемый текст.
#bullet Копировать строки — включить копирование отдельных строк.
#bullet Кнопка Разметка показывает синтаксис и вставляет шаблоны todo, done, quote, underline и strike.

#font18 #colorFFD166 4. Поиск

#bullet Поле слева ищет строки по содержимому всех заметок.
#bullet Глобальный результат показывает название заметки, папку, номер строки и найденный фрагмент.
#bullet Клик по глобальному результату открывает заметку, прокручивает и выделяет найденную строку.
#bullet Кнопка копирования справа от результата копирует всю отображаемую строку без управляющей разметки, включая ##sameline-продолжения.
#bullet Для поиска только в открытой заметке нажми Ctrl+F или кнопку над предпросмотром.
#bullet Поиск не зависит от регистра и проверяет отображаемый текст без управляющей разметки.
#bullet Enter или кнопка вниз переходят к следующему совпадению.
#bullet Shift+Enter или кнопка вверх переходят к предыдущему.
#bullet Кнопка копирования рядом со стрелками копирует всю активную отображаемую строку без управляющей разметки, включая ##sameline-продолжения.
#bullet Активная строка прокручивается в область просмотра и выделяется рамкой.
#bullet Кнопка закрытия очищает поиск и возвращает обычный предпросмотр.

#font18 #colorFFD166 5. Живые TXT

#bullet TXT обнаруживаются рекурсивно в общей папке notepad.
#bullet Внешние изменения подхватываются фоновым наблюдателем.
#bullet Редактирование, переименование, перемещение и удаление меняет реальный файл.
#bullet При перемещении сохраняются ID, избранное, порядок и ссылки HUD.
#bullet Если источник исчез, заметка и её ID сохраняются, пока пользователь не решит конфликт.
#bullet Поддерживаются UTF-8 с BOM и без BOM, UTF-16 LE/BE с BOM и Windows-1251.
#bullet При сохранении сохраняются исходные кодировка, BOM и тип перевода строк.

#quote Папки images и export, reparse points и корневой Инструкция.txt не сканируются как заметки.

#font18 #colorFFD166 6. Новая практичная разметка

#todo Незавершённый пункт: ##todo текст
#done Завершённый пункт: ##done текст
#quote Цитата или важная ремарка: ##quote текст

#underline Подчёркнутый текст: ##underline текст #reset обычный текст.
#strike Зачёркнутый текст: ##strike текст #reset обычный текст.

#font18 #colorFFD166 7. Цвета и inline-стили

Обычный текст, {4FA3FF}синий фрагмент, {FFD166}жёлтый фрагмент и #reset обычный текст.
#color7DDBA3 Цвет через маркер {RRGGBB} или директиву ##colorRRGGBB.
#bg334155 #colorF8FAFC Фон фрагмента. #bg #colorB8C4D6 Фон сброшен.
#alpha55 #colorFF8C8C Полупрозрачный фрагмент. #alpha #colorB8C4D6 Обычная прозрачность.
#shadow Текст с тенью. #reset #outline Текст с контуром.
#upper эта строка станет верхним регистром.
#lower ЭТА СТРОКА СТАНЕТ НИЖНИМ РЕГИСТРОМ.

#font18 #colorFFD166 8. Размеры, выравнивание и интервалы

#font12 Размер 12. #font14 Размер 14. #font16 Размер 16.
#font18 Размер 18. #font30 Размер 30. #font Обычный размер.
#small Короткая форма small — размер 12.
#big Короткая форма big — размер 18.
#left Выравнивание слева.
#center Выравнивание по центру.
#right Выравнивание справа.
#indent24 Отступ слева 24 пикселя.
#pad16 Дополнительный отступ 16 пикселей.
#tab2 Два шага табуляции.
#bullet Маркер списка.
#br1
#color8FA1B8 br добавляет вертикальный интервал, hr рисует горизонтальную линию.
#hr64748B

#font18 #colorFFD166 9. Составная строка

#color4FA3FF Статья 2.34.
#sameline #colorE2E8F0 Описание статьи продолжается в той же строке и переносится по ширине.
#sameline #colorFF8C8C Наказание: выговор.

#quote Несколько последовательных строк с sameline объединяются в один поток.

#font18 #colorFFD166 10. Иконки и изображения

#icon(star) Избранное
#icon(folder) Папка
#icon(file) Заметка
#icon(car) Транспорт

#bullet Иконка: ##icon(name) или ##icon(brand:name).
#bullet Изображение: ##img(file.png).
#bullet Размер изображения: ##img(file.png,size(320,180)).
#bullet Изображения загружаются только из общей папки notepad\images.
#bullet URL, абсолютные пути, .. и выход за пределы images блокируются.

#font18 #colorFFD166 11. Краткая шпаргалка

#quote Чтобы показать команду буквально, удвой первый #: ##colorFF0000 отображается как #colorFF0000.

Цвет: {RRGGBB}, ##colorRRGGBB
Фон: ##bgRRGGBB, сброс — ##bg
Прозрачность: ##alphaN, сброс — ##alpha
Шрифт: ##font12 ... ##font30, сброс — ##font
Размер: ##small, ##big
Выравнивание: ##left, ##center, ##right
Отступы: ##indentN, ##padN, ##tabN
Списки: ##bullet, ##todo, ##done
Цитата: ##quote
Регистр: ##upper, ##lower
Эффекты: ##shadow, ##outline, ##underline, ##strike
Перенос: ##brN
Линия: ##hr или ##hrRRGGBB
Иконка: ##icon(name)
Изображение: ##img(file.png,size(320,180))
Продолжение строки: ##sameline
Сброс inline-стиля: ##reset

#hr4FA3FF
#center #font14 #color8FA1B8 Встроенная инструкция соответствует текущей C++-реализации HelperByOrc.
)NOTEPAD";

std::wstring NormalizePath(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0) {
        return {};
    }
    std::wstring normalized(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            normalized.data(),
            required)
        <= 0) {
        return {};
    }
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
    return normalized;
}

} // namespace

std::string_view DefaultId() {
    return kDefaultId;
}

std::string_view Title() {
    return kTitle;
}

std::string_view InstructionText() {
    return kInstruction;
}

bool IsInstructionSource(std::string_view relativePath) {
    return IsInstructionSource(NormalizePath(relativePath));
}

bool IsInstructionSource(std::wstring_view relativePath) {
    std::wstring normalized(relativePath);
    std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
    constexpr std::wstring_view expected = L"Инструкция.txt";
    return normalized.size() == expected.size()
        && CompareStringOrdinal(
            normalized.data(),
            static_cast<int>(normalized.size()),
            expected.data(),
            static_cast<int>(expected.size()),
            TRUE)
        == CSTR_EQUAL;
}

} // namespace notepadbuiltin
