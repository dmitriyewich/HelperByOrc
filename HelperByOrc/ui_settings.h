#pragma once

#include "ui_fonts.h"

#include <imgui.h>

#include <string>
#include <vector>

#define APP_UI_TEXTS(X) \
    X(AppBrand, "HelperByOrc", "HelperByOrc") \
    X(LanguageRussian, "Русский", "Russian") \
    X(LanguageEnglish, "English", "English") \
    X(TabBinder, "Биндер", "Binder") \
    X(TabHud, "HUD", "HUD") \
    X(TabMisc, "Прочее", "Misc") \
    X(TabNotepad, "Блокнот", "Notepad") \
    X(TabSettings, "Настройки", "Settings") \
    X(TabBinderCompact, "БН", "BD") \
    X(TabHudCompact, "HUD", "HUD") \
    X(TabMiscCompact, "ПР", "MS") \
    X(TabNotepadCompact, "БЛ", "NP") \
    X(TabSettingsCompact, "НС", "ST") \
    X(HudSearchHint, "Поиск HUD-блоков", "Search HUD blocks") \
    X(HudWidgets, "HUD-блоки", "HUD blocks") \
    X(HudNoWidgets, "HUD-блоков пока нет. Создайте пустой блок или выберите шаблон.", "No HUD blocks yet. Create an empty block or choose a template.") \
    X(HudNoSelection, "Выберите HUD-блок слева или создайте новый.", "Select a HUD block on the left or create a new one.") \
    X(HudAddWidget, "Пустой HUD-блок", "Empty HUD block") \
    X(HudDuplicateWidget, "Дублировать", "Duplicate") \
    X(HudPresets, "Из шаблона", "From template") \
    X(HudPresetWeapon, "Оружие", "Weapon") \
    X(HudPresetFreeText, "Свободный текст", "Free text") \
    X(HudDefaultWidgetName, "Новый HUD-блок", "New HUD block") \
    X(HudSource, "Источник", "Source") \
    X(HudSourceInline, "Свой текст", "Inline text") \
    X(HudSourceNotepad, "Заметка из Блокнота", "Notepad note") \
    X(HudLinkedNoteMissing, "Связанная заметка не найдена.", "Linked note was not found.") \
    X(HudActionTagsDisabled, "Action-теги в HUD не выполняются: команды, скриншоты, фото, управление биндами и диалогами будут пропущены.", "Action tags are disabled in HUD: commands, screenshots, photos, bind control, and dialog actions will be skipped.") \
    X(HudText, "Текст слоя", "Layer text") \
    X(HudPosition, "Позиция", "Position") \
    X(HudAnchor, "Якорь", "Anchor") \
    X(HudAnchorTopLeft, "Слева сверху", "Top left") \
    X(HudAnchorTopCenter, "Сверху по центру", "Top center") \
    X(HudAnchorTopRight, "Справа сверху", "Top right") \
    X(HudAnchorCenterLeft, "Слева по центру", "Center left") \
    X(HudAnchorCenter, "Центр", "Center") \
    X(HudAnchorCenterRight, "Справа по центру", "Center right") \
    X(HudAnchorBottomLeft, "Слева снизу", "Bottom left") \
    X(HudAnchorBottomCenter, "Снизу по центру", "Bottom center") \
    X(HudAnchorBottomRight, "Справа снизу", "Bottom right") \
    X(HudOffsetX, "Смещение X", "Offset X") \
    X(HudOffsetY, "Смещение Y", "Offset Y") \
    X(HudPlaceOnScreen, "Разместить на экране", "Place on screen") \
    X(HudPlacementActive, "Перетащите HUD-блок мышью. Esc отменит режим.", "Drag the HUD block with the mouse. Esc cancels placement.") \
    X(HudSize, "Размер", "Size") \
    X(HudAutoSize, "Авторазмер", "Auto size") \
    X(HudWidth, "Ширина", "Width") \
    X(HudHeight, "Высота", "Height") \
    X(HudScale, "Масштаб", "Scale") \
    X(HudStyle, "Стиль", "Style") \
    X(HudBackground, "Фон", "Background") \
    X(HudBackgroundAlpha, "Прозрачность фона", "Background opacity") \
    X(HudPaddingX, "Отступ X", "Padding X") \
    X(HudPaddingY, "Отступ Y", "Padding Y") \
    X(HudRounding, "Закругление", "Rounding") \
    X(HudBorder, "Рамка", "Border") \
    X(HudShadow, "Тень", "Shadow") \
    X(HudStyleWindow, "Фон и рамка", "Background and border") \
    X(HudStyleTextRows, "Текст", "Text") \
    X(HudStyleBorderShadow, "Тень и контур", "Shadow and outline") \
    X(HudStyleSeparators, "Разделители", "Separators") \
    X(HudTextColor, "Цвет текста", "Text color") \
    X(HudTextAlpha, "Прозрачность текста", "Text opacity") \
    X(HudItemSpacingX, "Интервал X", "Spacing X") \
    X(HudItemSpacingY, "Интервал между строками", "Line spacing") \
    X(HudItemInnerSpacingX, "Внутренний интервал X", "Inner spacing X") \
    X(HudItemInnerSpacingY, "Внутренний интервал Y", "Inner spacing Y") \
    X(HudBorderColor, "Цвет рамки", "Border color") \
    X(HudBorderAlpha, "Прозрачность рамки", "Border opacity") \
    X(HudBorderSize, "Толщина рамки", "Border size") \
    X(HudSeparatorColor, "Цвет разделителя", "Separator color") \
    X(HudSeparatorAlpha, "Прозрачность разделителя", "Separator opacity") \
    X(HudSeparatorSize, "Толщина разделителя", "Separator size") \
    X(HudShadowColor, "Цвет тени", "Shadow color") \
    X(HudShadowAlpha, "Прозрачность тени", "Shadow opacity") \
    X(HudShadowOffsetX, "Смещение тени X", "Shadow offset X") \
    X(HudShadowOffsetY, "Смещение тени Y", "Shadow offset Y") \
    X(HudVisibility, "Видимость", "Visibility") \
    X(HudVisibilityConditions, "Условия", "Conditions") \
    X(HudConfigureConditions, "Настроить условия…", "Configure conditions...") \
    X(HudWidgetVisibilityHint, "HUD-блок показывается, когда выполнены настроенные условия.", "The HUD block is shown when its configured conditions are met.") \
    X(HudLayerVisibilityHint, "Слой наследует видимость HUD-блока и может иметь собственные условия.", "The layer inherits HUD block visibility and may have its own conditions.") \
    X(HudRefreshMs, "Обновление (мс)", "Refresh (ms)") \
    X(HudRefreshZeroWarning, "0 мс обновляет HUD-блок каждый кадр. Это может снизить FPS, особенно при тяжёлых тегах или множестве HUD-блоков.", "0 ms refreshes the HUD block every frame. This can reduce FPS, especially with expensive tags or many HUD blocks.") \
    X(HudPreview, "Предпросмотр", "Preview") \
    X(HudImagesFolder, "Папка картинок", "Images folder") \
    X(HudCanvas, "Холст", "Canvas") \
    X(HudLayers, "Слои", "Layers") \
    X(HudProperties, "Свойства", "Properties") \
    X(HudPropertiesContent, "Содержимое", "Content") \
    X(HudPropertiesGeometry, "Геометрия", "Geometry") \
    X(HudPropertiesAppearance, "Оформление", "Appearance") \
    X(HudPropertiesSearchHint, "Поиск по свойствам", "Search properties") \
    X(HudPropertiesNoResults, "Подходящих свойств нет. Измените поисковый запрос.", "No matching properties. Change the search query.") \
    X(HudSelectedElementsFormat, "Выбрано слоёв: %d", "Selected layers: %d") \
    X(HudInspectorVisibility, "Видимость", "Visibility") \
    X(HudWidgetSection, "HUD-блок", "HUD block") \
    X(HudElementSection, "Слой", "Layer") \
    X(HudAddElement, "Добавить слой", "Add layer") \
    X(HudDuplicateElement, "Дублировать слой", "Duplicate layer") \
    X(HudNoElementSelection, "Выберите слой на холсте или в списке слоёв.", "Select a layer on the canvas or in the layer list.") \
    X(HudNoLayers, "В этом HUD-блоке пока нет слоёв. Нажмите «Добавить слой».", "This HUD block has no layers yet. Choose Add layer.") \
    X(HudElementText, "Текст", "Text") \
    X(HudElementMarkup, "Разметка", "Markup") \
    X(HudElementImage, "Картинка", "Image") \
    X(HudElementShape, "Фигура", "Shape") \
    X(HudElementLine, "Линия", "Line") \
    X(HudElementIcon, "Иконка", "Icon") \
    X(HudElementProgress, "Прогресс", "Progress") \
    X(HudElementGroup, "Группа", "Group") \
    X(HudCanvasWidth, "Ширина холста", "Canvas width") \
    X(HudCanvasHeight, "Высота холста", "Canvas height") \
    X(HudScalePolicy, "Масштабирование", "Scale policy") \
    X(HudScalePolicyFixed, "Фиксированный", "Fixed") \
    X(HudScalePolicyWidth, "По ширине экрана", "Scale with width") \
    X(HudScalePolicyHeight, "По высоте экрана", "Scale with height") \
    X(HudScalePolicyUniform, "Пропорционально", "Uniform scale") \
    X(HudGrid, "Сетка", "Grid") \
    X(HudSnap, "Магнит", "Snap") \
    X(HudGuides, "Направляющие", "Guides") \
    X(HudCanvasInteractionHint, "Перетаскивание — перемещение, 8 ручек — размер, пустая область — рамка выделения, средняя кнопка — панорамирование.", "Drag to move, 8 handles to resize, empty area for marquee, middle mouse button to pan.") \
    X(HudCanvasSnapThresholdHint, "Магнит к сетке срабатывает только рядом с линией, без рывков на каждом пикселе.", "Grid snap activates only near a grid line, without snapping every pixel.") \
    X(HudZoomFit, "По размеру", "Fit") \
    X(HudZoom100, "100%", "100%") \
    X(HudZoomIn, "+", "+") \
    X(HudZoomOut, "-", "-") \
    X(HudCanvasAutoHeight, "Автовысота холста", "Automatic canvas height") \
    X(HudCanvasEditorHeight, "Высота холста", "Canvas height") \
    X(HudCanvasSplitterHint, "Перетащите для ручной высоты холста. Двойной щелчок возвращает автовысоту.", "Drag to set the canvas height manually. Double-click to restore automatic height.") \
    X(HudUndo, "Отменить", "Undo") \
    X(HudRedo, "Повторить", "Redo") \
    X(HudGroup, "Группа", "Group") \
    X(HudUngroup, "Разгрупп.", "Ungroup") \
    X(HudMoveToRoot, "Убрать из группы", "Move to root") \
    X(HudLayerSearchHint, "Поиск слоёв", "Search layers") \
    X(HudAlignMenu, "Выравнивание", "Align and distribute") \
    X(HudArrangeLeft, "По левому краю", "Align left") \
    X(HudArrangeHCenter, "По центру по горизонтали", "Align horizontal centers") \
    X(HudArrangeRight, "По правому краю", "Align right") \
    X(HudArrangeTop, "По верхнему краю", "Align top") \
    X(HudArrangeVCenter, "По центру по вертикали", "Align vertical centers") \
    X(HudArrangeBottom, "По нижнему краю", "Align bottom") \
    X(HudDistributeHorizontal, "Распределить по горизонтали", "Distribute horizontally") \
    X(HudDistributeVertical, "Распределить по вертикали", "Distribute vertically") \
    X(HudLocked, "Блокировка", "Lock") \
    X(HudHidden, "Скрыть", "Hide") \
    X(HudOpacity, "Прозрачность", "Opacity") \
    X(HudX, "X", "X") \
    X(HudY, "Y", "Y") \
    X(HudW, "Ширина", "Width") \
    X(HudH, "Высота", "Height") \
    X(HudFill, "Заливка", "Fill") \
    X(HudFillColor, "Цвет заливки", "Fill color") \
    X(HudFillAlpha, "Прозрачность заливки", "Fill opacity") \
    X(HudStroke, "Обводка", "Stroke") \
    X(HudStrokeColor, "Цвет обводки", "Stroke color") \
    X(HudStrokeAlpha, "Прозрачность обводки", "Stroke opacity") \
    X(HudStrokeSize, "Толщина обводки", "Stroke size") \
    X(HudOutline, "Контур", "Outline") \
    X(HudOutlineColor, "Цвет контура", "Outline color") \
    X(HudOutlineAlpha, "Прозрачность контура", "Outline opacity") \
    X(HudOutlineSize, "Толщина контура", "Outline size") \
    X(HudTint, "Тонировка", "Tint") \
    X(HudTintAlpha, "Прозрачность тонировки", "Tint opacity") \
    X(HudProgressFill, "Цвет прогресса", "Progress color") \
    X(HudProgressFillAlpha, "Прозрачность прогресса", "Progress opacity") \
    X(HudImagePath, "Путь картинки", "Image path") \
    X(HudInsertImage, "Выбрать картинку", "Choose image") \
    X(HudImageCopied, "Картинка скопирована в профиль HUD.", "Image was copied into the HUD profile.") \
    X(HudImageInsertFailed, "Не удалось вставить картинку HUD.", "Failed to insert HUD image.") \
    X(HudImageFitContain, "Contain", "Contain") \
    X(HudImageFitCover, "Cover", "Cover") \
    X(HudImageFitStretch, "Stretch", "Stretch") \
    X(HudImageFit, "Заполнение области", "Image fit") \
    X(HudUnsafeImagePath, "Разрешены только относительные пути внутри папки картинок HUD. URL, абсолютные пути и .. запрещены.", "Only relative paths inside the HUD images folder are allowed. URLs, absolute paths, and .. are blocked.") \
    X(HudIconName, "Имя иконки", "Icon name") \
    X(HudExpression, "Выражение", "Expression") \
    X(HudProgressRange, "Диапазон значений", "Value range") \
    X(HudMin, "Минимум", "Minimum") \
    X(HudMax, "Максимум", "Maximum") \
    X(HudDefaultValue, "Значение по умолчанию", "Default value") \
    X(HudFontSize, "Размер шрифта", "Font size") \
    X(HudAlignLeft, "Слева", "Left") \
    X(HudAlignCenter, "По центру", "Center") \
    X(HudAlignRight, "Справа", "Right") \
    X(HudTextAlign, "Выравнивание текста", "Text alignment") \
    X(HudTextMode, "Режим текста", "Text mode") \
    X(HudTextModePlain, "Простой текст", "Plain text") \
    X(HudTextModeMarkup, "Разметка", "Markup") \
    X(HudTextModeNotepad, "Заметка из Блокнота", "Notepad note") \
    X(HudVariables, "Переменные", "Variables") \
    X(HudVariablesTitle, "Переменные HUD", "HUD variables") \
    X(HudMarkupColor, "Цвет", "Color") \
    X(HudMarkupFont, "Шрифт", "Font") \
    X(HudMarkupAlign, "Выравн.", "Align") \
    X(HudMarkupLine, "Линия", "Line") \
    X(HudMarkupBreak, "Разрыв", "Break") \
    X(HudMarkupIcon, "Иконка", "Icon") \
    X(IconPickerTitle, "Выбор иконки", "Choose icon") \
    X(IconPickerSearchHint, "Поиск иконок", "Search icons") \
    X(IconPickerAllCategories, "Все", "All") \
    X(IconPickerRecent, "Недавние", "Recent") \
    X(IconPickerNoMatches, "Ничего не найдено.", "No icons found.") \
    X(IconPickerStyleSolid, "Solid", "Solid") \
    X(IconPickerStyleBrand, "Brands", "Brands") \
    X(IconPickerCategoryGeneral, "Общие", "General") \
    X(IconPickerCategoryBrands, "Бренды", "Brands") \
    X(IconPickerCategoryTransport, "Транспорт", "Transport") \
    X(IconPickerCategoryGame, "Игра", "Game") \
    X(IconPickerCategoryDocuments, "Документы", "Documents") \
    X(IconPickerCategoryCommunication, "Чат", "Communication") \
    X(IconPickerCategoryStatus, "Статус", "Status") \
    X(IconPickerCategoryMoney, "Деньги", "Money") \
    X(IconPickerCategorySettings, "Настройки", "Settings") \
    X(IconPickerCategoryPeople, "Люди", "People") \
    X(IconPickerSelect, "Выбрать", "Select") \
    X(HudMarkupImage, "Картинка", "Image") \
    X(HudInsertTagTime, "{time}", "{time}") \
    X(HudInsertTagHp, "{health}", "{health}") \
    X(HudInsertTagWeapon, "{myweapon}", "{myweapon}") \
    X(HudExport, "Экспорт", "Export") \
    X(HudImport, "Импорт", "Import") \
    X(HudExportedFormat, "Экспортировано: %s", "Exported: %s") \
    X(HudExportFailed, "Не удалось экспортировать HUD preset.", "Failed to export the HUD preset.") \
    X(HudImportMissingFormat, "Для импорта положите файл сюда: %s", "Place the import file here: %s") \
    X(HudImportInvalid, "Файл импорта не является поддерживаемым HUD v2/v3 preset.", "The import file is not a supported HUD v2/v3 preset.") \
    X(HudImported, "HUD preset импортирован.", "HUD preset imported.") \
    X(HudImportedMissingAssetsFormat, "HUD preset импортирован. Не найдено картинок: %d.", "HUD preset imported. Missing images: %d.") \
    X(HudImportMissingAssetsTitle, "Отсутствующие файлы HUD", "Missing HUD files") \
    X(HudImportMissingAssetsConfirmFormat, "В активном профиле отсутствуют %d файлов изображений. Они перечислены ниже. Импортировать HUD-блок всё равно?", "%d image files are missing from the active profile. They are listed below. Import the HUD block anyway?") \
    X(HudImportContinue, "Продолжить импорт", "Continue import") \
    X(HudPresetFilesFilter, "Пресеты HelperByOrc HUD", "HelperByOrc HUD presets") \
    X(HudFutureSchemaReadOnly, "HUD создан более новой версией HelperByOrc. Редактирование и сохранение отключены, чтобы не повредить данные.", "This HUD was created by a newer HelperByOrc version. Editing and saving are disabled to protect the data.") \
    X(HudMigrationBackupFailed, "Не удалось создать резервную копию HUD. Старый HUD загружен без перезаписи конфига.", "Failed to create the HUD backup. The old HUD was loaded without overwriting the config.") \
    X(HudToolbarEdit, "Действия", "Actions") \
    X(HudToolbarCreate, "Добавить", "Add") \
    X(HudToolbarFile, "Файл", "File") \
    X(HudToolbarView, "Холст", "Canvas") \
    X(HudMoreActions, "Ещё", "More") \
    X(HudCanvasSizeFormat, "Холст: %.0f × %.0f", "Canvas: %.0f × %.0f") \
    X(HudWidgetMetaFormat, "%.0f × %.0f · слоёв: %d", "%.0f × %.0f · layers: %d") \
    X(HudOn, "вкл", "on") \
    X(HudOff, "выкл", "off") \
    X(HudZoomPercentFormat, "%d%%", "%d%%") \
    X(HudCanvasStatusFormat, "Сетка: %s · Направляющие: %s · Шаг: %.0f · Zoom: %s", "Grid: %s · Guides: %s · Step: %.0f · Zoom: %s") \
    X(HudPanelCollapse, "Свернуть рабочую панель", "Collapse workspace panel") \
    X(HudPanelExpand, "Развернуть рабочую панель", "Expand workspace panel") \
    X(HudDeleteWidgetConfirmFormat, "Удалить HUD-блок «%s»? Его слои будут удалены вместе с ним. Действие можно отменить через Undo.", "Delete HUD block “%s”? Its layers will be deleted with it. The action can be reverted with Undo.") \
    X(HudDeleteWidgetTitle, "Удаление HUD-блока", "Delete HUD block") \
    X(HudHelpTitle, "Справка HUD", "HUD Help") \
    X(HudHelpWorkflow, "Рабочий процесс", "Workflow") \
    X(HudHelpCanvas, "Холст", "Canvas") \
    X(HudHelpElements, "Слои и группы", "Layers and groups") \
    X(HudHelpData, "Данные и условия", "Data and visibility") \
    X(HudHelpFiles, "Файлы и миграция", "Files and migration") \
    X(HudHelpIntro, "Краткая практическая справка. Выберите раздел слева — справа останутся только нужные действия и ограничения.", "A concise practical guide. Choose a section on the left to see only the relevant actions and constraints.") \
    X(HudHelpQuickStart, "Быстрый старт", "Quick start") \
    X(HudHelpStepCreateTitle, "1. Создайте HUD-блок", "1. Create a HUD block") \
    X(HudHelpStepCreateBody, "Нажмите «Добавить» и создайте пустой HUD-блок или выберите готовый шаблон.", "Choose Add to create an empty HUD block or start from a ready-made template.") \
    X(HudHelpStepElementsTitle, "2. Добавьте слои", "2. Add layers") \
    X(HudHelpStepElementsBody, "Текст, разметка, картинка, фигура, иконка, индикатор и группа добавляются через «Добавить» → «Добавить слой».", "Add text, markup, image, shape, icon, progress bar, or group through Add → Add layer.") \
    X(HudHelpStepEditTitle, "3. Соберите композицию", "3. Compose the HUD block") \
    X(HudHelpStepEditBody, "Перемещайте и меняйте размер на холсте; точные значения задавайте в разделе «Геометрия».", "Move and resize on the canvas; enter exact values in the Geometry section.") \
    X(HudHelpStepPlaceTitle, "4. Разместите на экране", "4. Place on screen") \
    X(HudHelpStepPlaceBody, "Выберите HUD-блок и откройте «Действия» → «Разместить на экране». Положение сохраняется в активном профиле.", "Select a HUD block and choose Actions → Place on screen. The position is saved in the active profile.") \
    X(HudHelpAutosaveTitle, "Сохранение", "Saving") \
    X(HudHelpAutosaveBody, "Изменения сохраняются после завершения действия. Drag, resize и ввод текста создают по одной Undo-транзакции, а не запись на каждый кадр.", "Changes are saved after an action completes. Drag, resize, and text input create one Undo transaction instead of one entry per frame.") \
    X(HudHelpMouseTitle, "Управление мышью", "Mouse controls") \
    X(HudHelpMouseLeftTitle, "ЛКМ по слою", "Left click a layer") \
    X(HudHelpMouseLeftBody, "Выбрать; удерживать и тянуть — переместить. Ctrl + ЛКМ добавляет или убирает слой из выбора.", "Select; hold and drag to move. Ctrl + left click adds or removes a layer from the selection.") \
    X(HudHelpMouseHandlesTitle, "Ручки рамки", "Selection handles") \
    X(HudHelpMouseHandlesBody, "Восемь ручек меняют размер. Заблокированный слой выбрать можно, перемещать и менять его размер нельзя.", "Eight handles resize the layer. A locked layer can be selected but cannot be moved or resized.") \
    X(HudHelpMouseEmptyTitle, "ЛКМ по пустому месту", "Left click empty space") \
    X(HudHelpMouseEmptyBody, "Снять выбор; потянуть — выделить рамкой. С Ctrl новое выделение добавляется к текущему.", "Clear selection; drag to marquee-select. Hold Ctrl to add the marquee result to the current selection.") \
    X(HudHelpMouseMiddleTitle, "Средняя кнопка", "Middle mouse button") \
    X(HudHelpMouseMiddleBody, "Перемещает вид холста без изменения координат слоёв.", "Pans the canvas view without changing layer coordinates.") \
    X(HudHelpSnapTitle, "Сетка и направляющие", "Grid and guides") \
    X(HudHelpSnapBody, "Умная направляющая привязывает края и центры. Если подходящей направляющей нет, применяется шаг сетки. Оба режима меняются в меню «Холст» над рабочей областью.", "Smart guides snap edges and centers. Grid snapping is used only when no suitable guide exists. Both modes are controlled from Canvas above the workspace.") \
    X(HudHelpCanvasHeightTitle, "Высота редактора", "Editor height") \
    X(HudHelpCanvasHeightBody, "В компактном режиме потяните разделитель под холстом для ручной высоты. Двойной щелчок возвращает автоматический режим.", "In compact mode, drag the splitter below the canvas for a manual height. Double-click restores automatic sizing.") \
    X(HudHelpLayerOrderTitle, "Порядок слоёв", "Layer order") \
    X(HudHelpLayerOrderBody, "Перетаскивание между строками меняет z-порядок. Точный индикатор показывает место вставки.", "Dropping between rows changes z-order. The exact indicator shows the insertion point.") \
    X(HudHelpGroupsTitle, "Группы", "Groups") \
    X(HudHelpGroupsBody, "Drop на группу добавляет слой. Во время перетаскивания над списком появляется цель «Убрать из группы». Поддерживается один уровень; дублирование группы копирует прямых детей.", "Drop onto a group to add a layer. While dragging, a Move to root target appears above the list. One level is supported, and duplicating a group copies its direct children.") \
    X(HudHelpMultiSelectTitle, "Несколько слоёв", "Multiple layers") \
    X(HudHelpMultiSelectBody, "Общие свойства показывают разные значения. Выравнивание работает для двух и более, распределение — для трёх и более незаблокированных слоёв.", "Common properties show mixed values. Alignment needs at least two unlocked layers; distribution needs at least three.") \
    X(HudHelpLockHideTitle, "Блокировка и скрытие", "Lock and hide") \
    X(HudHelpLockHideBody, "Блокировка защищает геометрию от случайных изменений. Скрытие убирает слой с холста и игрового HUD, но оставляет его в списке.", "Lock protects geometry from accidental edits. Hide removes the layer from the canvas and runtime HUD while keeping it in the layer list.") \
    X(HudHelpVariablesBody, "Кнопка «Переменные» вставляет выбранный тег в активное поле. В HUD используется pure-режим: action-теги не выполняют команды, скриншоты или бинды.", "Variables inserts the selected tag into the active field. HUD uses pure mode, so action tags cannot execute commands, screenshots, or binds.") \
    X(HudHelpExpressionsTitle, "Выражения", "Expressions") \
    X(HudHelpExpressionsBody, "Индикатор получает число из expression и переводит его в диапазон Минимум—Максимум. При ошибке используется значение по умолчанию.", "A progress bar reads a number from its expression and maps it to Minimum—Maximum. The default value is used when evaluation fails.") \
    X(HudHelpRefreshTitle, "Частота обновления", "Refresh interval") \
    X(HudHelpRefreshBody, "0 мс обновляет динамические данные каждый кадр. Для обычного текста рекомендуется 100–250 мс; статические данные повторно не вычисляются.", "0 ms refreshes dynamic data every frame. Use 100–250 ms for ordinary text; static data is not evaluated repeatedly.") \
    X(HudHelpConditionsTitle, "Условия видимости", "Visibility conditions") \
    X(HudHelpConditionsBody, "Условия вычисляются один раз за кадр и могут объединяться через «любое» или «все». Скрытый или заблокированный условиями слой не обновляется до появления.", "Conditions are evaluated once per frame and can use any/all combination. A hidden or condition-blocked layer is not refreshed until visible.") \
    X(HudHelpImportTitle, "Импорт и экспорт", "Import and export") \
    X(HudHelpImportBody, "Импорт принимает шаблоны v2/v3 .helperhud.json. Экспорт создаёт v3 и не изменяет другие HUD-блоки профиля.", "Import accepts v2/v3 .helperhud.json templates. Export creates v3 without changing other profile HUD blocks.") \
    X(HudHelpAssetsTitle, "Картинки", "Images") \
    X(HudHelpAssetsBody, "JSON не содержит бинарные картинки. Файлы HUD хранятся в hud\\images; абсолютные пути, URL и .. запрещены.", "JSON does not embed image binaries. HUD files live in hud\\images; absolute paths, URLs, and .. are blocked.") \
    X(HudHelpMigrationTitle, "Миграция", "Migration") \
    X(HudHelpMigrationBody, "Перед первым переходом v1/v2 → v3 создаётся hud\\backup\\hud-before-v3.json. При ошибке backup конфиг не перезаписывается.", "Before the first v1/v2 to v3 migration, hud\\backup\\hud-before-v3.json is created. The config is not overwritten if backup fails.") \
    X(HudHelpFutureSchemaTitle, "Новая неизвестная схема", "Unknown future schema") \
    X(HudHelpFutureSchemaBody, "Schema выше v3 открывается только для совместимого runtime-чтения: автоматическая запись и импорт блокируются, чтобы не повредить данные.", "Schemas newer than v3 are opened only for compatible runtime reading; automatic writes and imports are blocked to protect the data.") \
    X(HudPresetPlayerStatus, "Статус игрока", "Player status") \
    X(HudPresetVehicle, "Транспорт", "Vehicle") \
    X(HudPresetNoteCard, "Заметка", "Note card") \
    X(HudPresetNoteTitle, "Заметка", "Note") \
    X(HudPresetNoteBody, "Текст HUD-карточки", "HUD card text") \
    X(HudPresetTimer, "Таймер", "Timer") \
    X(HudPresetDashboard, "Мини-панель", "Mini dashboard") \
    X(MiscIntro, "Служебные тесты, отладочные кнопки и логи убраны. Оставлена только пустая оболочка раздела.", "Service tests, debug buttons, and logs were removed. Only the empty shell of this section remains.") \
    X(MiscShellTitle, "Техническая вкладка", "Technical Tab") \
    X(MiscShellDesc, "Раздел готов для будущих утилит, но сейчас не содержит ни тестовых действий, ни диагностических панелей.", "This section is ready for future utilities, but currently contains no test actions or diagnostic panels.") \
    X(MiscHomeIntro, "Во вкладке собраны отдельные служебные разделы. Нажмите на карточку нужного модуля, чтобы открыть его экран.", "This tab groups standalone utility sections. Click a module card to open its screen.") \
    X(MiscVariablesEntryDesc, "Открывает рабочий каталог переменных с поиском, категориями, описаниями и пользовательскими переменными.", "Opens the variables picker with search, categories, descriptions, and custom variables.") \
    X(MiscOpenSectionAction, "Открыть", "Open") \
    X(UnwantedTitle, "Игнорирование сообщений", "Message Filter") \
    X(UnwantedEntryDesc, "Скрывает нежелательные сообщения в чате и не даёт им запускать текстовые бинды.", "Hides unwanted chat messages and prevents them from triggering text binds.") \
    X(UnwantedHome, "Главная", "Home") \
    X(UnwantedModuleToggleTitle, "Фильтр сообщений", "Message filter") \
    X(UnwantedModuleToggleDesc, "Включает или выключает все правила. Список правил при этом сохраняется.", "Turns all rules on or off. Your rule list remains saved.") \
    X(UnwantedModuleOn, "Фильтр включён", "Filter enabled") \
    X(UnwantedModuleOff, "Фильтр выключен", "Filter disabled") \
    X(UnwantedCreateCardDesc, "Вставьте пример сообщения — Helper подготовит подходящее правило.", "Paste a sample message and Helper will prepare a suitable rule.") \
    X(UnwantedCreateOpen, "Создать правило", "Create rule") \
    X(UnwantedRulesOpen, "Показать правила", "Show rules") \
    X(UnwantedRulesCardStats, "Правил: %s | включено: %s | ошибок: %s | дублей: %s", "Rules: %s | enabled: %s | errors: %s | duplicates: %s") \
    X(UnwantedSessionTitle, "За этот запуск", "This launch") \
    X(UnwantedBlockedCount, "Скрыто сообщений: %s", "Messages hidden: %s") \
    X(UnwantedReload, "Загрузить настройки заново", "Reload settings") \
    X(UnwantedLastBlocked, "Последнее скрытое сообщение: %s", "Last hidden message: %s") \
    X(UnwantedLastBlockedEmpty, "Пока нет заблокированных сообщений.", "No blocked messages yet.") \
    X(UnwantedNewRule, "Новое правило", "New rule") \
    X(UnwantedTools, "Настройки", "Settings") \
    X(UnwantedCompatibility, "Совместимость", "Compatibility") \
    X(UnwantedChatAsiCompatibility, "Совместимость с _chat.asi", "_chat.asi compatibility") \
    X(UnwantedChatAsiCompatibilityHelp, "Скрывает сообщения, которые _chat.asi добавляет в обход обычного чата. Адреса находятся автоматически. Если _chat.asi не установлен или изменился и не распознан, Helper продолжит работать через стандартный чат.", "Hides messages that _chat.asi adds outside the standard chat path. Addresses are detected automatically. If _chat.asi is missing or changed and cannot be recognized, Helper continues through the standard chat path.") \
    X(UnwantedNormalizer, "Подготовка текста", "Text preparation") \
    X(UnwantedNormalizerDesc, "Эти настройки одинаково применяются к сообщениям в игре, помощнику и проверке правила.", "These settings apply equally to in-game messages, the helper, and rule testing.") \
    X(UnwantedStripColors, "Не учитывать цветовые коды", "Ignore color codes") \
    X(UnwantedStripColorsHelp, "Выключено: подходят правила как с цветовыми кодами, так и без них. Включено: проверяется только видимый текст без кодов {FFFFFF} и {FF0000FF}.", "Off: rules with or without color codes can match. On: only visible text without codes such as {FFFFFF} and {FF0000FF} is checked.") \
    X(UnwantedCollapseWhitespace, "Сжать пробелы", "Collapse whitespace") \
    X(UnwantedCollapseWhitespaceHelp, "Заменяет несколько пробелов, табуляций или переносов строки одним пробелом.", "Replaces repeated spaces, tabs, or line breaks with one space.") \
    X(UnwantedTrim, "Убирать пробелы по краям", "Trim outer spaces") \
    X(UnwantedTrimHelp, "Убирает пробелы только в начале и конце сообщения.", "Removes spaces only at the beginning and end of a message.") \
    X(UnwantedMaxPatternLength, "Максимальная длина правила", "Maximum rule length") \
    X(UnwantedRules, "Правила", "Rules") \
    X(UnwantedSelectAll, "Выделить все", "Select all") \
    X(UnwantedClearSelection, "Снять выделение", "Clear selection") \
    X(UnwantedDeleteSelected, "Удалить выбранные", "Delete selected") \
    X(UnwantedEnableSelected, "Включить выбранные", "Enable selected") \
    X(UnwantedDisableSelected, "Выключить выбранные", "Disable selected") \
    X(UnwantedSortByType, "По типу", "By type") \
    X(UnwantedSortStored, "Исходный порядок", "Stored order") \
    X(UnwantedSortName, "По имени", "By name") \
    X(UnwantedSortStatus, "По статусу", "By status") \
    X(UnwantedSortFormat, "Сортировка: %s", "Sort: %s") \
    X(UnwantedSearchHint, "Поиск правил", "Search rules") \
    X(UnwantedFilterAll, "Все", "All") \
    X(UnwantedFilterEnabled, "Включены", "Enabled") \
    X(UnwantedFilterDisabled, "Выключены", "Disabled") \
    X(UnwantedFilterRegex, "Шаблоны", "Patterns") \
    X(UnwantedFilterLiteral, "Обычный текст", "Plain text") \
    X(UnwantedFilterErrors, "Ошибки", "Errors") \
    X(UnwantedFilterByTypeHelp, "Показать только правила типа «%s»", "Show only %s rules") \
    X(UnwantedVisibleRulesFormat, "Показано: %s из %s", "Shown: %s of %s") \
    X(UnwantedBulkActionsFormat, "Выбрано: %s", "Selected: %s") \
    X(UnwantedColumnSelect, "Выбор", "Select") \
    X(UnwantedColumnEnabled, "Вкл.", "On") \
    X(UnwantedColumnRule, "Правило", "Rule") \
    X(UnwantedColumnActions, "Действия", "Actions") \
    X(UnwantedSelectRuleHelp, "Добавить правило в групповое выделение", "Add this rule to the bulk selection") \
    X(UnwantedResetFilters, "Сбросить поиск и фильтры", "Clear search and filters") \
    X(UnwantedNoRules, "Правил пока нет.", "No rules yet.") \
    X(UnwantedNoVisibleRules, "По выбранным условиям правил не найдено.", "No rules match the selected filters.") \
    X(UnwantedTypeLiteral, "Обычный текст", "Plain text") \
    X(UnwantedTypeLiteralHelp, "Скрывает сообщение, если в нём найден указанный текст. Подходит для большинства простых правил.", "Hides a message when it contains the specified text. Best for most simple rules.") \
    X(UnwantedTypeRegex, "Шаблон", "Pattern") \
    X(UnwantedTypeRegexHelp, "Позволяет заменять изменяющиеся части сообщения: числа, ники, время и другие фрагменты. Помощник подготовит шаблон автоматически.", "Lets variable parts of a message change, such as numbers, names, and time. The helper can build the pattern automatically.") \
    X(UnwantedNoCase, "Не учитывать регистр", "Ignore letter case") \
    X(UnwantedNoCaseHelp, "Большие и маленькие буквы считаются одинаковыми. Кириллица поддерживается.", "Uppercase and lowercase letters are treated as equal. Cyrillic is supported.") \
    X(UnwantedWholeWord, "Только целое слово", "Whole word only") \
    X(UnwantedNoFlags, "Обычный режим", "Default mode") \
    X(UnwantedInvalidRule, "Ошибка", "Error") \
    X(UnwantedWarning, "Проверьте", "Check") \
    X(UnwantedRuleOk, "Готово", "Ready") \
    X(UnwantedNoSelection, "Выберите правило слева или создайте новое.", "Select a rule on the left or create a new one.") \
    X(UnwantedCreateRuleTitle, "Создание правила", "Create rule") \
    X(UnwantedEditRuleTitle, "Редактирование правила", "Edit rule") \
    X(UnwantedRuleEnabled, "Правило включено", "Rule enabled") \
    X(UnwantedRuleName, "Имя правила", "Rule name") \
    X(UnwantedRuleNameHint, "Необязательно. Если оставить пустым, в списке будет показан текст правила", "Optional. If left empty, the rule text is shown in the list") \
    X(UnwantedRuleType, "Тип правила", "Rule type") \
    X(UnwantedRuleText, "Текст правила", "Rule text") \
    X(UnwantedTester, "Проверка правила", "Test rule") \
    X(UnwantedTesterHint, "Сообщение для проверки", "Message to test") \
    X(UnwantedTestAction, "Проверить", "Test") \
    X(UnwantedTesterMatched, "Это сообщение будет скрыто.", "This message will be hidden.") \
    X(UnwantedTesterNoMatch, "Это сообщение не будет скрыто.", "This message will not be hidden.") \
    X(UnwantedTesterNormalizedFormat, "После подготовки текста (%s симв.): «%s»", "After text preparation (%s chars): “%s”") \
    X(UnwantedTesterEmptyHint, "Пустое поле проверяет пустое сообщение. Для сообщения только из пробелов введите пробелы и нажмите «Проверить».", "An empty field tests an empty message. For a spaces-only message, enter spaces and click Test.") \
    X(UnwantedRegexHelper, "Помощник по шаблонам", "Pattern helper") \
    X(UnwantedHelperFlowHint, "Вставьте сообщение или полную строку чатлога и отметьте, какие части могут меняться.", "Paste a message or a full chatlog line and choose which parts may change.") \
    X(UnwantedHelperInputHint, "Вставьте пример сообщения", "Paste a sample message") \
    X(TextPatternChatlogTimestampRemoved, "Время чатлога распознано и удалено перед проверкой.", "The chatlog timestamp was detected and removed before testing.") \
    X(UnwantedGeneralizations, "Какие части могут меняться", "Parts that may change") \
    X(UnwantedHelperColors, "Цветовые коды", "Color codes") \
    X(UnwantedHelperNumbers, "Числа", "Numbers") \
    X(UnwantedHelperMoney, "Суммы денег", "Money amounts") \
    X(UnwantedHelperTime, "Время", "Time") \
    X(UnwantedHelperNick, "Ники игроков", "Player names") \
    X(UnwantedHelperPlayerId, "[id]", "[id]") \
    X(UnwantedHelperDomain, "Сайты и ссылки", "Sites and links") \
    X(UnwantedHelperBracketTag, "Префикс [Текст]", "[Text] prefix") \
    X(UnwantedHelperExact, "Точное сообщение", "Exact message") \
    X(UnwantedHelperGeneralized, "Рекомендуемый вариант", "Recommended") \
    X(UnwantedHelperContains, "Фрагмент сообщения", "Message fragment") \
    X(UnwantedRegexVariants, "Варианты шаблона", "Pattern variants") \
    X(UnwantedRegexVariantsHint, "«Копировать» отправит шаблон в буфер обмена, «Использовать» перенесёт его в правило.", "Copy sends the pattern to the clipboard; Use moves it into the rule.") \
    X(UnwantedNormalizedPreview, "Текст, который будет проверяться", "Text that will be checked") \
    X(UnwantedDetectedTokens, "Что найдено в сообщении", "Detected message parts") \
    X(UnwantedDetectedTokensFormat, "Найденные части: %s", "Detected parts: %s") \
    X(UnwantedTokenColor, "Цвет", "Color") \
    X(UnwantedTokenColorHelp, "Цветовые коды вида {FFFFFF} или {FF0000FF} будут подходить независимо от цвета.", "Color codes such as {FFFFFF} or {FF0000FF} may use any color value.") \
    X(UnwantedTokenPlayerId, "ID игрока", "Player ID") \
    X(UnwantedTokenPlayerIdHelp, "ID игрока в квадратных скобках, например [123].", "A player ID in square brackets, for example [123].") \
    X(UnwantedTokenBracketPrefix, "Префикс [...]", "Bracket prefix") \
    X(UnwantedTokenBracketPrefixHelp, "Начальная пометка сообщения, например [Подсказка] или [Информация].", "A message label such as [Hint] or [Information].") \
    X(UnwantedTokenNickname, "Ник", "Nickname") \
    X(UnwantedTokenNicknameHelp, "Ник вида Name_Surname. Конкретный ник сможет меняться.", "A name such as Name_Surname. The actual player name may change.") \
    X(UnwantedTokenInteger, "Целое число", "Integer") \
    X(UnwantedTokenIntegerHelp, "Любое целое число, включая отрицательное.", "Any whole number, including a negative one.") \
    X(UnwantedTokenDecimal, "Дробное число", "Decimal") \
    X(UnwantedTokenDecimalHelp, "Любое дробное число с точкой или запятой.", "Any decimal number using a dot or comma.") \
    X(UnwantedTokenPercentage, "Процент", "Percentage") \
    X(UnwantedTokenPercentageHelp, "Любое число со знаком процента, например 25%.", "Any percentage, for example 25%.") \
    X(UnwantedTokenCompactAmount, "Число с k", "Number with k") \
    X(UnwantedTokenCompactAmountHelp, "Числа вида 1.5k или 2k26.", "Numbers such as 1.5k or 2k26.") \
    X(UnwantedTokenMoney, "Деньги", "Money") \
    X(UnwantedTokenMoneyHelp, "Сумма со знаком $, включая варианты $10, $1.5k и $2k26.", "An amount with $, including $10, $1.5k, and $2k26.") \
    X(UnwantedTokenClock, "Время", "Clock") \
    X(UnwantedTokenClockHelp, "Время суток от 00:00 до 23:59.", "A time of day from 00:00 through 23:59.") \
    X(UnwantedTokenDuration, "Длительность", "Duration") \
    X(UnwantedTokenDurationHelp, "Продолжительность вида 5:30 или 125:59.", "A duration such as 5:30 or 125:59.") \
    X(UnwantedTokenDomain, "Сайт или ссылка", "Site or link") \
    X(UnwantedTokenDomainHelp, "Адрес сайта или ссылка. Конкретный адрес, порт и путь смогут меняться.", "A website address or link. Its address, port, and path may change.") \
    X(UnwantedUseInDraft, "Использовать", "Use") \
    X(UnwantedCopy, "Копировать", "Copy") \
    X(UnwantedDeleteSelectedQuestion, "Удалить выбранные правила: %s?", "Delete selected rules: %s?") \
    X(UnwantedErrorTooLong, "Правило слишком длинное. Максимум: %s.", "The rule is too long. Maximum: %s.") \
    X(UnwantedErrorEmpty, "Введите текст правила.", "Enter the rule text.") \
    X(UnwantedErrorUnknownType, "Тип правила «%s» не поддерживается. Выберите тип заново.", "Rule type “%s” is not supported. Select the type again.") \
    X(UnwantedRegexSafetyUnanchored, "Шаблон может совпасть только с частью сообщения. Для проверки всей строки используйте \\A в начале и \\z в конце.", "The pattern may match only part of a message. Use \\A at the start and \\z at the end to check the whole message.") \
    X(UnwantedRegexMatchesEmpty, "Этот шаблон подходит даже к пустому сообщению и может скрывать лишнее.", "This pattern also matches an empty message and may hide too much.") \
    X(UnwantedRegexBroadWildcard, "Сочетание .*/.+ может захватить слишком много текста. Лучше указать изменяемую часть точнее.", "The .*/.+ combination may capture too much text. Consider describing the changing part more precisely.") \
    X(UnwantedPcreErrorFormat, "Не удалось прочитать шаблон. Проверьте скобки и специальные знаки рядом с указанным местом.\n%s", "The pattern could not be read. Check brackets and special characters near the marked position.\n%s") \
    X(UnwantedPcrePositionDetail, "Символ %s\n%s\n%s^", "Character %s\n%s\n%s^") \
    X(UnwantedRuntimeWarningFormat, "Проверка правила остановлена: %s", "Rule checking stopped: %s") \
    X(UnwantedRuntimeMatchLimit, "шаблон потребовал слишком много проверок", "the pattern required too many checks") \
    X(UnwantedRuntimeDepthLimit, "шаблон слишком сложный", "the pattern is too complex") \
    X(UnwantedRuntimeHeapLimit, "для проверки не хватило памяти", "there was not enough memory to check it") \
    X(UnwantedRuntimeInvalidText, "сообщение содержит повреждённые символы", "the message contains invalid characters") \
    X(UnwantedRuntimeGenericError, "не удалось проверить сообщение", "the message could not be checked") \
    X(UnwantedHelperInvalidUtf8, "Пример содержит повреждённые символы. Вставьте сообщение заново.", "The sample contains invalid characters. Paste the message again.") \
    X(UnwantedHelperExactFallback, "Не удалось безопасно заменить изменяемые части. Показано точное правило.", "The changing parts could not be replaced safely. An exact rule is shown instead.") \
    X(UnwantedRegexReference, "Справочник шаблонов", "Pattern reference") \
    X(UnwantedRegexReferenceHint, "Здесь собраны готовые элементы для ручного редактирования правила. Нажмите элемент, чтобы скопировать, или «Вставить», чтобы добавить его в правило.", "Ready-to-use elements for manual rule editing. Click an item to copy it, or use Append to add it to the rule.") \
    X(UnwantedRegexReferenceSearch, "Поиск по выражениям и описаниям", "Search expressions and descriptions") \
    X(UnwantedRegexReferenceCategory, "Раздел", "Category") \
    X(UnwantedRegexReferenceExpression, "Элемент", "Item") \
    X(UnwantedRegexReferenceDescription, "Описание", "Description") \
    X(UnwantedRegexReferenceAppend, "Вставить", "Append") \
    X(UnwantedRegexReferenceNoResults, "Ничего не найдено.", "No matches found.") \
    X(UnwantedRegexRefCategoryBasic, "Основы", "Basics") \
    X(UnwantedRegexRefCategoryClasses, "Наборы символов", "Character sets") \
    X(UnwantedRegexRefCategoryQuantifiers, "Повторы", "Repeats") \
    X(UnwantedRegexRefCategoryGroups, "Условия и группы", "Conditions and groups") \
    X(UnwantedRegexRefCategoryReady, "Готовые примеры", "Ready examples") \
    X(UnwantedRegexRefEmptyMessage, "Полностью пустое сообщение.", "A completely empty message.") \
    X(UnwantedRegexRefSpacesOnly, "Сообщение только из одного или нескольких обычных пробелов.", "A message containing one or more regular spaces only.") \
    X(UnwantedRegexRefAbsoluteStart, "Начало сообщения. Обычно ставится первым символом правила.", "Start of the message. Usually placed at the beginning of a rule.") \
    X(UnwantedRegexRefAbsoluteEnd, "Конец сообщения. Обычно ставится последним символом правила.", "End of the message. Usually placed at the end of a rule.") \
    X(UnwantedRegexRefQuotedLiteral, "Текст между \\Q и \\E ищется буквально: специальные знаки внутри него не работают как команды.", "Text between \\Q and \\E is matched literally, so special characters inside are treated as normal text.") \
    X(UnwantedRegexRefEscapedMeta, "Обратный слеш перед специальным знаком превращает его в обычный. Пример ищет точку.", "A backslash before a special character makes it literal. This example matches a dot.") \
    X(UnwantedRegexRefAnyChar, "Один любой символ. Используйте осторожно, особенно рядом с * или +.", "Any single character. Use it carefully, especially next to * or +.") \
    X(UnwantedRegexRefClass, "Один символ из перечисленного набора.", "One character from the listed set.") \
    X(UnwantedRegexRefNegatedClass, "Один символ, которого нет в перечисленном наборе.", "One character not present in the listed set.") \
    X(UnwantedRegexRefRange, "Один символ из указанного диапазона.", "One character from the specified range.") \
    X(UnwantedRegexRefUnicodeDigit, "Одна цифра. Для цифр только от 0 до 9 можно использовать [0-9].", "One digit. Use [0-9] when only digits from 0 through 9 are allowed.") \
    X(UnwantedRegexRefUnicodeLetter, "Одна буква любого языка, включая кириллицу.", "One letter from any language, including Cyrillic.") \
    X(UnwantedRegexRefWhitespace, "Один пробельный символ: пробел, табуляция или перенос строки.", "One whitespace character: a space, tab, or line break.") \
    X(UnwantedRegexRefHorizontalWhitespace, "Один пробел или табуляция, но не перенос строки.", "One space or tab, but not a line break.") \
    X(UnwantedRegexRefWordChar, "Одна буква, цифра или подчёркивание.", "One letter, digit, or underscore.") \
    X(UnwantedRegexRefWordBoundary, "Начало или конец отдельного слова.", "The beginning or end of a whole word.") \
    X(UnwantedRegexRefOptional, "Предыдущий элемент встречается ноль или один раз.", "The previous item occurs zero or one time.") \
    X(UnwantedRegexRefZeroOrMore, "Предыдущий элемент встречается ноль или больше раз; с точкой может быть слишком широким.", "The previous item occurs zero or more times; it may be too broad after a dot.") \
    X(UnwantedRegexRefOneOrMore, "Предыдущий элемент встречается один или больше раз.", "The previous item occurs one or more times.") \
    X(UnwantedRegexRefExactCount, "Предыдущий элемент должен повториться ровно указанное число раз.", "The previous item must repeat exactly the specified number of times.") \
    X(UnwantedRegexRefRangeCount, "Предыдущий элемент повторяется от минимального до максимального числа раз.", "The previous item repeats between the minimum and maximum counts.") \
    X(UnwantedRegexRefLazy, "Берёт как можно меньше повторений, но сохраняет совпадение всего правила.", "Uses as few repetitions as possible while keeping the whole rule matched.") \
    X(UnwantedRegexRefNonCapturingGroup, "Объединяет несколько элементов в одну часть правила.", "Combines several elements into one part of the rule.") \
    X(UnwantedRegexRefAlternation, "Соответствует одному из вариантов слева или справа от |.", "Matches one of the alternatives on either side of |.") \
    X(UnwantedRegexRefPositiveLookahead, "Совпадение возможно, только если сразу после него идёт указанный текст.", "Matches only when the specified text follows immediately.") \
    X(UnwantedRegexRefNegativeLookahead, "Совпадение возможно, только если указанного текста дальше нет.", "Matches only when the specified following text is absent.") \
    X(UnwantedRegexRefPositiveLookbehind, "Совпадение возможно, только если прямо перед ним есть указанный текст.", "Matches only when the specified text appears immediately before it.") \
    X(UnwantedRegexRefNegativeLookbehind, "Совпадение возможно, только если указанного текста прямо перед ним нет.", "Matches only when the specified text is absent immediately before it.") \
    X(UnwantedRegexRefAtomicGroup, "Фиксирует найденный внутри группы вариант. Может ускорить сложное правило.", "Locks in the match found inside the group and may speed up a complex rule.") \
    X(UnwantedDuplicate, "Дубликат", "Duplicate") \
    X(UnwantedUnsavedTitle, "Несохранённые изменения", "Unsaved Changes") \
    X(UnwantedUnsavedDesc, "Черновик изменён. Закрыть его и потерять изменения?", "The draft was changed. Close it and discard the changes?") \
    X(UnwantedDiscard, "Не сохранять", "Discard") \
    X(TagsKindSimple, "Простая", "Simple") \
    X(TagsKindFunction, "Функциональная", "Function") \
    X(TagsBuiltinIdDescription, "Возвращает ваш локальный ID игрока через SampApi::Local_ID().", "Returns your local player ID via SampApi::Local_ID().") \
    X(TagsBuiltinNickDescription, "Возвращает ваш текущий ник через GetNameID(Local_ID()).", "Returns your current nickname via GetNameID(Local_ID()).") \
    X(TagsBuiltinThisbindDescription, "Возвращает человекочитаемую ссылку на текущий запущенный бинд для других bind...-переменных.\n\nФормат результата:\n- уровень «Без папки»: \"Имя бинда\" \"\";\n- папка или подпапка: \"Имя бинда\" \"Папка/Подпапка\".\n\nПримеры: [bindstop({thisbind})], [bindpause({thisbind})], [bindended({thisbind})]. Категория намеренно не добавляется для совместимости; для долговечной связи между разными биндами используйте @bind-N. Вне выполняющегося бинда возвращается пустая строка.", "Returns a human-readable reference to the currently running bind for other bind variables.\n\nResult format:\n- No folder: \"Bind name\" \"\";\n- folder or subfolder: \"Bind name\" \"Folder/Subfolder\".\n\nExamples: [bindstop({thisbind})], [bindpause({thisbind})], [bindended({thisbind})]. Category is intentionally omitted for compatibility; use @bind-N for durable links between different binds. Returns an empty string outside a running bind.") \
    X(TagsBuiltinThisbindSelectorDescription, "То же значение, что {thisbind}: \"Имя бинда\" \"Папка/Подпапка\" или \"Имя бинда\" \"\" для уровня «Без папки». Название подчёркивает, что результат предназначен для аргументов [bindstart(...)], [bindstop(...)], [bindenable(...)] и других bind...-переменных.", "Same value as {thisbind}: \"Bind name\" \"Folder/Subfolder\" or \"Bind name\" \"\" for No folder. The name emphasizes that the result is intended for [bindstart(...)], [bindstop(...)], [bindenable(...)], and other bind-variable arguments.") \
    X(TagsBuiltinThisbindNameDescription, "Возвращает только имя текущего запущенного бинда — без кавычек, папки и категории. Подходит для сообщений, уведомлений и логов.\n\nНе используйте как единственную ссылку в bind...-действии, если имена могут повторяться; для текущего бинда используйте {thisbind}, для связи с другим биндом — @bind-N.", "Returns only the current running bind name, without quotes, folder, or category. Suitable for messages, notifications, and logs.\n\nDo not use it as the only bind-action reference when names may repeat; use {thisbind} for the current bind and @bind-N for another bind.") \
    X(TagsBuiltinThisbindFolderDescription, "Возвращает полный путь папки текущего запущенного бинда.\n\nРезультат:\n- уровень «Без папки» — пустая строка;\n- папка — Папка;\n- вложенная папка — Папка/Подпапка.\n\nМожно передать в [bindrandom(\"{thisbindfolder}\")]. Для ссылки на сам текущий бинд используйте {thisbind}.", "Returns the full folder path of the currently running bind.\n\nResult:\n- No folder — empty string;\n- folder — Folder;\n- nested folder — Folder/Subfolder.\n\nIt can be passed to [bindrandom(\"{thisbindfolder}\")]. Use {thisbind} to reference the current bind itself.") \
    X(TagsBuiltinThiscategoryDescription, "Возвращает категорию, из которой запущен текущий бинд. Это runtime-категория запуска, а не выбранная вкладка интерфейса.\n\nИспользуйте как 3-й аргумент после имени бинда и пути папки: [bindstart(\"Имя бинда\" \"Папка/Подпапка\" \"{thiscategory}\")]. Для уровня «Без папки»: [bindstart(\"Имя бинда\" \"\" \"{thiscategory}\")].\n\nВне запущенного бинда возвращает пустую строку.", "Returns the category from which the current bind was started. This is the runtime launch category, not the selected UI tab.\n\nUse it as the third argument after bind name and folder path: [bindstart(\"Bind name\" \"Folder/Subfolder\" \"{thiscategory}\")]. For No folder: [bindstart(\"Bind name\" \"\" \"{thiscategory}\")].\n\nReturns an empty string outside a running bind.") \
    X(TagsBuiltinBindStopAllDescription, "Останавливает все запущенные бинды после текущей строки и ничего не вставляет в текст.\n\nСинтаксис: {bindstopall}. Аргументы «Категория», «Папка/Подпапка» и «Имя бинда» здесь не используются: действие всегда относится ко всем активным биндам. Работает только внутри выполняющегося бинда; если активных биндов нет, это не ошибка.", "Stops all running binds after the current line and inserts no text.\n\nSyntax: {bindstopall}. Category, Folder/Subfolder, and Bind name arguments are not used: the action always targets all active binds. Works only inside a running bind; having no active binds is not an error.") \
    X(TagsBuiltinTargetIdDescription, "Возвращает ID игрока, в которого вы целились последним.", "Returns the ID of the player you aimed at most recently.") \
    X(TagsBuiltinTargetNickDescription, "Возвращает ник игрока, в которого вы целились последним.", "Returns the nickname of the player you aimed at most recently.") \
    X(TagsBuiltinTargetRpNickDescription, "Возвращает RP-ник последней цели: Walcher_Flett станет Walcher Flett.", "Returns the RP nickname of the last target: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinTargetNameDescription, "Возвращает имя из ника последней цели до символа подчёркивания.", "Returns the first name from the last target nickname before the underscore.") \
    X(TagsBuiltinTargetSurnameDescription, "Возвращает фамилию из ника последней цели после символа подчёркивания.", "Returns the surname from the last target nickname after the underscore.") \
    X(TagsBuiltinTargetHealthDescription, "Возвращает здоровье игрока, в которого вы целились последним. Если цель недоступна или значение не удалось получить, возвращает пустую строку.", "Returns the health of the player you aimed at most recently. If the target is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinTargetArmourDescription, "Возвращает броню игрока, в которого вы целились последним. Если цель недоступна или значение не удалось получить, возвращает пустую строку.", "Returns the armour of the player you aimed at most recently. If the target is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinClosestIdDescription, "Возвращает ID ближайшего по 3D-дистанции другого застримленного игрока. Общий снимок closest обновляется не чаще одного раза в 250 мс.", "Returns the ID of the other streamed player with the shortest 3D distance. The shared closest snapshot is updated at most once every 250 ms.") \
    X(TagsBuiltinClosestIdToCenterDescription, "Возвращает ID игрока, чья спроецированная точка корпуса находится ближе всего к геометрическому центру текущего viewport. Учитываются только точки внутри viewport; радиуса и проверки препятствий нет.", "Returns the player ID whose projected torso point is closest to the geometric center of the current viewport. Only points inside the viewport are considered; there is no radius or obstacle check.") \
    X(TagsBuiltinClosestNameDescription, "Возвращает имя из ника ближайшего по 3D-дистанции другого застримленного игрока.", "Returns the first name from the nickname of the other streamed player with the shortest 3D distance.") \
    X(TagsBuiltinClosestSurnameDescription, "Возвращает фамилию из ника ближайшего по 3D-дистанции другого застримленного игрока.", "Returns the surname from the nickname of the other streamed player with the shortest 3D distance.") \
    X(TagsBuiltinClosestColorDescription, "Возвращает цвет ника ближайшего по 3D-дистанции другого застримленного игрока в формате {RRGGBB}. Если цвет недоступен, возвращает {FFFFFF}.", "Returns the nickname color of the other streamed player with the shortest 3D distance as {RRGGBB}. If the color is unavailable, it returns {FFFFFF}.") \
    X(TagsBuiltinClosestDriverCarDescription, "Возвращает локализованное название транспорта из активного GTA GXT для ближайшего по 3D-дистанции другого застримленного игрока, фактически занимающего водительское место. Если водитель или транспорт недоступен, возвращает пустую строку.", "Returns the localized vehicle name from the active GTA GXT for the other streamed player with the shortest 3D distance who actually occupies the driver seat. If the driver or vehicle is unavailable, it returns an empty string.") \
    X(TagsBuiltinClosestDriverColorDescription, "Возвращает цвет ника ближайшего по 3D-дистанции другого застримленного игрока-водителя в формате {RRGGBB}. Если цвет недоступен, возвращает {FFFFFF}.", "Returns the nickname color of the other streamed driver with the shortest 3D distance as {RRGGBB}. If the color is unavailable, it returns {FFFFFF}.") \
    X(TagsBuiltinClosestDriverIdDescription, "Возвращает ID ближайшего по 3D-дистанции другого застримленного игрока, фактически занимающего водительское место валидного транспорта любого типа.", "Returns the ID of the other streamed player with the shortest 3D distance who actually occupies the driver seat of a valid vehicle of any type.") \
    X(TagsBuiltinClosestDriverNameDescription, "Возвращает имя из ника ближайшего по 3D-дистанции другого застримленного игрока-водителя.", "Returns the first name from the nickname of the other streamed driver with the shortest 3D distance.") \
    X(TagsBuiltinClosestDriverSurnameDescription, "Возвращает фамилию из ника ближайшего по 3D-дистанции другого застримленного игрока-водителя.", "Returns the surname from the nickname of the other streamed driver with the shortest 3D distance.") \
    X(TagsBuiltinArmourDescription, "Возвращает вашу текущую броню. Если брони нет или значение недоступно, вернёт 0.", "Returns your current armour. If there is no armour or the value is unavailable, it returns 0.") \
    X(TagsBuiltinHealthDescription, "Возвращает ваше текущее здоровье локального игрока.", "Returns the current health of the local player.") \
    X(TagsBuiltinPingDescription, "Возвращает ping локального игрока как число без ms. Если SA:MP ещё не готов или значение недоступно, возвращает пустую строку.", "Returns the local player's ping as a number without ms. If SA:MP is not ready yet or the value is unavailable, it returns an empty string.") \
    X(TagsBuiltinMyXDescription, "Возвращает координату X локального игрока с двумя знаками после точки.", "Returns the local player's X coordinate with two decimal places.") \
    X(TagsBuiltinMyYDescription, "Возвращает координату Y локального игрока с двумя знаками после точки.", "Returns the local player's Y coordinate with two decimal places.") \
    X(TagsBuiltinMyZDescription, "Возвращает координату Z локального игрока с двумя знаками после точки.", "Returns the local player's Z coordinate with two decimal places.") \
    X(TagsBuiltinMyPosDescription, "Возвращает координаты локального игрока в формате X, Y, Z с двумя знаками после точки.", "Returns the local player's coordinates as X, Y, Z with two decimal places.") \
    X(TagsBuiltinMyDirectionShortDescription, "Возвращает краткое направление тела локального игрока: С, СЗ, З, ЮЗ, Ю, ЮВ, В или СВ.", "Returns the local player's body direction as a short compass value: N, NW, W, SW, S, SE, E, or NE.") \
    X(TagsBuiltinMyDirectionDescription, "Возвращает направление тела локального игрока: Север, Северо-запад, Запад, Юго-запад, Юг, Юго-восток, Восток или Северо-восток.", "Returns the local player's body direction: North, North-West, West, South-West, South, South-East, East, or North-East.") \
    X(TagsBuiltinMyDirectionShortEnDescription, "Возвращает краткое направление тела локального игрока на английском: N, NW, W, SW, S, SE, E или NE.", "Returns the local player's body direction as a short English compass value: N, NW, W, SW, S, SE, E, or NE.") \
    X(TagsBuiltinMyDirectionEnDescription, "Возвращает направление тела локального игрока на английском: North, North-West, West, South-West, South, South-East, East или North-East.", "Returns the local player's body direction in English: North, North-West, West, South-West, South, South-East, East, or North-East.") \
    X(TagsBuiltinMySquareDescription, "Возвращает квадрат карты локального игрока по Arizona-сетке 250x250 в формате А-1 ... Я-24.", "Returns the local player's map square on the 250x250 Arizona grid as А-1 ... Я-24.") \
    X(TagsBuiltinMySquareEnDescription, "Возвращает квадрат карты локального игрока по той же Arizona-сетке с латинской строкой: A-1 ... Ya-24.", "Returns the local player's map square on the same Arizona grid with a Latin row label: A-1 ... Ya-24.") \
    X(TagsBuiltinCityDescription, "Возвращает текущий город локального игрока: Лос-Сантос, Сан-Фиерро, Лас-Вентурас или Округ.", "Returns the local player's current city: Los Santos, San Fierro, Las Venturas, or Countryside.") \
    X(TagsBuiltinCityEnDescription, "Возвращает текущий город локального игрока на английском: Los Santos, San Fierro, Las Venturas или Countryside.", "Returns the local player's current city in English: Los Santos, San Fierro, Las Venturas, or Countryside.") \
    X(TagsBuiltinClipboardDescription, "Возвращает текст из буфера обмена Windows. Читает Unicode-текст, сохраняет переносы строк и ограничивает результат 4096 символами.", "Returns text from the Windows clipboard. It reads Unicode text, keeps line breaks, and caps the result at 4096 characters.") \
    X(TagsBuiltinMyColorDescription, "Возвращает цвет вашего ника в формате HUD-разметки {RRGGBB}. Если цвет недоступен, возвращает {FFFFFF}.", "Returns your nickname color as HUD markup {RRGGBB}. If the color is unavailable, it returns {FFFFFF}.") \
    X(TagsBuiltinMyCarDescription, "Возвращает локализованное название транспорта локального игрока из активного GTA GXT. Работает для водителя и пассажира; вне транспорта возвращает пустую строку.", "Returns the local player's localized vehicle name from the active GTA GXT. It works for drivers and passengers; outside a vehicle it returns an empty string.") \
    X(TagsBuiltinMyCarHealthDescription, "Возвращает здоровье транспорта локального игрока целым числом. Если игрок не в транспорте, возвращает пустую строку.", "Returns the local player's vehicle health as an integer. If the player is not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinMyCarSpeedDescription, "Возвращает скорость транспорта локального игрока в км/ч целым числом. Если игрок не в транспорте, возвращает пустую строку.", "Returns the local player's vehicle speed in km/h as an integer. If the player is not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinMyCarWindowDescription, "Возвращает 1, если окно возле места локального игрока открыто, и 0, если закрыто. Если игрок, транспорт, место или оконный узел недоступны, возвращает пустую строку.", "Returns 1 when the window beside the local player's seat is open and 0 when it is closed. If the player, vehicle, seat, or window node is unavailable, it returns an empty string.") \
    X(TagsBuiltinMyStaminaDescription, "Возвращает текущую выносливость локального игрока целым числом от 0 до 100 с учётом динамического игрового максимума.", "Returns the local player's current stamina as an integer from 0 to 100 using the dynamic in-game maximum.") \
    X(TagsBuiltinMyOxygenDescription, "Возвращает текущий запас кислорода локального игрока целым числом от 0 до 100 с учётом динамического игрового максимума.", "Returns the local player's current oxygen as an integer from 0 to 100 using the dynamic in-game maximum.") \
    X(TagsBuiltinWeatherDescription, "Возвращает категорию текущей визуально преобладающей игровой погоды на русском. Во время перехода до 50% используется старая погода, затем новая.", "Returns the current visually dominant in-game weather category in Russian. During a transition it uses the old weather below 50%, then the new weather.") \
    X(TagsBuiltinWeatherEnDescription, "Возвращает категорию текущей визуально преобладающей игровой погоды на английском.", "Returns the current visually dominant in-game weather category in English.") \
    X(TagsBuiltinMyCarPlayersIdDescription, "Возвращает SA:MP ID всех людей в вашем транспорте, кроме вас: водитель первым, затем пассажиры по местам.", "Returns SA:MP IDs of all people in your vehicle except you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarPlayersNameDescription, "Возвращает имена всех людей в вашем транспорте, кроме вас: водитель первым, затем пассажиры по местам.", "Returns first names of all people in your vehicle except you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarPlayersSurnameDescription, "Возвращает фамилии всех людей в вашем транспорте, кроме вас: водитель первым, затем пассажиры по местам.", "Returns surnames of all people in your vehicle except you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarPlayersNickDescription, "Возвращает raw SA:MP ники всех людей в вашем транспорте, кроме вас: водитель первым, затем пассажиры по местам.", "Returns raw SA:MP nicknames of all people in your vehicle except you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarPlayersRpNickDescription, "Возвращает RP-ники всех людей в вашем транспорте, кроме вас: водитель первым, затем пассажиры по местам.", "Returns RP nicknames of all people in your vehicle except you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarPassengersIdDescription, "Возвращает SA:MP ID пассажиров в вашем транспорте, кроме вас, по порядку мест.", "Returns SA:MP IDs of passengers in your vehicle except you, ordered by seat.") \
    X(TagsBuiltinMyCarPassengersNameDescription, "Возвращает имена пассажиров в вашем транспорте, кроме вас, по порядку мест.", "Returns first names of passengers in your vehicle except you, ordered by seat.") \
    X(TagsBuiltinMyCarPassengersSurnameDescription, "Возвращает фамилии пассажиров в вашем транспорте, кроме вас, по порядку мест.", "Returns surnames of passengers in your vehicle except you, ordered by seat.") \
    X(TagsBuiltinMyCarPassengersNickDescription, "Возвращает raw SA:MP ники пассажиров в вашем транспорте, кроме вас, по порядку мест.", "Returns raw SA:MP nicknames of passengers in your vehicle except you, ordered by seat.") \
    X(TagsBuiltinMyCarPassengersRpNickDescription, "Возвращает RP-ники пассажиров в вашем транспорте, кроме вас, по порядку мест.", "Returns RP nicknames of passengers in your vehicle except you, ordered by seat.") \
    X(TagsBuiltinMyCarAllPlayersIdDescription, "Возвращает SA:MP ID всех людей в вашем транспорте, включая вас: водитель первым, затем пассажиры по местам.", "Returns SA:MP IDs of all people in your vehicle including you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarAllPlayersNameDescription, "Возвращает имена всех людей в вашем транспорте, включая вас: водитель первым, затем пассажиры по местам.", "Returns first names of all people in your vehicle including you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarAllPlayersSurnameDescription, "Возвращает фамилии всех людей в вашем транспорте, включая вас: водитель первым, затем пассажиры по местам.", "Returns surnames of all people in your vehicle including you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarAllPlayersNickDescription, "Возвращает raw SA:MP ники всех людей в вашем транспорте, включая вас: водитель первым, затем пассажиры по местам.", "Returns raw SA:MP nicknames of all people in your vehicle including you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarAllPlayersRpNickDescription, "Возвращает RP-ники всех людей в вашем транспорте, включая вас: водитель первым, затем пассажиры по местам.", "Returns RP nicknames of all people in your vehicle including you: driver first, then passengers by seat.") \
    X(TagsBuiltinMyCarAllPassengersIdDescription, "Возвращает SA:MP ID пассажиров в вашем транспорте, включая вас, если вы пассажир.", "Returns SA:MP IDs of passengers in your vehicle including you if you are a passenger.") \
    X(TagsBuiltinMyCarAllPassengersNameDescription, "Возвращает имена пассажиров в вашем транспорте, включая вас, если вы пассажир.", "Returns first names of passengers in your vehicle including you if you are a passenger.") \
    X(TagsBuiltinMyCarAllPassengersSurnameDescription, "Возвращает фамилии пассажиров в вашем транспорте, включая вас, если вы пассажир.", "Returns surnames of passengers in your vehicle including you if you are a passenger.") \
    X(TagsBuiltinMyCarAllPassengersNickDescription, "Возвращает raw SA:MP ники пассажиров в вашем транспорте, включая вас, если вы пассажир.", "Returns raw SA:MP nicknames of passengers in your vehicle including you if you are a passenger.") \
    X(TagsBuiltinMyCarAllPassengersRpNickDescription, "Возвращает RP-ники пассажиров в вашем транспорте, включая вас, если вы пассажир.", "Returns RP nicknames of passengers in your vehicle including you if you are a passenger.") \
    X(TagsBuiltinDateDescription, "Возвращает текущую локальную дату в формате ДД.ММ.ГГГГ.", "Returns the current local date formatted as DD.MM.YYYY.") \
    X(TagsBuiltinMySkinDescription, "Возвращает ID текущего скина локального игрока через model index педа.", "Returns the current local player skin ID via the ped model index.") \
    X(TagsBuiltinMyWeaponDescription, "Возвращает название текущего оружия локального игрока.", "Returns the local player's current weapon name.") \
    X(TagsBuiltinMyWeaponIdDescription, "Возвращает ID текущего оружия локального игрока.", "Returns the local player's current weapon ID.") \
    X(TagsBuiltinMyWeaponClipDescription, "Возвращает количество патронов в текущей обойме локального игрока.", "Returns the current ammo-in-clip value for the local player's weapon.") \
    X(TagsBuiltinMyWeaponAmmoDescription, "Возвращает общее количество патронов текущего оружия локального игрока, включая патроны в обойме. Если игрок или текущий слот недоступны, возвращает 0.", "Returns the local player's total ammo for the current weapon, including ammo in the clip. If the player or current slot is unavailable, it returns 0.") \
    X(TagsBuiltinMyMoneyDescription, "Возвращает количество денег на руках у локального игрока.", "Returns the amount of money currently carried by the local player.") \
    X(TagsBuiltinFpsDescription, "Возвращает текущий FPS клиента как целое число.", "Returns the current client FPS as an integer.") \
    X(TagsBuiltinGetVehTypeDescription, "Возвращает тип транспорта, в котором сейчас находится локальный игрок. Если вы не в транспорте, возвращает пустую строку.", "Returns the type of vehicle the local player is currently in. If you are not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinScreenDescription, "Делает скриншот игры и сохраняет его в папку данных GTA SA\\\\HelperByOrc\\\\screens.", "Takes a game screenshot and saves it to the GTA SA userfiles folder\\\\HelperByOrc\\\\screens.") \
    X(TagsBuiltinScreenFunctionDescription, "Делает скриншот игры и сохраняет его в указанную подпапку внутри папки данных GTA SA\\\\HelperByOrc\\\\screens.", "Takes a game screenshot and saves it to the specified subfolder inside the GTA SA userfiles folder\\\\HelperByOrc\\\\screens.") \
    X(TagsBuiltinTPhotoDescription, "Делает игровое фото через механику фотоаппарата GTA SA и сохраняет его в галерею игры.", "Takes an in-game camera photo through GTA SA's photo mechanic and saves it to the game's gallery.") \
    X(TagsBuiltinChatClearDescription, "Локально очищает чат SA:MP пачкой пустых строк и ничего не вставляет в текст. В HUD, preview и pure-режиме игнорируется.", "Locally clears the SA:MP chat with blank lines and inserts no text. It is ignored in HUD, preview, and pure mode.") \
    X(TagsBuiltinCursorDescription, "Удаляется из текста и ставит caret стандартного SA:MP chat input в место маркера. Работает для вставки/открытия chat input; в instant-send путях только удаляется.", "Removes itself from text and places the standard SA:MP chat input caret at the marker position. Works for insert/open chat input paths; instant-send paths only remove it.") \
    X(TagsBuiltinArzCursorDescription, "Удаляется из текста и ставит caret Arizona _chat.asi input в место маркера через безопасный InputText callback hook. Если callback не найден или не прошёл validation, маркер удаляется, а причина пишется в лог.", "Removes itself from text and places the Arizona _chat.asi input caret at the marker position through a safe InputText callback hook. If the callback is not found or fails validation, the marker is removed and the reason is logged.") \
    X(TagsBuiltinCursorDialogDescription, "Удаляется из текста и ставит caret стандартного SA:MP dialog editbox в место маркера. Работает для input/password диалогов.", "Removes itself from text and places the standard SA:MP dialog editbox caret at the marker position. Works for input/password dialogs.") \
    X(TagsBuiltinArzCursorDialogDescription, "Удаляется из текста и ставит caret активного Arizona CEF dialog input/textarea в место маркера через DOM.", "Removes itself from text and places the active Arizona CEF dialog input/textarea caret at the marker position through DOM.") \
    X(TagsBuiltinNickRpDescription, "Возвращает ваш ник в RP-виде: Walcher_Flett станет Walcher Flett.", "Returns your nickname in RP form: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinNameDescription, "Возвращает имя из ника до символа подчёркивания: Walcher_Flett станет Walcher.", "Returns the first name before the underscore: Walcher_Flett becomes Walcher.") \
    X(TagsBuiltinSurnameDescription, "Возвращает фамилию из ника после символа подчёркивания: Walcher_Flett станет Flett.", "Returns the surname after the underscore: Walcher_Flett becomes Flett.") \
    X(TagsBuiltinTimeDescription, "Возвращает текущее локальное время в формате %H:%M:%S.", "Returns the current local time formatted as %H:%M:%S.") \
    X(TagsBuiltinTimeNoSecDescription, "Возвращает текущее локальное время без секунд в формате %H:%M.", "Returns the current local time without seconds in %H:%M format.") \
    X(TagsBuiltinDialogActiveDescription, "Возвращает true, если сейчас открыт активный диалог SA:MP. Иначе возвращает false.", "Returns true if a SA:MP dialog is currently open. Otherwise returns false.") \
    X(TagsBuiltinDialogCaptionDescription, "Возвращает заголовок активного диалога SA:MP. Если диалог не открыт, возвращает пустую строку.", "Returns the caption of the active SA:MP dialog. If no dialog is open, returns an empty string.") \
    X(TagsBuiltinDialogGetSelectedItemDescription, "Возвращает текст выбранного пункта активного диалога со списком. Если список недоступен или ничего не выбрано, возвращает пустую строку.", "Returns the text of the currently selected item in the active list dialog. If the list is unavailable or nothing is selected, it returns an empty string.") \
    X(TagsBuiltinDialogEditboxTextDescription, "Возвращает текущее содержимое editbox активного диалога SA:MP. Работает для input/password диалогов. Если editbox нет, возвращает пустую строку.", "Returns the current contents of the active SA:MP dialog edit box. Works for input/password dialogs. If there is no edit box, it returns an empty string.") \
    X(TagsBuiltinDialogSelectedIndexDescription, "Возвращает текущий выбранный индекс активного list/tablist диалога. Индекс 0-based. Если список недоступен или ничего не выбрано, возвращает пустую строку.", "Returns the currently selected index of the active list/tablist dialog. The index is 0-based. If the list is unavailable or nothing is selected, it returns an empty string.") \
    X(TagsBuiltinDialogGetIdDescription, "Возвращает ID активного диалога SA:MP. Если диалог не открыт, возвращает пустую строку.", "Returns the ID of the active SA:MP dialog. If no dialog is open, returns an empty string.") \
    X(TagsBuiltinArzDialogSetInputTextDescription, "Устанавливает текст в активное input-поле Arizona CEF диалога через DOM. Ничего не вставляет в текст.", "Sets the text in the active Arizona CEF dialog input field through DOM. Inserts no text.") \
    X(TagsBuiltinArzDialogGetInputTextDescription, "Возвращает последний кэшированный текст input-поля Arizona CEF диалога.", "Returns the last cached Arizona CEF dialog input text.") \
    X(TagsBuiltinArzDialogGetInputTextQueryDescription, "Запускает CEF-запрос input-текста Arizona CEF диалога. В выполняющемся bind текущая строка отложится до ответа или timeout; вне bind сразу вернётся кэш. Timeout максимум 3000 мс.", "Starts a CEF query for the Arizona CEF dialog input text. In a running bind, the current line is deferred until response or timeout; outside binds, the cache is returned immediately. Timeout is capped at 3000 ms.") \
    X(TagsBuiltinArzDialogCloseWithButtonDescription, "Закрывает Arizona CEF диалог указанной кнопкой через DOM: 1 = Enter/primary, 0 = Escape/secondary.", "Closes the Arizona CEF dialog through DOM: 1 = Enter/primary, 0 = Escape/secondary.") \
    X(TagsBuiltinArzDialogSetListItemDescription, "Выбирает пункт списка Arizona CEF диалога по 0-based индексу через DOM. Ничего не вставляет в текст.", "Selects an Arizona CEF dialog list item by 0-based index through DOM. Inserts no text.") \
    X(TagsBuiltinArzDialogItemDescription, "Открывает пункт активного Arizona CEF диалога по номеру или по части текста. Для чисел используется привычная 1-based форма: [ARZdialogitem(1)] откроет первый пункт; 0 принимается как alias первого пункта. Сначала пробует live DOM-список, затем кэш RPC 61.", "Opens an active Arizona CEF dialog item by number or partial text. Numeric arguments use the familiar 1-based form: [ARZdialogitem(1)] opens the first item; 0 is accepted as an alias for the first item. It tries the live DOM list first, then the RPC 61 cache.") \
    X(TagsBuiltinArzDialogGetListItemDescription, "Возвращает последний кэшированный 0-based индекс выбранного пункта Arizona CEF диалога.", "Returns the last cached 0-based selected item index of the Arizona CEF dialog.") \
    X(TagsBuiltinArzDialogGetListItemQueryDescription, "Запускает CEF-запрос 0-based индекса выбранного пункта Arizona CEF диалога. В выполняющемся bind текущая строка отложится до ответа или timeout; вне bind сразу вернётся кэш. Timeout максимум 3000 мс.", "Starts a CEF query for the current 0-based selected Arizona CEF dialog item index. In a running bind, the current line is deferred until response or timeout; outside binds, the cache is returned immediately. Timeout is capped at 3000 ms.") \
    X(TagsBuiltinArzDialogIsActiveDescription, "Возвращает true, если сейчас активен диалог SA:MP/Arizona CEF. Иначе возвращает false.", "Returns true if a SA:MP/Arizona CEF dialog is currently active. Otherwise returns false.") \
    X(TagsBuiltinArzDialogGetIdDescription, "Возвращает ID последнего Arizona CEF диалога из RPC 61. Если данных нет, возвращает пустую строку.", "Returns the last Arizona CEF dialog ID from RPC 61. If no data is cached, returns an empty string.") \
    X(TagsBuiltinArzDialogGetStyleDescription, "Возвращает стиль последнего Arizona CEF диалога из RPC 61.", "Returns the last Arizona CEF dialog style from RPC 61.") \
    X(TagsBuiltinArzDialogGetTitleDescription, "Возвращает заголовок последнего Arizona CEF диалога из RPC 61.", "Returns the title of the last Arizona CEF dialog from RPC 61.") \
    X(TagsBuiltinArzDialogGetButton1Description, "Возвращает текст первой кнопки последнего Arizona CEF диалога из RPC 61.", "Returns the first button text of the last Arizona CEF dialog from RPC 61.") \
    X(TagsBuiltinArzDialogGetButton2Description, "Возвращает текст второй кнопки последнего Arizona CEF диалога из RPC 61.", "Returns the second button text of the last Arizona CEF dialog from RPC 61.") \
    X(TagsBuiltinArzDialogGetDialogTextDescription, "Возвращает текст последнего Arizona CEF диалога из RPC 61.", "Returns the text of the last Arizona CEF dialog from RPC 61.") \
    X(TagsBuiltinArzDialogGetDialogTextFunctionDescription, "Возвращает токен текста последнего Arizona CEF диалога из RPC 61 по 0-based индексу. Разбиение такое же, как у [dialogtext(...)]. Кнопка \"Подобрать индекс\" в карточке тега открывает список доступных индексов.", "Returns a token from the last Arizona CEF dialog text from RPC 61 by 0-based index. Splitting matches [dialogtext(...)]. The \"Pick index\" button in the tag card opens the list of available indexes.") \
    X(TagsBuiltinArzDialogGetRespondDescription, "Возвращает последний ответ Arizona CEF диалога из RPC 62 в формате id;button;list;input.", "Returns the last Arizona CEF dialog response from RPC 62 as id;button;list;input.") \
    X(TagsBuiltinArzDialogRespondIdDescription, "Возвращает id последнего ответа Arizona CEF диалога из RPC 62.", "Returns the id of the last Arizona CEF dialog response from RPC 62.") \
    X(TagsBuiltinArzDialogRespondButtonDescription, "Возвращает button последнего ответа Arizona CEF диалога из RPC 62.", "Returns the button of the last Arizona CEF dialog response from RPC 62.") \
    X(TagsBuiltinArzDialogRespondListDescription, "Возвращает list item последнего ответа Arizona CEF диалога из RPC 62.", "Returns the list item of the last Arizona CEF dialog response from RPC 62.") \
    X(TagsBuiltinArzDialogRespondInputDescription, "Возвращает input последнего ответа Arizona CEF диалога из RPC 62.", "Returns the input text of the last Arizona CEF dialog response from RPC 62.") \
    X(TagsBuiltinArzDialogSendRespondDescription, "Отправляет прямой RPC 62 как sampSendDialogResponse. Формат: [ARZdialogsendrespond(id;button;listitem;input)]. Пустой listitem допустим, input читается как остаток строки.", "Sends direct RPC 62 like sampSendDialogResponse. Format: [ARZdialogsendrespond(id;button;listitem;input)]. Empty listitem is allowed, input is read as the rest of the string.") \
    X(TagsBuiltinTimefDescription, "Пишет текущее локальное время в указанном формате strftime. В конце формата обязательно нужна точка с запятой ;\n%a — сокращённое название дня недели\n%A — полное название дня недели\n%b — сокращённое название месяца\n%B — полное название месяца\n%c — дата и время целиком\n%d — день месяца [01-31]\n%H — час в 24-часовом формате [00-23]\n%I — час в 12-часовом формате [01-12]\n%M — минуты [00-59]\n%m — месяц [01-12]\n%p — am или pm\n%S — секунды [00-61]\n%w — день недели [0-6, где 0 — воскресенье]\n%x — дата\n%X — время\n%Y — полный год\n%y — двухзначный год [00-99]\n%% — символ %", "Prints the current local time using the specified strftime format. The format must end with a semicolon ;\n%a — abbreviated weekday name\n%A — full weekday name\n%b — abbreviated month name\n%B — full month name\n%c — full date and time\n%d — day of month [01-31]\n%H — hour in 24-hour format [00-23]\n%I — hour in 12-hour format [01-12]\n%M — minute [00-59]\n%m — month [01-12]\n%p — am or pm\n%S — second [00-61]\n%w — weekday [0-6, Sunday = 0]\n%x — date\n%X — time\n%Y — full year\n%y — two-digit year [00-99]\n%% — percent sign") \
    X(TagsBuiltinNickFunctionDescription, "Возвращает ник игрока по указанному ID. Работает и в обычном чате, и в биндах.", "Returns a player's nickname for the specified ID. Works in regular chat and in binds.") \
    X(TagsBuiltinRpNickFunctionDescription, "Возвращает RP-ник игрока по указанному ID: Walcher_Flett станет Walcher Flett.", "Returns a player's RP nickname for the specified ID: Walcher_Flett becomes Walcher Flett.") \
    X(TagsBuiltinNameFunctionDescription, "Возвращает имя из ника игрока по указанному ID.", "Returns the first name from the nickname of the specified player ID.") \
    X(TagsBuiltinSurnameFunctionDescription, "Возвращает фамилию из ника игрока по указанному ID.", "Returns the surname from the nickname of the specified player ID.") \
    X(TagsBuiltinParamcmdDescription, "Достаёт параметры из команды, которой был запущен бинд. Поддерживает селекторы 1, 1+, 3-, 2-4.", "Extracts arguments from the command that launched the bind. Supports selectors like 1, 1+, 3-, and 2-4.") \
    X(TagsBuiltinKeyEmulateDescription, "Эмулирует одно нажатие указанной виртуальной клавиши Windows и ничего не вставляет в текст. В sandbox-предпросмотре не срабатывает.", "Emulates a single press of the specified Windows virtual key and inserts no text. It stays inactive in sandbox previews.") \
    X(TagsBuiltinMathDescription, "Вычисляет арифметическое выражение. Поддерживает +, -, *, /, %, скобки и унарные знаки.", "Evaluates an arithmetic expression. Supports +, -, *, /, %, parentheses, and unary signs.") \
    X(TagsBuiltinNumberWithDotsDescription, "Форматирует число, разделяя целую часть точками по тысячам: 1000 станет 1.000, а -12345.67 станет -12.345.67.", "Formats a number by inserting dots every three digits in the integer part: 1000 becomes 1.000 and -12345.67 becomes -12.345.67.") \
    X(TagsBuiltinCyrToLatDescription, "Преобразует 33 русские буквы в читаемый латинский транслит, сохраняя регистр и остальные символы. Точные пары из общего файла vars/transliteration.txt имеют приоритет над стандартной таблицей.", "Transliterates the 33 Russian letters into readable Latin text while preserving case and other characters. Exact pairs from the shared vars/transliteration.txt file take priority over the standard table.") \
    X(TagsBuiltinLatToCyrDescription, "Преобразует латинский транслит в русские буквы по самому длинному совпадению. Поддерживает th, ph, ck, c, h, q, исправленные старые формы и точные пары из общего файла vars/transliteration.txt.", "Converts Latin transliteration to Russian letters using the longest match. It supports th, ph, ck, c, h, q, repaired legacy forms, and exact pairs from the shared vars/transliteration.txt file.") \
    X(TagsBuiltinToRomanDescription, "Преобразует арабское число от 1 до 3999 в каноническую римскую запись. Разрешены внешние пробелы и ведущие нули; невалидный аргумент возвращается без изменений.", "Converts an Arabic number from 1 to 3999 to canonical Roman notation. Surrounding whitespace and leading zeroes are accepted; an invalid argument is returned unchanged.") \
    X(TagsBuiltinFromRomanDescription, "Преобразует каноническое римское число из I, V, X, L, C, D, M без учёта регистра в арабское число от 1 до 3999. Невалидный аргумент возвращается без изменений.", "Converts a canonical case-insensitive Roman number using I, V, X, L, C, D, and M to an Arabic number from 1 to 3999. An invalid argument is returned unchanged.") \
    X(TagsBuiltinArmourFunctionDescription, "Возвращает броню игрока по ID. Если игрок недоступен или значение не удалось получить, возвращает пустую строку.", "Returns a player's armour by ID. If the player is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinHealthFunctionDescription, "Возвращает здоровье игрока по ID. Если игрок недоступен или значение не удалось получить, возвращает пустую строку.", "Returns a player's health by ID. If the player is unavailable or the value cannot be read, it returns an empty string.") \
    X(TagsBuiltinPingFunctionDescription, "Возвращает ping игрока по ID или точному нику как число без ms. Параметр сначала раскрывает вложенные теги, поэтому работают [ping({targetid})], [ping({targetnick})] и [ping([nick(5)])]. Если игрок не найден или значение недоступно, возвращает пустую строку.", "Returns a player's ping by ID or exact nickname as a number without ms. The parameter expands nested tags first, so [ping({targetid})], [ping({targetnick})], and [ping([nick(5)])] work. If the player is not found or the value is unavailable, it returns an empty string.") \
    X(TagsBuiltinSkinFunctionDescription, "Возвращает model index педа игрока по указанному ID. Если пед игрока не найден или не застримлен, возвращает пустую строку.", "Returns the ped model index for the specified player ID. If the player's ped is not found or not streamed in, it returns an empty string.") \
    X(TagsBuiltinNickColorFunctionDescription, "Возвращает цвет ника игрока в HUD-разметке {RRGGBB}. Если цвет недоступен, возвращает белый.", "Returns the player's nick color as HUD markup {RRGGBB}. If the color is unavailable, it returns white.") \
    X(TagsBuiltinCarFunctionDescription, "Возвращает локализованное название транспорта игрока по ID из активного GTA GXT. Если игрок не в транспорте или не застримлен, возвращает пустую строку.", "Returns a player's localized vehicle name by ID from the active GTA GXT. If the player is not in a vehicle or is not streamed in, it returns an empty string.") \
    X(TagsBuiltinCarHealthFunctionDescription, "Возвращает здоровье транспорта игрока по ID. Если транспорт недоступен, возвращает пустую строку.", "Returns the vehicle health for a player by ID. If the vehicle is unavailable, it returns an empty string.") \
    X(TagsBuiltinCarWindowFunctionDescription, "Возвращает 1, если окно возле места игрока с указанным ID открыто, и 0, если закрыто. Недоступный, незастримленный или не находящийся в поддерживаемом месте игрок даёт пустую строку.", "Returns 1 when the window beside the specified player ID's seat is open and 0 when it is closed. An unavailable, unstreamed, or unsupported-seat player returns an empty string.") \
    X(TagsBuiltinKeyDownDescription, "Зажимает указанную виртуальную клавишу Windows на заданное количество миллисекунд. Формат: [keydown(КодКлавиши;Мс)]. В уже запущенном бинде тег ставит выполнение на паузу до конца удержания.", "Holds the specified Windows virtual key for the requested number of milliseconds. Format: [keydown(KeyCode;Ms)]. In a running bind, the tag pauses execution until the hold finishes.") \
    X(TagsBuiltinStrLowDescription, "Возвращает текст в нижнем регистре с Unicode-преобразованием для кириллицы, латиницы и других поддерживаемых Windows письменностей.", "Returns text in lowercase using Windows Unicode casing for Cyrillic, Latin, and other supported scripts.") \
    X(TagsBuiltinAddTimeDescription, "Возвращает текущее локальное время плюс указанное смещение. Поддерживает форматы MM:SS и HH:MM:SS. Результат всегда выводится как %H:%M:%S.", "Returns the current local time plus the specified offset. Supports MM:SS and HH:MM:SS formats. The result is always printed as %H:%M:%S.") \
    X(TagsBuiltinRandomDescription, "Генерирует случайное значение. [random()] даёт число от -2147483647 до 2147483647, [random(10)] — от 1 до 10, [random(20-30)] — от 20 до 30, [random(Да;Нет;Не знаю)] — случайный вариант из списка.", "Generates a random value. [random()] returns a number from -2147483647 to 2147483647, [random(10)] returns 1 to 10, [random(20-30)] returns 20 to 30, and [random(Yes;No;Maybe)] picks a random option from the list.") \
    X(TagsBuiltinIfAndOrDescription, "Возвращает один из двух вариантов по условию.\n\nСинтаксис:\n[ifandor(Условие?Верно:Неверно)]\n\nКак это работает:\n1. Сначала вычисляется только Условие.\n2. Если условие истинно, раскрывается и выполняется только часть Верно.\n3. Если условие ложно, раскрывается и выполняется только часть Неверно.\n4. Невыбранная ветка вообще не раскрывается и не выполняется.\n\nЭто важно:\n- Внутри Верно и Неверно можно безопасно использовать функциональные переменные с действиями, например [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Выполнится только выбранная ветка.\n- Внутри самого условия side effects запрещены: условие нужно только для проверки.\n\nЧто можно писать в условии:\n- операторы сравнения: ==, !=, >, >=, <, <=\n- логические операторы: and, or, not\n- круглые скобки: ( )\n- числа: 1, 74, 12.5\n- строки в кавычках: \"text\" или 'text'\n- булевы значения: true, false\n- обычные и безопасные функциональные переменные после подстановки, например {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nКак сравниваются значения:\n- Если обе стороны похожи на числа, сравнение будет числовым.\n- Иначе сравнение будет строковым.\n\nПримеры:\n- [ifandor({id}==74?Мой id 74:Мой id не 74)]\n- [ifandor(1>0?[bindstart(30)]:[bindstart(@bind-62)])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?День:Не день)]\n- [ifandor([bindended({thisbind})]==1?Бинд завершён:Бинд ещё работает)]\n\nВажно по синтаксису:\n- Формат строго один: Условие?Верно:Неверно\n- Если в строках есть текст, лучше брать его в кавычки при сравнении.\n- Старый формат вида @...@ не используется. Используйте текущий синтаксис [ ... ].", "Returns one of two branches by condition.\n\nSyntax:\n[ifandor(Condition?True:False)]\n\nHow it works:\n1. Only the Condition is evaluated first.\n2. If the condition is true, only the True branch is expanded and executed.\n3. If the condition is false, only the False branch is expanded and executed.\n4. The branch that was not selected is never expanded or executed.\n\nThis matters:\n- You can safely use action-oriented functional variables inside True and False, such as [bindstart(...)], [bindstop(...)], [keyemulate(...)], [wait(...)].\n- Only the selected branch runs.\n- Side effects are forbidden inside the condition itself; the condition is for checking only.\n\nSupported inside the condition:\n- comparison operators: ==, !=, >, >=, <, <=\n- logical operators: and, or, not\n- parentheses: ( )\n- numbers: 1, 74, 12.5\n- quoted strings: \"text\" or 'text'\n- boolean values: true, false\n- regular and safe functional variables after expansion, such as {id}, [timef(%H;)], [math(2+2)], [bindended({thisbind})]\n\nHow values are compared:\n- If both sides look like numbers, comparison is numeric.\n- Otherwise comparison is string-based.\n\nExamples:\n- [ifandor({id}==74?My id is 74:My id is not 74)]\n- [ifandor(1>0?[bindstart(30)]:[bindstart(@bind-62)])]\n- [ifandor(([timef(%H;)]>=\"12\") and ([timef(%H;)]<\"20\")?Day:Not day)]\n- [ifandor([bindended({thisbind})]==1?Bind finished:Bind still running)]\n\nSyntax notes:\n- The format is strictly Condition?True:False\n- If you compare text, quoting it is recommended.\n- The old @...@ form is not used. Use the current [ ... ] syntax.") \
    X(TagsBuiltinGetVehTypeFunctionDescription, "Возвращает тип транспорта игрока по указанному ID. Если игрок не найден или не находится в транспорте, возвращает пустую строку.", "Returns the type of vehicle used by the specified player ID. If the player is not found or is not in a vehicle, it returns an empty string.") \
    X(TagsBuiltinWaitDescription, "Переопределяет паузу до следующей строки у уже запущенного бинда. Работает как обычная задержка между строками и ничего не вставляет в текст.", "Overrides the delay before the next line of an already running bind. Works like the regular delay between lines and inserts no text.") \
    X(TagsBuiltinCursorFunctionDescription, "Ставит caret или выделение стандартного SA:MP chat input по 0-based позиции: [cursor(start)] или [cursor(start;finish)]. Ничего не вставляет в текст.", "Places the standard SA:MP chat input caret or selection by 0-based position: [cursor(start)] or [cursor(start;finish)]. Inserts no text.") \
    X(TagsBuiltinArzCursorFunctionDescription, "Ставит caret или выделение Arizona _chat.asi input по 0-based UTF-8 byte позиции через InputText callback hook: [ARZcursor(start)] или [ARZcursor(start;finish)]. Ничего не вставляет в текст.", "Places the Arizona _chat.asi input caret or selection by 0-based UTF-8 byte position through an InputText callback hook: [ARZcursor(start)] or [ARZcursor(start;finish)]. Inserts no text.") \
    X(TagsBuiltinCursorDialogFunctionDescription, "Ставит caret или выделение стандартного SA:MP dialog editbox по 0-based позиции: [cursordialog(start)] или [cursordialog(start;finish)]. Ничего не вставляет в текст.", "Places the standard SA:MP dialog editbox caret or selection by 0-based position: [cursordialog(start)] or [cursordialog(start;finish)]. Inserts no text.") \
    X(TagsBuiltinArzCursorDialogFunctionDescription, "Ставит caret или выделение активного Arizona CEF dialog input/textarea по 0-based DOM-позиции: [ARZcursordialog(start)] или [ARZcursordialog(start;finish)].", "Places the active Arizona CEF dialog input/textarea caret or selection by 0-based DOM position: [ARZcursordialog(start)] or [ARZcursordialog(start;finish)].") \
    X(TagsBuiltinDialogCloseDescription, "Закрывает активный диалог SA:MP. Параметр 1 отправляет положительный ответ (Enter/OK), параметр 0 отправляет отрицательный ответ (Esc/Cancel). Ничего не вставляет в текст.", "Closes the active SA:MP dialog. Parameter 1 sends the positive response (Enter/OK), parameter 0 sends the negative response (Esc/Cancel). Inserts no text.") \
    X(TagsBuiltinDialogSetTextDescription, "Устанавливает текст в editbox активного диалога SA:MP. Работает только для input/password диалогов и ничего не вставляет в текст.", "Sets the text of the active SA:MP dialog editbox. Works only for input/password dialogs and inserts no text.") \
    X(TagsBuiltinDialogWaitOpenDescription, "Ничего не вставляет в текст и дожидается появления активного диалога SA:MP до 3 секунд. Если диалог не открылся, текущий бинд будет остановлен.", "Inserts no text and waits up to 3 seconds for an active SA:MP dialog to appear. If no dialog opens, the current bind is stopped.") \
    X(TagsBuiltinDialogWaitCloseDescription, "Ничего не вставляет в текст и ставит текущий бинд на паузу, пока открыт диалог SA:MP. После закрытия диалога выполнение продолжается.", "Inserts no text and pauses the current bind while a SA:MP dialog remains open. Execution resumes after the dialog closes.") \
    X(TagsBuiltinDialogItemDescription, "Открывает активный диалоговый пункт по номеру или по части текста. Для чисел поддерживается привычная 1-based форма: [dialogitem(1)] нажмёт первый пункт. В list/tablist диалогах кнопка \"Подобрать пункт\" в карточке тега открывает picker и сразу даёт готовый пример.", "Opens the active dialog item by number or by partial text. Numeric arguments support the familiar 1-based form: [dialogitem(1)] presses the first item. In list/tablist dialogs, the \"Pick item\" button in the tag card opens the picker and gives a ready-made example.") \
    X(TagsBuiltinDialogSelectDescription, "Выбирает пункт активного list/tablist диалога без нажатия Enter. Поддерживает номер или часть текста пункта. Для чисел используется привычная 1-based форма: [dialogselect(1)] выберет первый пункт, но не отправит диалог.", "Selects an item in the active list/tablist dialog without pressing Enter. Supports an item number or a partial text match. Numeric arguments use the familiar 1-based form: [dialogselect(1)] selects the first item without submitting the dialog.") \
    X(TagsBuiltinDialogWaitIdDescription, "Ничего не вставляет в текст и ждёт появления конкретного dialog id до 3 секунд. Если нужный диалог уже открыт, выполнение продолжается сразу. Если за 3 секунды нужный id так и не появился, текущий бинд будет остановлен.", "Inserts no text and waits up to 3 seconds for a specific dialog id to appear. If the requested dialog is already open, execution continues immediately. If that id does not appear within 3 seconds, the current bind is stopped.") \
    X(TagsBuiltinDialogResponseDescription, "Универсально отвечает на активный диалог одной переменной.\n\nСинтаксис:\n[dialogresponse(button;item;text)]\n\nПараметры:\n- button — обязательный. 1 = положительный ответ (Enter/OK), 0 = отрицательный ответ (Esc/Cancel).\n- item — необязательный. Используется только для list/tablist диалогов. Можно указать номер пункта или часть его текста.\n- text — необязательный. Используется только для input/password диалогов и задаёт текст для editbox.\n\nКак это работает:\n- Для msgbox достаточно button: [dialogresponse(1)]\n- Для list/tablist можно выбрать пункт и сразу подтвердить: [dialogresponse(1;3;)] или [dialogresponse(1;Инвентарь;)]\n- Для input/password можно передать текст и сразу подтвердить: [dialogresponse(1;;Пример)]\n- Для отрицательного ответа достаточно [dialogresponse(0)] — item и text будут проигнорированы.\n\nВажно:\n- Формат всегда один и тот же: button;item;text\n- Если какой-то параметр не нужен, оставьте его пустым, но сохраните разделители. Например: [dialogresponse(1;;Пример)]\n- item для чисел работает в привычной 1-based форме, как [dialogitem(1)]\n- item по тексту ищется по части текста без учёта регистра\n- text и item перед отправкой безопасно раскрывают обычные переменные и pure-функции, но не выполняют action-теги\n- Сам тег ничего не вставляет в текст и только отправляет ответ в активный диалог\n\nПримеры:\n- [dialogresponse(1)]\n- [dialogresponse(0)]\n- [dialogresponse(1;1;)]\n- [dialogresponse(1;Навыки персонажа;)]\n- [dialogresponse(1;;{nick})]\n- [dialogresponse(1;;[timef(%H:%M:%S;)])]", "Sends a universal response to the active dialog with a single variable.\n\nSyntax:\n[dialogresponse(button;item;text)]\n\nParameters:\n- button — required. 1 = positive response (Enter/OK), 0 = negative response (Esc/Cancel).\n- item — optional. Used only for list/tablist dialogs. You can pass an item number or a partial item text.\n- text — optional. Used only for input/password dialogs and sets the edit box text.\n\nHow it works:\n- For a msgbox only button is needed: [dialogresponse(1)]\n- For list/tablist dialogs you can select an item and confirm it immediately: [dialogresponse(1;3;)] or [dialogresponse(1;Inventory;)]\n- For input/password dialogs you can pass text and confirm immediately: [dialogresponse(1;;Example)]\n- For a negative response, [dialogresponse(0)] is enough — item and text are ignored.\n\nImportant:\n- The format is always button;item;text\n- If a parameter is not needed, leave it empty but keep the separators. Example: [dialogresponse(1;;Example)]\n- Numeric item values use the familiar 1-based form, like [dialogitem(1)]\n- Text item lookup is case-insensitive and matches by partial text\n- item and text safely expand regular variables and pure functions before submit, but do not execute action tags\n- The tag inserts no text and only sends a response to the active dialog\n\nExamples:\n- [dialogresponse(1)]\n- [dialogresponse(0)]\n- [dialogresponse(1;1;)]\n- [dialogresponse(1;Skills;)]\n- [dialogresponse(1;;{nick})]\n- [dialogresponse(1;;[timef(%H:%M:%S;)])]") \
    X(TagsBuiltinDialogTextDescription, "Возвращает токен текста активного диалога по 0-based индексу. Текст разбивается примерно как в Lua-версии: пробелы и табы разделяют слова, а символы [](){} считаются отдельными токенами. Кнопка \"Подобрать индекс\" в карточке тега открывает список доступных индексов.", "Returns a token from the active dialog text by 0-based index. The text is split similarly to the Lua version: spaces and tabs separate words, while [](){} are treated as standalone tokens. The \"Pick index\" button in the tag card opens the list of available indexes.") \
    X(TagsBuiltinSaveDialogDescription, "Сохраняет активный диалог в .txt файл в папку данных GTA SA\\\\HelperByOrc\\\\saved\\\\dialogs. Если аргумент не указан, имя файла берётся из заголовка диалога. Ничего не вставляет в текст.", "Saves the active dialog to a .txt file in the GTA SA userfiles folder\\\\HelperByOrc\\\\saved\\\\dialogs. If no argument is provided, the file name is derived from the dialog caption. Inserts no text.") \
    X(TagsBuiltinBindDisableDescription, "Выключает выбранный бинд. Уже запущенный бинд не останавливается.\n\nАргументы selector по позициям:\n1. \"Имя бинда\".\n2. \"Папка/Подпапка\"; \"\" означает «Без папки», \"*\" — любую папку указанной категории.\n3. \"Категория\".\n\nФормы: [binddisable(@bind-62)] — стабильный ID; [binddisable(31)] — глобальный UI-номер; [binddisable(\"№31 Имя бинда\")] — номер с проверкой имени; [binddisable(\"Имя бинда\")] — имя во всей текущей категории; [binddisable(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")] — полный selector.\n\nРаботает внутри выполняющегося бинда после текущей строки. Повторное выключение — успешное действие без изменений.", "Disables the selected bind. A bind that is already running is not stopped.\n\nSelector arguments by position:\n1. \"Bind name\".\n2. \"Folder/Subfolder\"; \"\" means No folder and \"*\" means any folder in the selected category.\n3. \"Category\".\n\nForms: [binddisable(@bind-62)] is a stable ID; [binddisable(31)] is a global UI number; [binddisable(\"No.31 Bind name\")] verifies the number and name; [binddisable(\"Bind name\")] searches the current category; [binddisable(\"Bind name\" \"Folder/Subfolder\" \"Category\")] is the full selector.\n\nRuns after the current line inside a running bind. Disabling an already disabled bind succeeds without changes.") \
    X(TagsBuiltinBindEnableDescription, "Включает выбранный бинд. Родительские папки автоматически не включаются.\n\nАргументы selector: 1 — \"Имя бинда\"; 2 — \"Папка/Подпапка\" или \"\" для уровня «Без папки»; 3 — \"Категория\".\n\nПримеры: [bindenable(@bind-62)], [bindenable(31)], [bindenable(\"№31 Имя бинда\")], [bindenable(\"Имя бинда\" \"\")], [bindenable(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")].\n\nРаботает внутри выполняющегося бинда после текущей строки. Повторное включение — успешное действие без изменений.", "Enables the selected bind. Parent folders are not enabled automatically.\n\nSelector arguments: 1 — \"Bind name\"; 2 — \"Folder/Subfolder\" or \"\" for No folder; 3 — \"Category\".\n\nExamples: [bindenable(@bind-62)], [bindenable(31)], [bindenable(\"No.31 Bind name\")], [bindenable(\"Bind name\" \"\")], [bindenable(\"Bind name\" \"Folder/Subfolder\" \"Category\")].\n\nRuns after the current line inside a running bind. Enabling an already enabled bind succeeds without changes.") \
    X(TagsBuiltinBindStartDescription, "Запускает выбранный бинд после текущей строки и ничего не вставляет в текст.\n\nАргументы selector по позициям:\n1. \"Имя бинда\".\n2. \"Папка/Подпапка\"; \"\" — «Без папки», \"*\" — любая папка указанной категории.\n3. \"Категория\".\n\nПримеры: [bindstart(@bind-62)], [bindstart(31)], [bindstart(\"№31 Имя бинда\")], [bindstart(\"Имя бинда\")], [bindstart(\"Имя бинда\" \"\")], [bindstart(\"Имя бинда\" \"Папка/Подпапка\")], [bindstart(\"Имя бинда\" \"*\" \"Категория\")], [bindstart(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")].\n\nКавычки означают полное совпадение, аргумент без кавычек допускает частичный поиск. Выключенный бинд или выключенная папка не запускаются. Текущий бинд по {thisbind} получает один отложенный перезапуск; уже запущенный другой бинд возвращает ошибку.", "Starts the selected bind after the current line and inserts no text.\n\nSelector arguments by position:\n1. \"Bind name\".\n2. \"Folder/Subfolder\"; \"\" means No folder and \"*\" means any folder in the selected category.\n3. \"Category\".\n\nExamples: [bindstart(@bind-62)], [bindstart(31)], [bindstart(\"No.31 Bind name\")], [bindstart(\"Bind name\")], [bindstart(\"Bind name\" \"\")], [bindstart(\"Bind name\" \"Folder/Subfolder\")], [bindstart(\"Bind name\" \"*\" \"Category\")], [bindstart(\"Bind name\" \"Folder/Subfolder\" \"Category\")].\n\nQuoted arguments require an exact match; unquoted arguments allow partial lookup. Disabled binds and disabled folders cannot start. The current bind targeted through {thisbind} gets one deferred restart; another bind that is already running produces an error.") \
    X(TagsBuiltinBindStopDescription, "Останавливает выбранный запущенный бинд после текущей строки.\n\nБез аргументов или с {thisbind} выбирается текущий бинд. Полный selector: [bindstop(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")], где аргументы идут строго как Имя бинда → Папка/Подпапка → Категория. Также поддерживаются @bind-62, глобальный номер 31 и \"№31 Имя бинда\".\n\nМожно остановить уже выполняющийся бинд, даже если он или его папка выключены. Если бинд не запущен, возвращается ошибка.", "Stops the selected running bind after the current line.\n\nNo arguments or {thisbind} select the current bind. Full selector: [bindstop(\"Bind name\" \"Folder/Subfolder\" \"Category\")], with arguments strictly ordered as Bind name → Folder/Subfolder → Category. @bind-62, global number 31, and \"No.31 Bind name\" are also supported.\n\nA running bind can be stopped even when it or its folder is disabled. An inactive bind produces an error.") \
    X(TagsBuiltinBindPauseDescription, "Ставит выбранный запущенный бинд на паузу после текущей строки.\n\nБез аргументов или с {thisbind} выбирается текущий бинд. Полный selector: [bindpause(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")]. Порядок аргументов: Имя бинда → Папка/Подпапка → Категория. Поддерживаются @bind-62, номер 31 и \"№31 Имя бинда\".\n\nВыключенное состояние не мешает управлять уже запущенным биндом. Незапущенный или уже поставленный на паузу бинд возвращает точную ошибку состояния.", "Pauses the selected running bind after the current line.\n\nNo arguments or {thisbind} select the current bind. Full selector: [bindpause(\"Bind name\" \"Folder/Subfolder\" \"Category\")]. Argument order: Bind name → Folder/Subfolder → Category. @bind-62, number 31, and \"No.31 Bind name\" are supported.\n\nDisabled state does not prevent controlling an already running bind. An inactive or already paused bind produces an exact state error.") \
    X(TagsBuiltinBindUnpauseDescription, "Снимает паузу с выбранного бинда после текущей строки.\n\nSelector: [bindunpause(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")]. Позиции всегда одинаковы: 1 — Имя бинда, 2 — Папка/Подпапка или \"\" для «Без папки», 3 — Категория. Также поддерживаются @bind-62, номер 31, \"№31 Имя бинда\" и {thisbind}.\n\nВыключенное состояние не мешает продолжить уже запущенный бинд. Если бинд не стоит на паузе, возвращается ошибка.", "Resumes the selected paused bind after the current line.\n\nSelector: [bindunpause(\"Bind name\" \"Folder/Subfolder\" \"Category\")]. Positions are always: 1 — Bind name, 2 — Folder/Subfolder or \"\" for No folder, 3 — Category. @bind-62, number 31, \"No.31 Bind name\", and {thisbind} are also supported.\n\nDisabled state does not prevent resuming an already running bind. A bind that is not paused produces an error.") \
    X(TagsBuiltinBindFastMenuDescription, "Добавляет выбранный бинд в быстрое меню.\n\nSelector: [bindfastmenu(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")]. Аргументы: 1 — Имя бинда; 2 — Папка/Подпапка или \"\" для «Без папки»; 3 — Категория. Также поддерживаются @bind-62, номер 31 и \"№31 Имя бинда\".\n\nРаботает после текущей строки. Если бинд уже находится в быстром меню, действие успешно завершается без изменений.", "Adds the selected bind to the quick menu.\n\nSelector: [bindfastmenu(\"Bind name\" \"Folder/Subfolder\" \"Category\")]. Arguments: 1 — Bind name; 2 — Folder/Subfolder or \"\" for No folder; 3 — Category. @bind-62, number 31, and \"No.31 Bind name\" are also supported.\n\nRuns after the current line. If the bind is already in the quick menu, the action succeeds without changes.") \
    X(TagsBuiltinBindUnfastMenuDescription, "Убирает выбранный бинд из быстрого меню.\n\nSelector: [bindunfastmenu(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")]. Аргументы: 1 — Имя бинда; 2 — Папка/Подпапка или \"\" для «Без папки»; 3 — Категория. Также поддерживаются @bind-62, номер 31 и \"№31 Имя бинда\".\n\nРаботает после текущей строки. Если бинда уже нет в быстром меню, действие успешно завершается без изменений.", "Removes the selected bind from the quick menu.\n\nSelector: [bindunfastmenu(\"Bind name\" \"Folder/Subfolder\" \"Category\")]. Arguments: 1 — Bind name; 2 — Folder/Subfolder or \"\" for No folder; 3 — Category. @bind-62, number 31, and \"No.31 Bind name\" are also supported.\n\nRuns after the current line. If the bind is already absent from the quick menu, the action succeeds without changes.") \
    X(TagsBuiltinBindRandomDescription, "Запускает один случайный подходящий бинд после текущей строки.\n\nАргументы задают область, а не имя бинда:\n- [bindrandom] — только прямые бинды текущей папки;\n- [bindrandom(\"\")] — только уровень «Без папки» текущей категории;\n- [bindrandom(\"Папка/Подпапка\")] — только прямые бинды указанной папки;\n- [bindrandom(\"Папка/Подпапка/**\")] — указанная папка и все её подпапки;\n- [bindrandom(*)] — вся текущая категория;\n- [bindrandom(\"Папка/Подпапка/**\" \"Категория\")] — область другой категории.\n\nВыключенные, уже запущенные, ожидающие ввода/подтверждения и заблокированные условиями бинды исключаются. Если кандидатов нет, возвращается ошибка.", "Starts one random eligible bind after the current line.\n\nArguments select a scope, not a bind name:\n- [bindrandom] — direct binds in the current folder only;\n- [bindrandom(\"\")] — No folder in the current category only;\n- [bindrandom(\"Folder/Subfolder\")] — direct binds in that folder only;\n- [bindrandom(\"Folder/Subfolder/**\")] — that folder and all subfolders;\n- [bindrandom(*)] — the whole current category;\n- [bindrandom(\"Folder/Subfolder/**\" \"Category\")] — a scope in another category.\n\nDisabled, already running, input/confirmation-waiting, and condition-blocked binds are excluded. No eligible candidates produces an error.") \
    X(TagsBuiltinBindEndedDescription, "Проверяет состояние выбранного бинда: возвращает 1, если он завершён или не активен; 0, если выполняется, ждёт ввода/подтверждения или стоит на паузе.\n\nSelector: [bindended(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")]. Порядок: Имя бинда → Папка/Подпапка → Категория. Также поддерживаются @bind-62, номер 31, \"№31 Имя бинда\" и {thisbind}.\n\nЭто pure-проверка без запуска действий; её можно использовать в [ifandor(...)].", "Checks the selected bind state: returns 1 when it has ended or is inactive; returns 0 while it is running, waiting for input/confirmation, or paused.\n\nSelector: [bindended(\"Bind name\" \"Folder/Subfolder\" \"Category\")]. Order: Bind name → Folder/Subfolder → Category. @bind-62, number 31, \"No.31 Bind name\", and {thisbind} are also supported.\n\nThis is a pure check with no action side effects and can be used in [ifandor(...)].") \
    X(TagsBuiltinBindPopupDescription, "Открывает popup со строками выбранного бинда для ручной отправки. Сам бинд не запускается, текст автоматически не вставляется.\n\nSelector: [bindpopup(\"Имя бинда\" \"Папка/Подпапка\" \"Категория\")]. Аргументы: 1 — Имя бинда; 2 — Папка/Подпапка или \"\" для «Без папки»; 3 — Категория. Также поддерживаются @bind-62, номер 31 и \"№31 Имя бинда\".\n\nPopup открывается после текущей строки. Ошибка возвращается при отсутствующем или неоднозначном selector.", "Opens a popup with the selected bind lines for manual sending. The bind is not started and text is not inserted automatically.\n\nSelector: [bindpopup(\"Bind name\" \"Folder/Subfolder\" \"Category\")]. Arguments: 1 — Bind name; 2 — Folder/Subfolder or \"\" for No folder; 3 — Category. @bind-62, number 31, and \"No.31 Bind name\" are also supported.\n\nThe popup opens after the current line. Missing or ambiguous selectors produce an error.") \
    X(MiscVariablesTitle, "Переменные", "Variables") \
    X(TransliterationDictionaryTitle, "Словарь транслитерации", "Transliteration dictionary") \
    X(TransliterationDictionaryDescription, "Необязательные точные пары для [lattocyr(...)] и [cyrtolat(...)]. Формат строки: Collins|Коллинс. Совпадение выполняется по целым словам без учёта регистра.", "Optional exact pairs for [lattocyr(...)] and [cyrtolat(...)]. Line format: Collins|Коллинс. Matching is case-insensitive and applies to whole words.") \
    X(TransliterationDictionaryOpen, "Открыть файл", "Open file") \
    X(TransliterationDictionaryReload, "Перечитать", "Reload") \
    X(TransliterationDictionaryPathFormat, "Файл: %s", "File: %s") \
    X(TransliterationDictionaryMissing, "Файл отсутствует. Словарь не используется.", "The file is missing. The dictionary is not used.") \
    X(TransliterationDictionaryLoadedFormat, "Загружено пар: %llu.", "Loaded pairs: %llu.") \
    X(TransliterationDictionaryWarningsFormat, "Загружено пар: %llu; пропущено строк: %llu.", "Loaded pairs: %llu; skipped lines: %llu.") \
    X(TransliterationDictionaryLoadError, "Файл не загружен: ошибка чтения, кодировки или ограничения размера. Если словарь уже работал, его предыдущая версия сохранена.", "The file was not loaded due to a read, encoding, or size-limit error. If a dictionary was already active, its previous version was kept.") \
    X(TransliterationDictionaryOpenError, "Не удалось создать или открыть файл словаря.", "Failed to create or open the dictionary file.") \
    X(MiscVariablesCatalogTitle, "Список тегов", "Tag Catalog") \
    X(MiscVariablesCatalogEmpty, "По запросу ничего не найдено.", "No tags matched the query.") \
    X(MiscVariablesInspectorTitle, "Карточка тега", "Tag Card") \
    X(MiscVariablesDescriptionLabel, "Описание", "Description") \
    X(MiscVariablesExampleLabel, "Пример", "Example") \
    X(MiscVariablesParamcmdNote, "[paramcmd(...)] работает только если бинд был запущен именно командой. Аргументы делятся по пробелам, как в Lua-версии.", "[paramcmd(...)] only works when the bind was launched by a command. Arguments are split by spaces, matching the Lua version.") \
    X(MiscVariablesBindSelectorNote, "Порядок аргументов: 1 — Имя бинда, 2 — Папка/Подпапка, 3 — Категория. @bind-N — стабильный ID; N и \"№N Имя бинда\" — глобальные UI-номера; \"Имя бинда\" ищет во всей текущей категории; второй аргумент \"\" означает «Без папки», \"Папка/Подпапка\" — точный путь, \"*\" — любую папку указанной категории. Для bindrandom: * — вся категория, путь — только прямая папка, Папка/** — папка со всеми подпапками. Некорректные кавычки и лишние аргументы считаются ошибкой.", "Argument order: 1 — Bind name, 2 — Folder/Subfolder, 3 — Category. @bind-N is a stable ID; N and \"No.N Bind name\" are global UI numbers; \"Bind name\" searches the whole current category; the second argument \"\" means No folder, \"Folder/Subfolder\" is an exact path, and \"*\" means any folder in the selected category. For bindrandom: * is the whole category, a path is the direct folder only, and Folder/** includes the folder subtree. Invalid quotes and extra arguments are errors.") \
    X(MiscVariablesKeyEmulateNote, "[keyemulate(...)] выполняет одно нажатие клавиши во время выполнения bind и возвращает пустую строку.", "[keyemulate(...)] performs one key press while the bind is running and returns an empty string.") \
    X(MiscVariablesKeyPickerOpenHint, "Открыть список виртуальных клавиш и скопировать готовый [keyemulate(...)].", "Open the virtual-key list and copy a ready-made [keyemulate(...)].") \
    X(MiscVariablesKeyPickerTitle, "Подбор клавиши для [keyemulate(...)]", "Pick a key for [keyemulate(...)]") \
    X(MiscVariablesKeyPickerIntro, "Выберите виртуальную клавишу. Готовая переменная сразу скопируется в буфер обмена.", "Choose a virtual key. The ready-made variable is copied to the clipboard immediately.") \
    X(MiscVariablesKeyPickerSearchHint, "Поиск по коду или названию клавиши", "Search by key code or key name") \
    X(VariablesBuildBindTag, "Собрать bind-переменную", "Build bind variable") \
    X(MiscVariablesBindBuilderOpenHint, "Выбрать действие, категорию, папку/подпапку и имя бинда и получить готовую переменную.", "Choose an action, category, folder/subfolder, and bind name to generate a ready variable.") \
    X(BindBuilderTitle, "Конструктор bind-переменной", "Bind variable builder") \
    X(BindBuilderIntro, "Поля конструктора: Категория → Папка/Подпапка/Без папки → Имя бинда. В готовой строке аргументы записываются как Имя бинда → Папка/Подпапка → Категория. Stable ID переживает перенос и переименование.", "Builder fields: Category → Folder/Subfolder/No folder → Bind name. The generated arguments are written as Bind name → Folder/Subfolder → Category. A stable ID survives moves and renames.") \
    X(BindBuilderAction, "Действие", "Action") \
    X(BindBuilderCategory, "Категория", "Category") \
    X(BindBuilderFolder, "Папка / подпапка", "Folder / subfolder") \
    X(BindBuilderBind, "Имя бинда", "Bind name") \
    X(BindBuilderAnyFolder, "Любая папка", "Any folder") \
    X(BindBuilderNoFolder, "Без папки", "No folder") \
    X(BindBuilderSearch, "Поиск по имени бинда", "Search by bind name") \
    X(BindBuilderOutput, "Формат selector", "Selector format") \
    X(BindBuilderOutputStable, "Stable ID @bind-N", "Stable ID @bind-N") \
    X(BindBuilderOutputHuman, "Имя бинда + Папка/Подпапка + Категория", "Bind name + Folder/Subfolder + Category") \
    X(BindBuilderOutputNumber, "UI-номер №N (нестабильный)", "UI number No.N (volatile)") \
    X(BindBuilderScope, "Область bindrandom", "bindrandom scope") \
    X(BindBuilderScopeCategory, "Вся категория (*)", "Whole category (*)") \
    X(BindBuilderScopeRoot, "Только без папки", "No folder only") \
    X(BindBuilderScopeDirect, "Только выбранная папка", "Selected folder only") \
    X(BindBuilderScopeRecursive, "Папка и все подпапки (/**)", "Folder and all subfolders (/**)") \
    X(BindBuilderPreview, "Готовая переменная", "Generated variable") \
    X(BindBuilderEmpty, "В выбранной области нет биндов.", "No binds exist in the selected scope.") \
    X(MiscVariablesKeyPickerEmpty, "По этому фильтру клавиши не найдены.", "No keys matched this filter.") \
    X(MiscVariablesKeyPickerCopyHint, "Щелчок по строке копирует [keyemulate(code)] с уже подставленным кодом.", "Clicking a row copies [keyemulate(code)] with the selected code inserted.") \
    X(MiscVariablesDialogItemPickerOpenHint, "Открыть список доступных пунктов активного диалога и скопировать [dialogitem(...)].", "Open the active dialog item list and copy [dialogitem(...)].") \
    X(MiscVariablesDialogItemPickerTitle, "Подбор пункта для [dialogitem(...)]", "Pick an item for [dialogitem(...)]") \
    X(MiscVariablesDialogItemPickerIntro, "Показаны текущие пункты активного list/tablist диалога. Щелчок по строке копирует [dialogitem(N)] с нужным номером.", "Shows the current items of the active list/tablist dialog. Clicking a row copies [dialogitem(N)] with the selected number.") \
    X(MiscVariablesDialogItemPickerSearchHint, "Поиск по номеру или тексту пункта", "Search by item number or text") \
    X(MiscVariablesDialogItemPickerEmpty, "По этому фильтру пункты не найдены.", "No dialog items matched this filter.") \
    X(MiscVariablesDialogItemPickerNoDialog, "Нужен активный диалог SA:MP со списком.", "An active SA:MP list dialog is required.") \
    X(MiscVariablesDialogItemPickerNotList, "Активный диалог не является list/tablist.", "The active dialog is not a list/tablist dialog.") \
    X(MiscVariablesDialogItemPickerHeaderLabel, "Заголовок колонок: %s", "Header row: %s") \
    X(MiscVariablesDialogItemPickerCaptionLabel, "Заголовок диалога: %s", "Dialog caption: %s") \
    X(MiscVariablesDialogItemPickerCopyHint, "Щелчок по строке копирует [dialogitem(N)] для выбранного пункта.", "Clicking a row copies [dialogitem(N)] for the selected item.") \
    X(MiscVariablesArzDialogItemPickerOpenHint, "Открыть список пунктов текущего Arizona CEF диалога и скопировать [ARZdialogitem(...)].", "Open the current Arizona CEF dialog item list and copy [ARZdialogitem(...)].") \
    X(MiscVariablesArzDialogItemPickerTitle, "Подбор пункта для [ARZdialogitem(...)]", "Pick an item for [ARZdialogitem(...)]") \
    X(MiscVariablesArzDialogItemPickerIntro, "Показаны пункты текущего Arizona CEF диалога из live DOM или кэша RPC 61. Щелчок по строке копирует [ARZdialogitem(N)] с 1-based номером.", "Shows items from the current Arizona CEF dialog using live DOM or the RPC 61 cache. Clicking a row copies [ARZdialogitem(N)] with a 1-based number.") \
    X(MiscVariablesArzDialogItemPickerNoDialog, "Нужен активный или кэшированный Arizona CEF диалог со списком.", "An active or cached Arizona CEF list dialog is required.") \
    X(MiscVariablesArzDialogItemPickerCaptionLabel, "Заголовок Arizona CEF диалога: %s", "Arizona CEF dialog title: %s") \
    X(MiscVariablesArzDialogItemPickerCopyHint, "Щелчок по строке копирует [ARZdialogitem(N)] для выбранного пункта.", "Clicking a row copies [ARZdialogitem(N)] for the selected item.") \
    X(MiscVariablesDialogTextPickerOpenHint, "Открыть список токенов активного диалога и скопировать [dialogtext(...)].", "Open the active dialog token list and copy [dialogtext(...)].") \
    X(MiscVariablesDialogTextPickerTitle, "Подбор индекса для [dialogtext(...)]", "Pick an index for [dialogtext(...)]") \
    X(MiscVariablesDialogTextPickerIntro, "Показаны токены текста активного диалога. Щелчок по строке копирует [dialogtext(index)] с нужным индексом.", "Shows tokens from the active dialog text. Clicking a row copies [dialogtext(index)] with the selected index.") \
    X(MiscVariablesArzDialogTextPickerOpenHint, "Открыть список токенов последнего Arizona CEF диалога и скопировать [ARZdialoggetdialogtext(...)].", "Open the last Arizona CEF dialog token list and copy [ARZdialoggetdialogtext(...)].") \
    X(MiscVariablesArzDialogTextPickerTitle, "Подбор индекса для [ARZdialoggetdialogtext(...)]", "Pick an index for [ARZdialoggetdialogtext(...)]") \
    X(MiscVariablesArzDialogTextPickerIntro, "Показаны токены текста последнего Arizona CEF диалога из RPC 61. Щелчок по строке копирует [ARZdialoggetdialogtext(index)] с нужным индексом.", "Shows tokens from the last Arizona CEF dialog text from RPC 61. Clicking a row copies [ARZdialoggetdialogtext(index)] with the selected index.") \
    X(MiscVariablesArzDialogTextPickerNoDialog, "Нужен кэш последнего Arizona CEF диалога.", "A cached last Arizona CEF dialog is required.") \
    X(MiscVariablesArzDialogTextPickerCaptionLabel, "Заголовок Arizona CEF диалога: %s", "Arizona CEF dialog title: %s") \
    X(MiscVariablesArzDialogTextPickerCopyHint, "Щелчок по строке копирует [ARZdialoggetdialogtext(index)] с выбранным индексом.", "Clicking a row copies [ARZdialoggetdialogtext(index)] with the selected index.") \
    X(MiscVariablesDialogTextPickerSearchHint, "Поиск по индексу или тексту токена", "Search by token index or token text") \
    X(MiscVariablesDialogTextPickerEmpty, "По этому фильтру токены не найдены.", "No dialog tokens matched this filter.") \
    X(MiscVariablesDialogTextPickerNoDialog, "Нужен активный диалог SA:MP.", "An active SA:MP dialog is required.") \
    X(MiscVariablesDialogTextPickerCaptionLabel, "Заголовок диалога: %s", "Dialog caption: %s") \
    X(MiscVariablesDialogTextPickerCountLabel, "Всего токенов: %s", "Total tokens: %s") \
    X(MiscVariablesDialogTextPickerCopyHint, "Щелчок по строке копирует [dialogtext(index)] с выбранным индексом.", "Clicking a row copies [dialogtext(index)] with the selected index.") \
    X(VariablesSearchHint, "Поиск по токену, описанию или категории", "Search by token, description, or category") \
    X(VariablesNoMatches, "По фильтрам ничего не найдено.", "No variables matched the filters.") \
    X(VariablesInspectorEmpty, "Выберите переменную слева.", "Select a variable on the left.") \
    X(VariablesVisibleCountFormat, "Показано %s из %s", "Showing %s of %s") \
    X(VariablesCategoryAll, "Все", "All") \
    X(VariablesCategoryPlayer, "Игрок", "Player") \
    X(VariablesCategoryTarget, "Цель", "Target") \
    X(VariablesCategoryVehicle, "Транспорт", "Vehicle") \
    X(VariablesCategoryWorld, "Мир/позиция", "World/position") \
    X(VariablesCategoryTime, "Время", "Time") \
    X(VariablesCategorySampDialog, "Диалог SA:MP", "SA:MP dialog") \
    X(VariablesCategoryArizona, "Arizona CEF", "Arizona CEF") \
    X(VariablesCategoryBinder, "Бинд", "Bind") \
    X(VariablesCategoryText, "Текст/математика", "Text/math") \
    X(VariablesCategoryActions, "Действия", "Actions") \
    X(VariablesCategoryCustom, "Пользовательские", "Custom") \
    X(VariablesCategoryParameters, "Параметры бинда", "Bind parameters") \
    X(VariablesBadgeSimple, "Простая", "Simple") \
    X(VariablesBadgeFunction, "Функция", "Function") \
    X(VariablesBadgeCustom, "Custom", "Custom") \
    X(VariablesBadgeParameter, "Параметр", "Parameter") \
    X(VariablesBadgeAction, "Действие", "Action") \
    X(VariablesInsert, "Вставить", "Insert") \
    X(VariablesCopy, "Скопировать", "Copy") \
    X(VariablesInsertExample, "Вставить пример", "Insert example") \
    X(VariablesInsertTemplate, "Вставить шаблон", "Insert template") \
    X(VariablesCopyExample, "Копировать пример", "Copy example") \
    X(VariablesCopyTemplate, "Копировать шаблон", "Copy template") \
    X(VariablesPickKey, "Подобрать клавишу", "Pick key") \
    X(VariablesPickDialogItem, "Подобрать пункт", "Pick item") \
    X(VariablesPickDialogIndex, "Подобрать индекс", "Pick index") \
    X(VariablesNoInsertTarget, "Сначала выберите текстовую строку сценария или поле нового шага.", "Select a scenario text row or the new-step field first.") \
    X(VariablesNewCustom, "Переменная", "Variable") \
    X(VariablesCustomCreateTitle, "Новая пользовательская переменная", "New custom variable") \
    X(VariablesCustomEditTitle, "Редактирование пользовательской переменной", "Edit custom variable") \
    X(VariablesCustomNameHint, "Имя: латиница, цифры и _, 1-64 символа. Использование: {name}.", "Name: Latin letters, digits, and _, 1-64 characters. Usage: {name}.") \
    X(VariablesCustomValue, "Значение", "Value") \
    X(VariablesCustomReadOnlyHint, "Редактирование пользовательских переменных доступно во вкладке Прочее.", "Custom variables can be edited from the Misc tab.") \
    X(VariablesDeleteCustomQuestion, "Удалить пользовательскую переменную \"%s\"?", "Delete custom variable \"%s\"?") \
    X(VariablesCustomErrorEmptyName, "Введите имя переменной.", "Enter a variable name.") \
    X(VariablesCustomErrorBadName, "Имя может содержать только A-Z, a-z, 0-9 и _. Длина: 1-64.", "The name may contain only A-Z, a-z, 0-9, and _. Length: 1-64.") \
    X(VariablesCustomErrorBuiltinConflict, "Имя конфликтует со встроенной переменной.", "The name conflicts with a built-in variable.") \
    X(VariablesCustomErrorDuplicate, "Пользовательская переменная с таким именем уже есть.", "A custom variable with this name already exists.") \
    X(VariablesTabCatalog, "Каталог", "Catalog") \
    X(VariablesTabLua, "Lua", "Lua") \
    X(VariablesCodeTitle, "Переменные из Lua", "Lua variables") \
    X(VariablesCodeDescription, "Подключайте Lua-файлы из общей папки. Каждый новый provider выключен, пока вы явно не включите его.", "Load Lua files from the shared folder. Every new provider stays disabled until you explicitly enable it.") \
    X(VariablesCodeTrustedWarning, "Lua запускается MoonLoader внутри процесса игры. Включайте только доверенные файлы.", "Lua runs through MoonLoader inside the game process. Enable trusted files only.") \
    X(VariablesCodeRuntimeTitle, "Среда выполнения", "Runtime") \
    X(VariablesCodeActionsTitle, "Действия", "Actions") \
    X(VariablesCodeBackendLabel, "Backend", "Backend") \
    X(VariablesCodeHostLabel, "MoonLoader Host", "MoonLoader Host") \
    X(VariablesCodePathFormat, "Папка: %s", "Folder: %s") \
    X(VariablesCodeOpenFolder, "Открыть папку", "Open folder") \
    X(VariablesCodeReload, "Перезагрузить Lua-файлы", "Reload Lua files") \
    X(VariablesCodeInstallHost, "Установить Host", "Install Host") \
    X(VariablesCodeUpdateHost, "Обновить Host", "Update Host") \
    X(VariablesCodeHostInstalled, "HelperByOrcVarsHost.lua установлен. Перезапустите игру или перезагрузите скрипты MoonLoader.", "HelperByOrcVarsHost.lua was installed. Restart the game or reload MoonLoader scripts.") \
    X(VariablesCodeHostInstallFailed, "Не удалось установить Host: %s", "Failed to install Host: %s") \
    X(VariablesCodeHostUnavailable, "MoonLoader не обнаружен: Lua providers недоступны.", "MoonLoader was not detected: Lua providers are unavailable.") \
    X(VariablesCodeHostOutdated, "Найден другой или устаревший HelperByOrcVarsHost.lua; перед обновлением будет создан .bak.", "A different or outdated HelperByOrcVarsHost.lua was found; a .bak will be created before updating.") \
    X(VariablesCodeHostStateUnavailable, "недоступен", "unavailable") \
    X(VariablesCodeHostStateMissing, "не установлен", "not installed") \
    X(VariablesCodeHostStateCurrent, "актуален", "current") \
    X(VariablesCodeHostStateOutdated, "нужно обновить", "update required") \
    X(VariablesCodeProvidersTitle, "Файлы-провайдеры", "Provider files") \
    X(VariablesCodeProvidersSummary, "Файлов: %s · включено: %s · переменных: %s", "Files: %s · enabled: %s · variables: %s") \
    X(VariablesCodeEmpty, "В GTA San Andreas User Files\\HelperByOrc\\vars и подпапках нет Lua-файлов.", "There are no Lua files in GTA San Andreas User Files\\HelperByOrc\\vars or its subfolders.") \
    X(VariablesCodeProviderEnable, "Включить", "Enable") \
    X(VariablesCodeProviderEnabled, "Включён", "Enabled") \
    X(VariablesCodeProviderVariablesFormat, "переменных: %s", "variables: %s") \
    X(VariablesCodeStateDisabled, "выключен", "disabled") \
    X(VariablesCodeStateWaiting, "ожидает MoonLoader Host", "waiting for MoonLoader Host") \
    X(VariablesCodeStateLoading, "загрузка", "loading") \
    X(VariablesCodeStateReady, "готов", "ready") \
    X(VariablesCodeStateConflict, "конфликт", "conflict") \
    X(VariablesCodeStateFaulted, "отключён после ошибки", "disabled after error") \
    X(VariablesCustomErrorCodeConflict, "Имя конфликтует с переменной из Lua provider.", "The name conflicts with a Lua provider variable.") \
    X(NotepadIntro, "Профильный блокнот с папками, разметкой, локальными картинками и live preview.", "Profile-aware notepad with folders, markup, local images, and live preview.") \
    X(NotepadRootName, "Блокнот", "Notepad") \
    X(NotepadNewNote, "Заметка", "Note") \
    X(NotepadNewFolder, "Папка", "Folder") \
    X(NotepadDefaultFolder, "Новая папка", "New folder") \
    X(NotepadUntitled, "Без названия", "Untitled") \
    X(NotepadCopySuffix, " копия", " copy") \
    X(NotepadSearchHint, "Поиск заметок", "Search notes") \
    X(NotepadSearchResults, "Найдено:", "Found:") \
    X(NotepadFavorites, "Избранное", "Favorites") \
    X(NotepadEmptyFolder, "В этой папке пока пусто.", "This folder is empty.") \
    X(NotepadEmptySearch, "По запросу ничего не найдено.", "No notes matched the query.") \
    X(NotepadNoSelection, "Выберите заметку слева или создайте новую.", "Select a note on the left or create a new one.") \
    X(NotepadEditor, "Редактор", "Editor") \
    X(NotepadLivePreview, "Предпросмотр", "Preview") \
    X(NotepadFavorite, "В избранное", "Add to favorites") \
    X(NotepadUnfavorite, "Убрать из избранного", "Remove from favorites") \
    X(NotepadOpenFolder, "Открыть папку", "Open folder") \
    X(NotepadImportTxt, "Импорт .txt", "Import .txt") \
    X(NotepadExportTxt, "Экспорт .txt", "Export .txt") \
    X(NotepadTxtRefresh, "Пересканировать .txt", "Rescan .txt") \
    X(NotepadTxtSourceFormat, "Живой TXT: %s", "Live TXT: %s") \
    X(NotepadTxtMissing, "Исходный файл удалён. Заметка и её ID сохранены.", "The source file was deleted. The note and its ID are preserved.") \
    X(NotepadTxtUnavailableFormat, "Файл временно недоступен (ошибка %lu).", "The file is temporarily unavailable (error %lu).") \
    X(NotepadTxtConflict, "Файл изменён снаружи во время редактирования. Выберите версию.", "The file changed externally while editing. Choose a version.") \
    X(NotepadTxtUseFile, "Использовать файл", "Use file") \
    X(NotepadTxtAcceptDelete, "Принять удаление", "Accept deletion") \
    X(NotepadTxtOverwriteFile, "Записать версию заметки", "Write note version") \
    X(NotepadTxtOperationPending, "Файловая операция выполняется...", "File operation is running...") \
    X(NotepadTxtOperationFailedFormat, "Операция с TXT не выполнена (ошибка %lu).", "TXT operation failed (error %lu).") \
    X(NotepadCopyRaw, "Копировать raw", "Copy raw") \
    X(NotepadCopyRendered, "Копировать текст", "Copy text") \
    X(NotepadCopyLine, "Копировать строки", "Copy lines") \
    X(NotepadPreviewMode, "Предпросмотр", "Preview") \
    X(NotepadInsertImage, "Картинка", "Image") \
    X(NotepadApplyTags, "Подставлять теги", "Expand tags") \
    X(NotepadMarkupHelp, "Разметка", "Markup") \
    X(NotepadMarkupHelpTitle, "Поддерживаемая разметка", "Supported markup") \
    X(NotepadMarkupHelpHint, "Картинки ищутся только в папке профиля notepad/images. Абсолютные пути и .. запрещены.", "Images are resolved only from the profile notepad/images folder. Absolute paths and .. are blocked.") \
    X(NotepadModalTitle, "Блокнот", "Notepad") \
    X(NotepadCreateFolderTitle, "Новая папка", "New folder") \
    X(NotepadCreateNoteTitle, "Новая заметка", "New note") \
    X(NotepadRenameTitle, "Переименование", "Rename") \
    X(NotepadDeleteNoteTitle, "Удаление заметки", "Delete note") \
    X(NotepadDeleteFolderTitle, "Удаление папки", "Delete folder") \
    X(NotepadFolderName, "Имя папки", "Folder name") \
    X(NotepadNoteTitle, "Название заметки", "Note title") \
    X(NotepadDeleteNoteQuestionFormat, "Удалить заметку \"%s\"?", "Delete note \"%s\"?") \
    X(NotepadDeleteFolderQuestionFormat, "Удалить папку \"%s\"? Внутри папок: %d, заметок: %d.", "Delete folder \"%s\"? Nested folders: %d, notes: %d.") \
    X(NotepadConfirmDelete, "Удалить", "Delete") \
    X(NotepadFolderExists, "Папка с таким именем уже есть.", "A folder with this name already exists.") \
    X(NotepadInvalidName, "Некорректное имя.", "Invalid name.") \
    X(NotepadImportFailed, "Не удалось импортировать txt.", "Failed to import txt.") \
    X(NotepadExportFailed, "Не удалось экспортировать заметку.", "Failed to export note.") \
    X(NotepadExportSuccessFormat, "Экспортировано: %s", "Exported: %s") \
    X(NotepadImageCopied, "Картинка скопирована в профиль и вставлена в заметку.", "Image was copied into the profile and inserted into the note.") \
    X(NotepadImageInsertFailed, "Не удалось вставить картинку.", "Failed to insert image.") \
    X(NotepadInvalidImagePath, "Некорректный путь к картинке.", "Invalid image path.") \
    X(NotepadMissingImageFormat, "Картинка не найдена: %s", "Image not found: %s") \
    X(NotepadTxtFilesFilter, "Текстовые файлы (*.txt)", "Text files (*.txt)") \
    X(NotepadImageFilesFilter, "Картинки (*.png;*.jpg;*.jpeg;*.bmp;*.gif)", "Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif)") \
    X(NotepadAllFilesFilter, "Все файлы (*.*)", "All files (*.*)") \
    X(SettingsGtaVersion, "Версия GTA", "GTA version") \
    X(SettingsSectionGeneral, "Общее", "General") \
    X(SettingsSectionBinder, "Биндер", "Binder") \
    X(SettingsSectionHotkeys, "Горячие клавиши", "Hotkeys") \
    X(SettingsSectionProfiles, "Профили", "Profiles") \
    X(SettingsSectionQuickMenu, "Быстрое меню", "Quick menu") \
    X(SettingsSectionNotifications, "Уведомления", "Notifications") \
    X(SettingsSectionDiagnostics, "Диагностика", "Diagnostics") \
    X(SettingsGeneralIntro, "Язык, масштаб интерфейса и внешние интеграции.", "Language, interface scaling, and external integrations.") \
    X(SettingsBinderIntro, "Общие параметры выполнения биндов.", "General bind runtime settings.") \
    X(SettingsBinderListStyle, "Стиль списка биндов", "Bind list style") \
    X(SettingsBinderListStyleExplorer, "Проводник", "Explorer") \
    X(SettingsBinderListStyleTwoPane, "Две панели", "Two-pane") \
    X(SettingsBinderTextConfirmationTimeoutSec, "Таймер подтверждения триггера, сек", "Trigger confirmation timeout, sec") \
    X(SettingsBinderTextConfirmationTimeoutHint, "Если бинд ждёт подтверждение по текстовому триггеру дольше этого времени, ожидание сбрасывается и бинд не запускается. Диапазон: 5-600 секунд.", "If a bind waits for text-trigger confirmation longer than this value, the pending confirmation is cleared and the bind is not started. Range: 5-600 seconds.") \
    X(SettingsHotkeysIntro, "Комбинации для открытия основных окон HelperByOrc.", "Shortcuts for opening HelperByOrc windows.") \
    X(SettingsHotkeysHelpTitle, "Справка по клавишам", "Key reference") \
    X(SettingsHotkeysHelpBindList, "Список биндов", "Bind list") \
    X(SettingsHotkeysHelpInputDialog, "Окно заполнения параметров", "Parameter input window") \
    X(SettingsHotkeysHelpCapture, "Захват комбинации", "Combination capture") \
    X(SettingsHotkeysHelpNotepad, "Блокнот", "Notepad") \
    X(SettingsHotkeysHelpHud, "HUD", "HUD") \
    X(SettingsHotkeysActionClearSearch, "Очистить поиск.", "Clear search.") \
    X(SettingsHotkeysActionMoveSelection, "Двигать выделение в активной панели.", "Move selection in the active pane.") \
    X(SettingsHotkeysActionOpenSelected, "Открыть выбранный бинд или папку.", "Open the selected bind or folder.") \
    X(SettingsHotkeysActionNavigateUp, "Перейти на папку выше.", "Go to the parent folder.") \
    X(SettingsHotkeysActionDeleteSelected, "Удалить выбранный бинд или папку активной панели.", "Delete the selected bind or folder in the active pane.") \
    X(SettingsHotkeysActionRenameFolder, "Переименовать выбранную папку активной панели.", "Rename the selected folder in the active pane.") \
    X(SettingsHotkeysActionInlineFolderSaveCancel, "Сохранить или отменить имя папки.", "Save or cancel the folder name.") \
    X(SettingsHotkeysActionFocusSearch, "Фокус в поиск.", "Focus search.") \
    X(SettingsHotkeysActionLaunchInputDialog, "Запустить с введёнными параметрами.", "Launch with the entered parameters.") \
    X(SettingsHotkeysActionCancelInputDialog, "Отменить ввод параметров.", "Cancel parameter input.") \
    X(SettingsHotkeysActionSaveCapture, "Сохранить комбинацию.", "Save the combination.") \
    X(SettingsHotkeysActionClearCapture, "Очистить комбинацию.", "Clear the combination.") \
    X(SettingsHotkeysActionCancelCapture, "Отменить захват.", "Cancel capture.") \
    X(SettingsHotkeysActionCaptureMouse, "Кнопки мыши захватываются через кнопку \"Мышь\".", "Mouse buttons are captured through the \"Mouse\" button.") \
    X(SettingsHotkeysActionNotepadSaveEdit, "Сохранить заметку и выйти из редактирования.", "Save the note and leave editing.") \
    X(SettingsHotkeysActionNotepadDelete, "Удалить выбранную заметку или папку.", "Delete the selected note or folder.") \
    X(SettingsHotkeysActionNotepadRename, "Переименовать выбранную заметку или папку.", "Rename the selected note or folder.") \
    X(SettingsHotkeysActionNotepadCancelEdit, "Отменить активное редактирование.", "Cancel active editing.") \
    X(SettingsHotkeysActionNotepadOpenFolder, "Открыть выбранную папку.", "Open the selected folder.") \
    X(SettingsHotkeysActionNotepadModalSaveCancel, "Сохранить или закрыть модальное окно.", "Save or close the modal window.") \
    X(SettingsHotkeysActionHudExitEdit, "Выйти из размещения или inline-редактирования текста.", "Leave placement or inline text editing.") \
    X(SettingsQuickMenuIntro, "Как открывается быстрое меню биндов.", "How the binder quick menu opens.") \
    X(SettingsDiagnosticsIntro, "Пути, журнал и runtime-состояние для поиска проблем.", "Paths, log, and runtime state for troubleshooting.") \
    X(SettingsNotificationsEnabled, "Включить системные уведомления", "Enable system notifications") \
    X(SettingsNotificationsChannel, "Канал", "Channel") \
    X(SettingsNotificationsChannelPopup, "Окно", "Popup") \
    X(SettingsNotificationsChannelLog, "Лог", "Log") \
    X(SettingsNotificationsGroups, "Группы уведомлений", "Notification groups") \
    X(SettingsNotificationsGroupBinderErrors, "Ошибки биндов и отправки", "Bind and send errors") \
    X(SettingsNotificationsGroupTagErrors, "Ошибки тегов", "Tag errors") \
    X(SettingsNotificationsGroupSampDialogErrors, "Ошибки диалогов SA:MP", "SA:MP dialog errors") \
    X(SettingsNotificationsGroupSuccess, "Успешные действия", "Successful actions") \
    X(SettingsNotificationsGroupConfirmation, "Подтверждения и отмены", "Confirmations and cancels") \
    X(SettingsNotificationsGroupBinderEvents, "События", "Events") \
    X(SettingsNotificationsPosition, "Позиция", "Position") \
    X(SettingsNotificationsPositionTopLeft, "Сверху слева", "Top left") \
    X(SettingsNotificationsPositionTopCenter, "Сверху по центру", "Top center") \
    X(SettingsNotificationsPositionTopRight, "Сверху справа", "Top right") \
    X(SettingsNotificationsPositionMiddleLeft, "По центру слева", "Middle left") \
    X(SettingsNotificationsPositionMiddleCenter, "По центру", "Middle center") \
    X(SettingsNotificationsPositionMiddleRight, "По центру справа", "Middle right") \
    X(SettingsNotificationsPositionBottomLeft, "Снизу слева", "Bottom left") \
    X(SettingsNotificationsPositionBottomCenter, "Снизу по центру", "Bottom center") \
    X(SettingsNotificationsPositionBottomRight, "Снизу справа", "Bottom right") \
    X(SettingsNotificationsOffsetX, "Отступ X", "Offset X") \
    X(SettingsNotificationsOffsetY, "Отступ Y", "Offset Y") \
    X(SettingsNotificationsDisplay, "Отображение", "Display") \
    X(SettingsNotificationsDurationMs, "Время показа, мс", "Duration, ms") \
    X(SettingsNotificationsWidth, "Ширина окна", "Popup width") \
    X(SettingsNotificationsOpacity, "Прозрачность", "Opacity") \
    X(SettingsNotificationsAntiFlood, "Антифлуд", "Anti-flood") \
    X(SettingsNotificationsDedupeMs, "Схлопывать повторы, мс", "Collapse repeats, ms") \
    X(SettingsNotificationsMaxVisible, "Максимум на экране", "Max visible") \
    X(SettingsNotificationsMaxQueue, "Максимум в очереди", "Max queued") \
    X(SettingsNotificationsTest, "Тестовое уведомление", "Test notification") \
    X(SettingsNotificationsTestText, "Тестовое уведомление HelperByOrc.", "HelperByOrc test notification.") \
    X(SettingsSummaryProfile, "Профиль", "Profile") \
    X(SettingsSummaryLanguage, "Язык", "Language") \
    X(SettingsSummaryMainWindow, "Главное окно", "Main window") \
    X(SettingsSummaryQuickMenu, "Быстрое меню", "Quick menu") \
    X(SettingsLanguage, "Язык", "Language") \
    X(SettingsUiScale, "Масштаб интерфейса", "Interface Scale") \
    X(SettingsAutoScale, "Автомасштаб под разрешение", "Auto scale for resolution") \
    X(SettingsScaleMultiplier, "Пользовательский множитель", "Custom multiplier") \
    X(SettingsEffectiveScale, "Итоговый масштаб", "Effective scale") \
    X(SettingsUiFont, "Шрифт интерфейса", "Interface font") \
    X(SettingsFontFamily, "Семейство", "Family") \
    X(SettingsFontSize, "Размер", "Size") \
    X(SettingsExternalScripts, "Внешние скрипты", "External scripts") \
    X(SettingsExpandExternalTags, "Обрабатывать теги во внешних вызовах", "Process tags in external calls") \
    X(SettingsExpandExternalTagsDesc, "Раскрывает теги HelperByOrc в sampSendChat и sampAddChatMessage, вызванных MoonLoader, SAMPFUNCS и другими ASI. Сообщения сервера и prefix не изменяются.", "Expands HelperByOrc tags in sampSendChat and sampAddChatMessage calls made by MoonLoader, SAMPFUNCS, and other ASI plugins. Server messages and prefixes remain unchanged.") \
    X(SettingsResetDefaults, "Сбросить интерфейс", "Reset interface") \
    X(SettingsConfigPath, "Файл конфига", "Config file") \
    X(SettingsLogPath, "Файл журнала", "Log file") \
    X(SettingsLogState, "Состояние журнала", "Log state") \
    X(SettingsLogStateFormat, "handle: %s; файл: %s; последняя ошибка: %lu (%s); записи: %llu; открытия: %llu; восстановления: %llu", "handle: %s; file: %s; last error: %lu (%s); writes: %llu; opens: %llu; recoveries: %llu") \
    X(SettingsLogHandleOpen, "открыт", "open") \
    X(SettingsLogHandleClosed, "закрыт", "closed") \
    X(SettingsLogFileExists, "существует", "exists") \
    X(SettingsLogFileMissing, "отсутствует", "missing") \
    X(SettingsLogRetryOpen, "Повторить открытие журнала", "Retry opening log") \
    X(SettingsLogLevel, "Уровень логирования", "Log level") \
    X(SettingsLogLevelOff, "Off", "Off") \
    X(SettingsLogLevelError, "Error", "Error") \
    X(SettingsLogLevelInfo, "Info", "Info") \
    X(SettingsApplyDamageProtection, "Защита деталей транспорта", "Vehicle parts protection") \
    X(SettingsApplyDamageProtectionDesc, "Не даёт деталям транспорта отваливаться после урона.", "Prevents vehicle parts from falling off after damage.") \
    X(SettingsScaleHint, "Автомасштаб берёт за основу 1920x1080 и подстраивает UI под текущее разрешение. Его можно отключить или скорректировать множителем.", "Auto scale uses 1920x1080 as the reference and adapts the UI to the current resolution. You can disable it or fine-tune it with the multiplier.") \
    X(SettingsMainWindowHotkey, "Хоткей открытия главного окна", "Main window hotkey") \
    X(SettingsMainWindowHotkeyHelp, "Комбинация открывает и закрывает главное окно HelperByOrc.", "This shortcut opens and closes the main HelperByOrc window.") \
    X(SettingsResetHotkey, "Сбросить", "Reset") \
    X(SettingsProfilesSection, "Профили", "Profiles") \
    X(SettingsProfilesIntro, "Каждый профиль хранит отдельный HelperByOrc.json. Переключение сразу перезагружает настройки UI, биндер и переменные.", "Each profile stores a separate HelperByOrc.json. Switching reloads UI settings, binder data, and variables immediately.") \
    X(SettingsActiveProfile, "Активный профиль", "Active profile") \
    X(SettingsProfileName, "Название профиля", "Profile name") \
    X(SettingsProfileCreateEmpty, "Создать пустой", "Create empty") \
    X(SettingsProfileDuplicate, "Дублировать текущий", "Duplicate current") \
    X(SettingsProfileRename, "Переименовать", "Rename") \
    X(SettingsProfileDelete, "Удалить профиль", "Delete profile") \
    X(SettingsProfileDeleteTitle, "Удаление профиля", "Delete profile") \
    X(SettingsProfileDeleteQuestionFormat, "Удалить профиль \"%s\" вместе с его HelperByOrc.json?", "Delete profile \"%s\" together with its HelperByOrc.json?") \
    X(SettingsProfileCannotDeleteLast, "Нельзя удалить последний профиль.", "The last profile cannot be deleted.") \
    X(SettingsProfileNameRequired, "Введите название профиля.", "Enter a profile name.") \
    X(SettingsProfileOperationFailed, "Не удалось выполнить действие с профилем. Подробности записаны в лог.", "Failed to complete the profile action. Details were written to the log.") \
    X(SettingsProfilesPath, "Папка профилей", "Profiles folder") \
    X(SettingsProfilesRegistryPath, "Реестр профилей", "Profiles registry") \
    X(SettingsOpenProfilesFolder, "Открыть папку профилей", "Open profiles folder") \
    X(SettingsOpenConfigFile, "Открыть конфиг", "Open config") \
    X(SettingsOpenRegistryFile, "Открыть реестр", "Open registry") \
    X(SettingsOpenLogFile, "Открыть журнал", "Open log") \
    X(SettingsCopyPath, "Копировать путь", "Copy path") \
    X(SettingsSampStatus, "SA:MP", "SA:MP") \
    X(SettingsSampVersion, "Версия SA:MP", "SA:MP version") \
    X(SettingsSampWaiting, "ожидание samp.dll", "waiting for samp.dll") \
    X(SettingsSampLoaded, "загружен", "loaded") \
    X(SettingsSampReady, "готов", "ready") \
    X(SettingsSampUnsupported, "неподдерживаемая версия", "unsupported version") \
    X(SettingsChatAsiStatus, "_chat.asi", "_chat.asi") \
    X(SettingsChatAsiLoaded, "загружен", "loaded") \
    X(SettingsChatAsiNotLoaded, "не загружен", "not loaded") \
    X(SettingsFallbackStatus, "SA:MP fallback", "SA:MP fallback") \
    X(SettingsFallbackAvailable, "доступен при готовом SA:MP", "available when SA:MP is ready") \
    X(SettingsHooksStatus, "SA:MP hooks", "SA:MP hooks") \
    X(SettingsRakHooksStatus, "RakNet hooks", "RakNet hooks") \
    X(MiscGameFixesTitle, "Игровые исправления", "Game fixes") \
    X(MiscGameFixesDesc, "Небольшие исправления поведения игры, которые работают без отдельной настройки биндов.", "Small game behavior fixes that do not require binder setup.") \
    X(HotkeyConflictFormat, "Комбинация конфликтует с %s.", "This combination conflicts with %s.") \
    X(HotkeyConflictMainWindowFormat, "хоткеем открытия главного окна (%s)", "the main window hotkey (%s)") \
    X(HotkeyConflictQuickMenuFormat, "хоткеем быстрого меню (%s)", "the quick menu hotkey (%s)") \
    X(HotkeyConflictBindFormat, "биндом \"%s\" (%s)", "bind \"%s\" (%s)") \
    X(BinderDefaultRootFolder, "Основные", "Main") \
    X(BinderDefaultHotkey, "Новый бинд", "New bind") \
    X(BinderDefaultFolder, "Папка", "Folder") \
    X(BinderNewCategory, "Новая категория", "New category") \
    X(BinderNewFolder, "Новая папка", "New folder") \
    X(ConditionInWater, "В воде", "In water") \
    X(ConditionDead, "Мёртв", "Dead") \
    X(ConditionInAir, "В воздухе", "In air") \
    X(ConditionInAnyCar, "В любом транспорте", "In any vehicle") \
    X(ConditionWithoutWeapon, "Без оружия", "Without weapon") \
    X(ConditionWithWeapon, "С оружием", "With weapon") \
    X(ConditionOnFoot, "Пешком", "On foot") \
    X(ConditionChatOpened, "Открыт чат", "Chat open") \
    X(ConditionDialogOpened, "Открыт диалог", "Dialog open") \
    X(ConditionScoreboardOpen, "Таб открыт", "Scoreboard open") \
    X(ConditionScoreboardClosed, "Таб закрыт", "Scoreboard closed") \
    X(ConditionChatVisible, "Чат виден", "Chat visible") \
    X(ConditionChatHidden, "Чат скрыт", "Chat hidden") \
    X(ConditionSampCursorActive, "Активный курсор SA:MP", "SA:MP cursor active") \
    X(ConditionWindowsCursorActive, "Активный курсор Windows", "Windows cursor active") \
    X(ConditionHelperCursorBlocked, "окно Helper открыто", "Helper window is open") \
    X(ConditionGameHudVisible, "HUD игры виден", "Game HUD visible") \
    X(ConditionGameHudHidden, "HUD игры не виден", "Game HUD hidden") \
    X(ConditionCameraLookingAtPlayer, "Камера смотрит на игрока", "Camera looking at player") \
    X(ConditionCameraNotLookingAtPlayer, "Камера не смотрит на игрока", "Camera not looking at player") \
    X(ConditionAnyExplosionActive, "Есть активный взрыв", "Active explosion") \
    X(ConditionNoActiveExplosion, "Нет активного взрыва", "No active explosion") \
    X(ConditionServerConnected, "Подключён к серверу", "Connected to server") \
    X(ConditionServerDisconnected, "Не подключён к серверу", "Disconnected from server") \
    X(ConditionDriver, "За рулём", "Driver") \
    X(ConditionPassenger, "Пассажир", "Passenger") \
    X(ConditionPassengerDriveByOn, "Целится с места пассажира", "Aiming as passenger") \
    X(ConditionPassengerDriveByOff, "Не целится с места пассажира", "Not aiming as passenger") \
    X(ConditionVehicleSirenOn, "Сирена включена", "Siren on") \
    X(ConditionVehicleSirenOff, "Сирена выключена", "Siren off") \
    X(ConditionVehicleEngineOn, "Двигатель заведён", "Engine on") \
    X(ConditionVehicleEngineOff, "Двигатель заглушён", "Engine off") \
    X(ConditionVehicleLightsOn, "Фары включены", "Headlights on") \
    X(ConditionVehicleLightsOff, "Фары выключены", "Headlights off") \
    X(ConditionGtaMenuOpen, "GTA меню открыто", "GTA menu open") \
    X(ConditionGtaMenuClosed, "GTA меню закрыто", "GTA menu closed") \
    X(ConditionSampCursorInactive, "Неактивный курсор SA:MP", "SA:MP cursor inactive") \
    X(ConditionWindowsCursorInactive, "Неактивный курсор Windows", "Windows cursor inactive") \
    X(ConditionChatClosed, "Закрыт чат", "Chat closed") \
    X(ConditionDialogClosed, "Закрыт диалог", "Dialog closed") \
    X(ConditionVehicleBoat, "Лодка", "Boat") \
    X(ConditionVehicleCar, "Машина", "Car") \
    X(ConditionVehicleTrain, "Поезд", "Train") \
    X(ConditionVehicleHeli, "Вертолёт", "Helicopter") \
    X(ConditionVehiclePlane, "Самолёт", "Plane") \
    X(ConditionVehicleBike, "Мотоцикл", "Motorcycle") \
    X(ConditionVehicleFakePlane, "Псевдосамолёт", "Fake plane") \
    X(ConditionVehicleMonsterTruck, "Монстр-трак", "Monster truck") \
    X(ConditionVehicleQuadBike, "Квадроцикл", "Quad bike") \
    X(ConditionVehicleBicycle, "Велосипед", "Bicycle") \
    X(ConditionNotVehicleBoat, "Не лодка", "Not boat") \
    X(ConditionNotVehicleTrain, "Не поезд", "Not train") \
    X(ConditionNotVehiclePlane, "Не самолёт", "Not plane") \
    X(ConditionNotVehicleFakePlane, "Не псевдосамолёт", "Not fake plane") \
    X(ConditionNotVehicleCar, "Не машина", "Not car") \
    X(ConditionNotVehicleHeli, "Не вертолёт", "Not helicopter") \
    X(ConditionNotVehicleBike, "Не мотоцикл", "Not motorcycle") \
    X(ConditionNotVehicleMonsterTruck, "Не монстр-трак", "Not monster truck") \
    X(ConditionNotVehicleBicycle, "Не велосипед", "Not bicycle") \
    X(ConditionNotVehicleQuadBike, "Не квадроцикл", "Not quad bike") \
    X(ConditionNotInWater, "Не в воде", "Not in water") \
    X(ConditionNotInAir, "Не в воздухе", "Not in air") \
    X(ConditionDeprecated, "Удалённое условие", "Removed condition") \
    X(ConditionCategoryPlayer, "Игрок", "Player") \
    X(ConditionCategoryInterface, "Интерфейс", "Interface") \
    X(ConditionCategoryGame, "Игра", "Game") \
    X(ConditionCategoryVehicle, "Транспорт", "Vehicle") \
    X(ConditionSearchHint, "Поиск условий", "Search conditions") \
    X(ConditionSelectedCount, "Выбрано: %d", "Selected: %d") \
    X(ConditionSelectedNone, "Условия не выбраны.", "No conditions selected.") \
    X(ConditionReset, "Сбросить", "Reset") \
    X(ConditionNoMatches, "Нет условий по этому фильтру.", "No conditions match this filter.") \
    X(InputModeText, "Свободный ввод", "Free text") \
    X(InputModeButtonsList, "Выбор из вариантов", "Pick from options") \
    X(InputModeButtonsListText, "Варианты + свой текст", "Options + custom text") \
    X(HotkeyModeModifierTrigger, "Модификатор + клавиша", "Modifier + key") \
    X(HotkeyModeOrderedCombo, "Последовательная комбинация", "Ordered combo") \
    X(QuickMenuModeHold, "Удержание", "Hold") \
    X(QuickMenuModeToggle, "Переключение", "Toggle") \
    X(SendLocalChat, "Локальный чат", "Local chat") \
    X(SendViaSamp, "Через SA:MP", "Send via SA:MP") \
    X(SendDirect, "Серверу", "To server") \
    X(SendNoSend, "Без отправки", "No send") \
    X(SendInsertChat, "Вставить в чат", "Insert into chat") \
    X(SendOpenChat, "Открыть чат", "Open chat") \
    X(SendDialog, "В диалог", "Into dialog") \
    X(SendClipboard, "В буфер обмена", "To clipboard") \
    X(SendLog, "В лог", "To log") \
    X(SendToast, "Уведомление", "Toast") \
    X(SendUnknown, "Неизвестно", "Unknown") \
    X(ToastBindConfirmExpired, "%s: подтверждение бинда истекло: %s", "%s: bind confirmation expired: %s") \
    X(ToastBindCanceled, "%s: бинд отменён: %s", "%s: bind canceled: %s") \
    X(ToastBindPaused, "Бинд поставлен на паузу: %s", "Bind paused: %s") \
    X(ToastBindResumed, "Бинд продолжен: %s", "Bind resumed: %s") \
    X(ToastConditionBlocked, "Бинд заблокирован: %s", "Bind blocked: %s") \
    X(ToastFinishActiveInput, "Сначала завершите активный ввод.", "Finish the active input first.") \
    X(ToastSendLocalFailed, "Не удалось добавить сообщение в чат SA:MP.", "Failed to add a message to the SA:MP chat.") \
    X(ToastSendSampFailed, "Не удалось отправить текст в SA:MP.", "Failed to send text to SA:MP.") \
    X(ToastInsertChatFailed, "Не удалось вставить текст в чат.", "Failed to insert text into chat.") \
    X(ToastOpenChatFailed, "Не удалось открыть чат с текстом.", "Failed to open chat with text.") \
    X(ToastInsertDialogFailed, "Не удалось вставить текст в диалог.", "Failed to insert text into dialog.") \
    X(ToastClipboardFailed, "Не удалось записать текст в буфер обмена.", "Failed to copy text to the clipboard.") \
    X(ToastClipboardCopied, "Скопировано в буфер обмена.", "Copied to the clipboard.") \
    X(ToastUnknownSendMethod, "Неизвестный способ отправки: %d", "Unknown send method: %d") \
    X(ToastBindTagTargetRequired, "Для %s нужно указать бинд или вызвать тег внутри запущенного бинда.", "%s requires a bind target or must be called from a running bind.") \
    X(ToastBindTagInvalidSyntax, "Некорректный selector для %s. Проверьте кавычки и порядок: Бинд, Папка, Категория.", "Invalid selector for %s. Check quotes and the Bind, Folder, Category order.") \
    X(ToastBindTagTooManyArguments, "Для %s указано слишком много аргументов.", "Too many arguments were provided for %s.") \
    X(ToastBindTagUnterminatedQuote, "В selector для %s не закрыта кавычка.", "The selector for %s contains an unterminated quote.") \
    X(ToastBindTagInvalidStableId, "Для %s нужен stable ID вида @bind-61.", "%s expects a stable ID such as @bind-61.") \
    X(ToastBindTagNoFolders, "Папки биндов ещё не созданы.", "No bind folders have been created yet.") \
    X(ToastBindTagFolderNotFound, "Папка для %s не найдена.", "The folder for %s was not found.") \
    X(ToastBindTagCategoryNotFound, "Категория для %s не найдена.", "The category for %s was not found.") \
    X(ToastBindTagAmbiguous, "Selector для %s неоднозначен: найдено несколько биндов или папок.", "The selector for %s is ambiguous: multiple binds or folders matched.") \
    X(ToastBindTagBindNotFound, "Бинд для %s не найден.", "The bind for %s was not found.") \
    X(ToastBindTagNotStarted, "Не удалось запустить бинд для %s.", "Failed to start the bind for %s.") \
    X(ToastBindTagAlreadyRunning, "Бинд для %s уже запущен. Используйте bindpause, bindunpause или bindstop.", "The bind for %s is already running. Use bindpause, bindunpause, or bindstop.") \
    X(ToastBindTagDisabled, "Бинд для %s выключен.", "The bind for %s is disabled.") \
    X(ToastBindTagFolderDisabled, "Папка или родительская папка бинда для %s выключена.", "The bind folder or one of its parent folders is disabled for %s.") \
    X(ToastBindTagBusy, "Бинд для %s уже ждёт ввод или подтверждение.", "The bind for %s is already waiting for input or confirmation.") \
    X(ToastBindTagConditionsBlocked, "Условия запуска заблокировали бинд для %s.", "Start conditions blocked the bind for %s.") \
    X(ToastBindTagInputBusy, "Для %s нельзя открыть ввод: уже открыт ввод другого бинда.", "%s cannot open input because another bind input is already active.") \
    X(ToastBindTagEmpty, "Бинд для %s не содержит строк для выполнения.", "The bind for %s has no executable lines.") \
    X(ToastBindTagNotRunning, "Бинд для %s не запущен.", "The bind for %s is not running.") \
    X(ToastBindTagNotPaused, "Бинд для %s не стоит на паузе.", "The bind for %s is not paused.") \
    X(ToastBindTagAlreadyPaused, "Бинд для %s уже стоит на паузе.", "The bind for %s is already paused.") \
    X(ToastBindTagNoChanges, "%s не изменил состояние бинда.", "%s did not change the bind state.") \
    X(ToastBindTagPopupUnavailable, "Не удалось открыть попап строк бинда для %s.", "Failed to open the bind lines popup for %s.") \
    X(ToastBindTagStopAllEmpty, "Нет запущенных биндов для остановки.", "There are no running binds to stop.") \
    X(ToastBindTagUnknownAction, "Неизвестное действие bind-тега: %s", "Unknown bind-tag action: %s") \
    X(ToastScreenDocumentsUnavailable, "Не удалось определить папку данных GTA SA для сохранения скриншота.", "Failed to resolve the GTA SA userfiles folder for saving the screenshot.") \
    X(ToastScreenInvalidFolder, "Недопустимая подпапка для [screen(...)]: %s", "Invalid subfolder for [screen(...)]: %s") \
    X(ToastScreenCaptureFailed, "Не удалось сделать скриншот игры.", "Failed to capture the game screenshot.") \
    X(ToastTPhotoFailed, "Не удалось сделать фото через игровой фотоаппарат.", "Failed to take a photo through the in-game camera.") \
    X(ToastKeyDownInvalidFormat, "Для [keydown(...)] нужен формат key;milliseconds.", "The [keydown(...)] tag expects the key;milliseconds format.") \
    X(ToastKeyDownInvalidKey, "Для [keydown(...)] код клавиши должен быть целым числом от 1 до 255.", "The [keydown(...)] key code must be an integer from 1 to 255.") \
    X(ToastKeyDownInvalidDuration, "Для [keydown(...)] длительность должна быть целым числом больше 0.", "The [keydown(...)] duration must be an integer greater than 0.") \
    X(ToastTimefMissingSemicolon, "Для [timef(...)] формат должен заканчиваться точкой с запятой ;", "The format for [timef(...)] must end with a semicolon ;") \
    X(ToastTimefEmptyFormat, "Для [timef(...)] нужно указать формат времени перед ;", "You must provide a time format before ; in [timef(...)]") \
    X(ToastTimefInvalidSpecifier, "Неподдерживаемый параметр [timef(...)] : %s", "Unsupported [timef(...)] specifier: %s") \
    X(ToastTimefFormatFailed, "Не удалось отформатировать время для [timef(...)].", "Failed to format time for [timef(...)]") \
    X(ToastDialogCloseNoActive, "Для [dialogclose(...)] нужен активный диалог SA:MP.", "The [dialogclose(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogCloseInvalidButton, "Для [dialogclose(...)] допустимы только 1 или 0.", "Only 1 or 0 are allowed in [dialogclose(...)]") \
    X(ToastDialogCloseFailed, "Не удалось закрыть активный диалог через [dialogclose(...)].", "Failed to close the active dialog via [dialogclose(...)]") \
    X(ToastDialogSetTextNoActive, "Для [dialogsettext(...)] нужен активный диалог SA:MP.", "The [dialogsettext(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogSetTextNoEditbox, "В активном диалоге нет editbox для [dialogsettext(...)].", "The active dialog has no editbox for [dialogsettext(...)]") \
    X(ToastDialogSetTextFailed, "Не удалось установить текст в editbox через [dialogsettext(...)].", "Failed to set dialog editbox text via [dialogsettext(...)]") \
    X(ToastDialogWaitOpenTimedOut, "Диалог не открылся за 3 секунды. Текущий бинд остановлен.", "No dialog opened within 3 seconds. The current bind was stopped.") \
    X(ToastDialogWaitIdInvalidId, "Для [dialogwaitid(...)] нужно указать dialog id >= 0.", "You must provide a dialog id >= 0 for [dialogwaitid(...)]") \
    X(ToastDialogWaitIdTimedOut, "Диалог с id %s не открылся за 3 секунды. Текущий бинд остановлен.", "Dialog id %s did not open within 3 seconds. The current bind was stopped.") \
    X(ToastDialogItemNoActive, "Для [dialogitem(...)] нужен активный list/tablist диалог SA:MP.", "The [dialogitem(...)] tag requires an active SA:MP list/tablist dialog.") \
    X(ToastDialogItemEmptyParam, "Для [dialogitem(...)] нужно указать номер или текст пункта.", "You must provide an item number or item text for [dialogitem(...)]") \
    X(ToastDialogItemNotList, "Активный диалог не поддерживает выбор пункта через [dialogitem(...)].", "The active dialog does not support [dialogitem(...)] item selection.") \
    X(ToastDialogItemNotFound, "Пункт для [dialogitem(...)] не найден в активном диалоге.", "The requested [dialogitem(...)] target was not found in the active dialog.") \
    X(ToastDialogItemOutOfRange, "Для [dialogitem(...)] пункт %s вне диапазона активного списка.", "The [dialogitem(...)] target %s is out of range for the active dialog list.") \
    X(ToastDialogItemReadFailed, "Не удалось прочитать пункты активного диалога для [dialogitem(...)].", "Failed to read the active dialog items for [dialogitem(...)]") \
    X(ToastDialogItemFailed, "Не удалось открыть выбранный пункт через [dialogitem(...)].", "Failed to activate the selected item via [dialogitem(...)]") \
    X(ToastDialogSelectNoActive, "Для [dialogselect(...)] нужен активный list/tablist диалог SA:MP.", "The [dialogselect(...)] tag requires an active SA:MP list/tablist dialog.") \
    X(ToastDialogSelectEmptyParam, "Для [dialogselect(...)] нужно указать номер или текст пункта.", "You must provide an item number or item text for [dialogselect(...)]") \
    X(ToastDialogSelectNotList, "Активный диалог не поддерживает выбор пункта через [dialogselect(...)].", "The active dialog does not support [dialogselect(...)] item selection.") \
    X(ToastDialogSelectNotFound, "Пункт для [dialogselect(...)] не найден в активном диалоге.", "The requested [dialogselect(...)] target was not found in the active dialog.") \
    X(ToastDialogSelectOutOfRange, "Для [dialogselect(...)] пункт %s вне диапазона активного списка.", "The [dialogselect(...)] target %s is out of range for the active dialog list.") \
    X(ToastDialogSelectReadFailed, "Не удалось прочитать пункты активного диалога для [dialogselect(...)].", "Failed to read the active dialog items for [dialogselect(...)]") \
    X(ToastDialogSelectFailed, "Не удалось выбрать пункт через [dialogselect(...)].", "Failed to select the requested item via [dialogselect(...)]") \
    X(ToastDialogResponseNoActive, "Для [dialogresponse(...)] нужен активный диалог SA:MP.", "The [dialogresponse(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogResponseInvalidFormat, "Для [dialogresponse(...)] нужен формат button;item;text", "The [dialogresponse(...)] tag expects the format button;item;text") \
    X(ToastDialogResponseInvalidButton, "Для [dialogresponse(...)] button должен быть 1 или 0.", "The [dialogresponse(...)] button must be 1 or 0.") \
    X(ToastDialogResponseReadFailed, "Не удалось прочитать активный диалог для [dialogresponse(...)].", "Failed to read the active dialog for [dialogresponse(...)]") \
    X(ToastDialogResponseItemNotList, "Активный диалог не поддерживает выбор пункта через [dialogresponse(...)].", "The active dialog does not support item selection via [dialogresponse(...)]") \
    X(ToastDialogResponseItemNotFound, "Пункт для [dialogresponse(...)] не найден в активном диалоге.", "The requested [dialogresponse(...)] item was not found in the active dialog.") \
    X(ToastDialogResponseItemOutOfRange, "Для [dialogresponse(...)] пункт %s вне диапазона активного списка.", "The [dialogresponse(...)] item %s is out of range for the active dialog list.") \
    X(ToastDialogResponseFailed, "Не удалось отправить ответ через [dialogresponse(...)].", "Failed to send the response via [dialogresponse(...)]") \
    X(ToastDialogTextNoActive, "Для [dialogtext(...)] нужен активный диалог SA:MP.", "The [dialogtext(...)] tag requires an active SA:MP dialog.") \
    X(ToastDialogTextEmptyParam, "Для [dialogtext(...)] нужно указать индекс.", "You must provide an index for [dialogtext(...)]") \
    X(ToastDialogTextInvalidIndex, "Для [dialogtext(...)] индекс должен быть целым числом >= 0.", "The [dialogtext(...)] index must be an integer >= 0.") \
    X(ToastDialogTextOutOfRange, "Для [dialogtext(...)] индекс %s вне диапазона 0..%s.", "The [dialogtext(...)] index %s is out of range 0..%s.") \
    X(ToastDialogTextReadFailed, "Не удалось прочитать текст активного диалога для [dialogtext(...)].", "Failed to read the active dialog text for [dialogtext(...)]") \
    X(ToastSaveDialogNoActive, "Для [save_dialog(...)] нужен активный диалог SA:MP.", "The [save_dialog(...)] tag requires an active SA:MP dialog.") \
    X(ToastSaveDialogDocumentsUnavailable, "Не удалось определить папку данных GTA SA для [save_dialog(...)].", "Failed to resolve the GTA SA userfiles folder for [save_dialog(...)]") \
    X(ToastSaveDialogCreateDirFailed, "Не удалось создать папку сохранения для [save_dialog(...)].", "Failed to create the save directory for [save_dialog(...)]") \
    X(ToastSaveDialogWriteFailed, "Не удалось сохранить диалог через [save_dialog(...)].", "Failed to save the dialog via [save_dialog(...)]") \
    X(ToastSaveDialogSuccess, "Диалог сохранён: %s", "Dialog saved: %s") \
    X(ToastArzDialogCefUnavailable, "Arizona CEF диалог или packet bridge сейчас недоступен.", "The Arizona CEF dialog or packet bridge is currently unavailable.") \
    X(ToastArzDialogSetInputFailed, "Не удалось установить текст через [ARZdialogsetinputtext(...)].", "Failed to set text via [ARZdialogsetinputtext(...)]") \
    X(ToastArzDialogCloseInvalidButton, "Для [ARZdialogclosewithbutton(...)] допустимы только 1 или 0.", "Only 1 or 0 are allowed in [ARZdialogclosewithbutton(...)]") \
    X(ToastArzDialogCloseFailed, "Не удалось закрыть CEF диалог через [ARZdialogclosewithbutton(...)].", "Failed to close the CEF dialog via [ARZdialogclosewithbutton(...)]") \
    X(ToastArzDialogSetListInvalidIndex, "Для [ARZdialogsetlistitem(...)] индекс должен быть целым числом >= 0.", "The [ARZdialogsetlistitem(...)] index must be an integer >= 0.") \
    X(ToastArzDialogSetListFailed, "Не удалось выбрать пункт через [ARZdialogsetlistitem(...)].", "Failed to select an item via [ARZdialogsetlistitem(...)]") \
    X(ToastArzDialogItemNoActive, "Для [ARZdialogitem(...)] нужен активный Arizona CEF диалог.", "The [ARZdialogitem(...)] tag requires an active Arizona CEF dialog.") \
    X(ToastArzDialogItemEmptyParam, "Для [ARZdialogitem(...)] нужно указать номер или текст пункта.", "You must provide an item number or item text for [ARZdialogitem(...)]") \
    X(ToastArzDialogItemNotFound, "Пункт для [ARZdialogitem(...)] не найден в Arizona CEF диалоге.", "The requested [ARZdialogitem(...)] target was not found in the Arizona CEF dialog.") \
    X(ToastArzDialogItemOutOfRange, "Для [ARZdialogitem(...)] пункт %s вне диапазона списка.", "The [ARZdialogitem(...)] target %s is out of range for the dialog list.") \
    X(ToastArzDialogItemReadFailed, "Не удалось прочитать пункты Arizona CEF диалога для [ARZdialogitem(...)].", "Failed to read Arizona CEF dialog items for [ARZdialogitem(...)]") \
    X(ToastArzDialogItemFailed, "Не удалось открыть выбранный пункт через [ARZdialogitem(...)].", "Failed to open the selected item via [ARZdialogitem(...)]") \
    X(ToastArzDialogSendRespondInvalidFormat, "Для [ARZdialogsendrespond(...)] нужен формат id;button;listitem;input.", "The [ARZdialogsendrespond(...)] tag expects the format id;button;listitem;input.") \
    X(ToastArzDialogSendRespondInvalidId, "Для [ARZdialogsendrespond(...)] dialog id должен быть целым числом >= 0.", "The [ARZdialogsendrespond(...)] dialog id must be an integer >= 0.") \
    X(ToastArzDialogSendRespondInvalidButton, "Для [ARZdialogsendrespond(...)] button должен быть 1 или 0.", "The [ARZdialogsendrespond(...)] button must be 1 or 0.") \
    X(ToastArzDialogSendRespondInvalidList, "Для [ARZdialogsendrespond(...)] listitem должен быть целым числом >= -1 или пустым.", "The [ARZdialogsendrespond(...)] listitem must be an integer >= -1 or empty.") \
    X(ToastArzDialogSendRespondFailed, "Не удалось отправить RPC 62 через [ARZdialogsendrespond(...)].", "Failed to send RPC 62 via [ARZdialogsendrespond(...)]") \
    X(ToastArzDialogTextNoCached, "Для [ARZdialoggetdialogtext(...)] нужен кэш последнего Arizona CEF диалога.", "The [ARZdialoggetdialogtext(...)] tag requires a cached last Arizona CEF dialog.") \
    X(ToastArzDialogTextEmptyParam, "Для [ARZdialoggetdialogtext(...)] нужно указать индекс.", "You must provide an index for [ARZdialoggetdialogtext(...)]") \
    X(ToastArzDialogTextInvalidIndex, "Для [ARZdialoggetdialogtext(...)] индекс должен быть целым числом >= 0.", "The [ARZdialoggetdialogtext(...)] index must be an integer >= 0.") \
    X(ToastArzDialogTextOutOfRange, "Для [ARZdialoggetdialogtext(...)] индекс %s вне диапазона 0..%s.", "The [ARZdialoggetdialogtext(...)] index %s is out of range 0..%s.") \
    X(ToastIfAndOrInvalidSyntax, "Для [ifandor(...)] нужен формат Условие?Верно:Неверно", "The [ifandor(...)] tag requires the Condition?True:False format") \
    X(ToastIfAndOrEmptyCondition, "Для [ifandor(...)] нужно указать условие перед символом ?", "The [ifandor(...)] tag requires a condition before the ? symbol") \
    X(ToastIfAndOrConditionFailed, "Ошибка условия [ifandor(...)] : %s", "Failed to evaluate [ifandor(...)] condition: %s") \
    X(ToastBindSaved, "Бинд сохранён.", "Bind saved.") \
    X(ToastConfirmPrompt, "%s: подтвердить бинд \"%s\": [%s] принять, [%s] отменить", "%s: confirm bind \"%s\": [%s] accept, [%s] cancel") \
    X(ValidationFirstMessageRequired, "Заполните первую строку бинда.", "Fill in the first bind line.") \
    X(ValidationExistingFolderRequired, "Укажите существующую папку.", "Select an existing folder.") \
    X(ValidationFolderNameRequired, "Укажите название папки.", "Enter a folder name.") \
    X(ValidationFolderNameUnique, "Папка с таким названием уже есть здесь.", "A folder with this name already exists here.") \
    X(ValidationFolderNameReserved, "В названии папки нельзя использовать /. Имена * и ** зарезервированы selector-ом.", "Folder names cannot contain /. The names * and ** are reserved by bind selectors.") \
    X(ValidationCategoryNameRequired, "Укажите название категории.", "Enter a category name.") \
    X(ValidationCategoryNameUnique, "Категория с таким названием уже есть.", "A category with this name already exists.") \
    X(ValidationRepeatInterval, "Интервал повтора не может быть отрицательным.", "Repeat interval cannot be negative.") \
    X(ValidationConfirmCancelKeysDifferent, "Клавиши подтверждения и отклонения должны отличаться.", "Confirm and cancel keys must be different.") \
    X(ValidationInputKeyRequired, "У каждого параметра должен быть служебный ключ.", "Each parameter must have a service key.") \
    X(ValidationInputKeyUnique, "Служебные ключи параметров должны быть уникальными.", "Parameter service keys must be unique.") \
    X(ValidationButtonsRequired, "Для параметра с вариантами нужен хотя бы один вариант.", "Parameters with options require at least one option.") \
    X(ValidationButtonsTextRequired, "Для параметра с вариантами нужен хотя бы один вариант со значением.", "Parameters with options require at least one option with a value.") \
    X(ValidationInvalidRegex, "Некорректное регулярное выражение триггера: %s", "Invalid trigger regex: %s") \
    X(FolderAdd, "+ Папка", "+ Folder") \
    X(BinderAddFolderTooltip, "Добавить папку", "Add folder") \
    X(FolderRename, "Переименовать", "Rename") \
    X(FolderEnable, "Включить папку", "Enable folder") \
    X(FolderDisable, "Выключить папку", "Disable folder") \
    X(Delete, "Удалить", "Delete") \
    X(UndoFolderMove, "Отменить перемещение", "Undo move") \
    X(ToastFolderMoveInvalid, "Нельзя перенести папку сюда.", "Cannot move the folder here.") \
    X(FolderDropInto, "Сделать подпапкой", "Nest into folder") \
    X(BinderDropMoveHere, "Перенести сюда", "Move here") \
    X(BinderDropCurrentFolder, "Это уже текущая папка.", "This is already the current folder.") \
    X(BinderDropCrossCategoryDisabled, "Drag-and-drop между категориями отключён. Используйте \"Переместить в...\".", "Cross-category drag-and-drop is disabled. Use \"Move to...\".") \
    X(Name, "Название", "Name") \
    X(NameOptional, "Название (необяз.)", "Name (optional)") \
    X(Save, "Сохранить", "Save") \
    X(Done, "Готово", "Done") \
    X(Cancel, "Отмена", "Cancel") \
    X(Close, "Закрыть", "Close") \
    X(DeleteFolderMoveBindsQuestion, "Удалить папку вместе с подпапками и биндами?", "Delete the folder together with its subfolders and binds?") \
    X(DeleteFolderAll, "Удалить всё", "Delete all") \
    X(DeleteFolderMoveContentsHere, "Перенести содержимое сюда", "Move contents here") \
    X(BinderRootName, "Биндер", "Binder") \
    X(BinderNoFolder, "Без папки", "No folder") \
    X(CategoryAdd, "Добавить категорию", "Add category") \
    X(CategoryRenameTitle, "Переименование категории", "Rename category") \
    X(CategoryConditions, "Скрыть категорию, если активно", "Hide category when active") \
    X(CategoryQuickMenuConditions, "Блокировки быстрого меню", "Quick menu blocks") \
    X(CategoryCannotDeleteLast, "Нельзя удалить последнюю категорию.", "The last category cannot be deleted.") \
    X(DeleteCategoryQuestion, "Удалить категорию?", "Delete category?") \
    X(DeleteCategoryMoveContents, "Перенести и удалить", "Move and delete") \
    X(DeleteCategoryAll, "Удалить всё", "Delete all") \
    X(CategoryMoveContentsTarget, "Куда перенести", "Move contents to") \
    X(CategoryMoveLeft, "Переместить влево", "Move left") \
    X(CategoryMoveRight, "Переместить вправо", "Move right") \
    X(BinderEmptyFolder, "Папка пуста", "Folder is empty") \
    X(BinderEmptyBinds, "Биндов нет", "No binds") \
    X(BinderSearchGlobal, "Поиск", "Search") \
    X(BinderSearchFolders, "Поиск папок", "Search folders") \
    X(BinderSearchBinds, "Поиск биндов", "Search binds") \
    X(BinderGoUp, "Вверх", "Up") \
    X(BinderOpenFolder, "Открыть", "Open") \
    X(BindFolderDisabledTooltip, "Бинд, его папка или родительская папка выключены.", "This bind, its folder, or a parent folder is disabled.") \
    X(AddField, "+ Параметр", "+ Parameter") \
    X(AddButton, "+ Вариант", "+ Option") \
    X(FieldLabelFormat, "Параметр %d", "Parameter %d") \
    X(ParameterSelectorFormat, "%s · %d/%d", "%s · %d/%d") \
    X(ParameterAddHint, "+ Добавить подсказку", "+ Add hint") \
    X(ParameterAdvancedCompact, "Дополнительно", "Advanced") \
    X(ParameterVariantsCountFormat, "Варианты (%d)", "Options (%d)") \
    X(ParameterBulkEdit, "Править списком", "Edit as list") \
    X(ParameterBulkTitle, "Варианты списком", "Options as list") \
    X(ParameterOptionActions, "Действия с вариантом", "Option actions") \
    X(ButtonLabelFormat, "Вариант %d", "Option %d") \
    X(UnnamedField, "(без названия)", "(unnamed)") \
    X(ButtonPropertiesTitle, "Свойства варианта", "Option properties") \
    X(InputFieldsEmpty, "Параметры ещё не настроены.", "No parameters are configured yet.") \
    X(ParameterPrompt, "Заголовок вопроса", "Prompt title") \
    X(ParameterHintText, "Подсказка под вопросом", "Hint under the prompt") \
    X(ParameterResponseType, "Тип ответа", "Answer type") \
    X(ParameterAllowMultiple, "Можно выбрать несколько", "Allow multiple choices") \
    X(ParameterJoinSeparator, "Разделитель при подстановке", "Separator for inserted values") \
    X(ParameterVariantsSection, "Варианты ответа", "Answer options") \
    X(ParameterVariantsHint, "Название показывается игроку, а значение подставляется в {{KEY}}.", "The label is shown to the player, while the value is inserted into {{KEY}}.") \
    X(ParameterAdvancedHint, "Служебный ключ нужен для {{KEY}}, зависимостей и каскадных списков.", "The service key is used for {{KEY}}, dependencies, and cascading lists.") \
    X(ParameterSystemKey, "Служебный ключ", "Service key") \
    X(ParameterDependsOn, "Зависит от параметра", "Depends on parameter") \
    X(OptionName, "Название варианта", "Option label") \
    X(OptionValue, "Подставляемое значение", "Inserted value") \
    X(OptionHint, "Подсказка варианта", "Option hint") \
    X(Key, "Ключ", "Key") \
    X(Hint, "Подсказка", "Hint") \
    X(Mode, "Режим", "Mode") \
    X(MultiSelect, "Множественный выбор", "Multi-select") \
    X(Separator, "Разделитель", "Separator") \
    X(CascadeFromKey, "Каскад от ключа", "Cascade from key") \
    X(Buttons, "Варианты", "Options") \
    X(ButtonsEmpty, "Список вариантов пуст.", "The options list is empty.") \
    X(ButtonsBulkAddLine, "+ 1 строка", "+ 1 line") \
    X(ButtonsBulkAddFiveLines, "+ 5 строк", "+ 5 lines") \
    X(ButtonsBulkTemplateValueFormat, "значение_%d", "value_%d") \
    X(ButtonsBulkTemplateHintFormat, "Подсказка %d", "Hint %d") \
    X(ButtonsBulkNormalize, "Нормализовать", "Normalize") \
    X(ButtonsBulkApply, "Применить", "Apply") \
    X(ButtonsFormatHint, "Формат строки: название | значение | подсказка", "Line format: label | value | hint") \
    X(ButtonsCascadeFormatHint, "Для зависимого списка поддерживается четвёртый столбец when.", "Dependent lists support an optional fourth when column.") \
    X(ButtonsBulkEscapingHint, "Поддерживаются комментарии # и экранирование \\\\|, \\\\n, \\\\\\\\.", "Supports # comments and escaping \\\\|, \\\\n, \\\\\\\\.") \
    X(ButtonsBulkPreviewFormat, "Строк: %d, полезных: %d, пропущено: %d, кнопок после разбора: %d", "Lines: %d, used: %d, ignored: %d, buttons after parse: %d") \
    X(ButtonsBulkExtraPipesHint, "Лишние символы | после третьего столбца объединяются в when.", "Extra | after the third column are merged into when.") \
    X(MoveUp, "Поднять выше", "Move up") \
    X(MoveDown, "Опустить ниже", "Move down") \
    X(CopyPlaceholder, "Скопировать {{KEY}}", "Copy {{KEY}}") \
    X(InputFieldPlaceholderFormat, "Подстановка: {{%s}}", "Insert token: {{%s}}") \
    X(NewBindTitle, "Новый бинд", "New bind") \
    X(EditBindTitle, "Редактирование бинда", "Edit bind") \
    X(EditorPrimaryLaunch, "Основное", "Main") \
    X(EditorScenarioTab, "Сценарий", "Scenario") \
    X(EditorMultiInputTitle, "Мульти-ввод", "Multi-input") \
    X(EditorOpenMultiInput, "Мульти-ввод", "Multi-input") \
    X(EditorInputFieldsTab, "Параметры", "Parameters") \
    X(EditorOpenConditions, "Условия", "Conditions") \
    X(EditorConditionsButtonCount, "Условия (%d)", "Conditions (%d)") \
    X(EditorConditionsNone, "Блокировок: нет", "Blocks: none") \
    X(EditorConditionsCount, "Блокировок: %d", "Blocks: %d") \
    X(EditorBack, "Назад", "Back") \
    X(EditorPreviousBind, "Предыдущий бинд", "Previous bind") \
    X(EditorNextBind, "Следующий бинд", "Next bind") \
    X(EditorUnsaved, "Несохранённые изменения", "Unsaved changes") \
    X(EditorTriggerHint, "Срабатывает по входящему или исходящему сообщению. Перед сравнением удаляются цветовые коды, нормализуются окончания строк и пробелы по краям.", "Triggers on incoming or outgoing messages. Color codes are removed, line endings are normalized, and surrounding whitespace is trimmed before matching.") \
    X(EditorTriggerPatternTooltip, "Открыть помощник PCRE2.\n\nОбычный триггер сравнивает всю подготовленную строку с учётом регистра. Шаблон ищет фрагмент; для полной строки используйте \\A в начале и \\z в конце. Доступны Unicode-классы, группы и lookaround. Проверка ограничена по ресурсам, чтобы сложное выражение не подвешивало игру.", "Open the PCRE2 helper.\n\nA plain trigger compares the full prepared string case-sensitively. A pattern searches for a fragment; use \\A at the start and \\z at the end for the full string. Unicode classes, groups, and lookaround are supported. Matching is resource-limited so a complex expression cannot hang the game.") \
    X(EditorTriggerExample, "Например: Голова, [Гг]олова, \\Aдом\\d+\\z", "For example: Head, [Hh]ead, \\Ahouse\\d+\\z") \
    X(EditorPatternHelperTitle, "Шаблон текстового триггера", "Text trigger pattern") \
    X(EditorPatternEnabled, "Использовать шаблон PCRE2", "Use a PCRE2 pattern") \
    X(EditorPatternCurrent, "Текст триггера / текущий шаблон", "Trigger text / current pattern") \
    X(EditorPatternSample, "Пример и проверка", "Example and test") \
    X(EditorPatternSampleHint, "Можно вставить полную строку чатлога: время будет удалено. Цветовые коды удаляются так же, как в игре.", "You can paste a full chatlog line: its timestamp will be removed. Color codes are removed exactly as they are in game.") \
    X(EditorPatternReferenceHint, "Готовые элементы PCRE2. Нажмите выражение, чтобы скопировать его, или «Вставить», чтобы заменить выделение в тексте триггера либо добавить элемент в позицию курсора.", "Ready-to-use PCRE2 elements. Click an expression to copy it, or use Append to replace the trigger selection or insert it at the cursor.") \
    X(EditorPatternMatchesEmpty, "Этот шаблон совпадает даже с пустым сообщением и может запускать бинд слишком часто.", "This pattern also matches an empty message and may trigger the bind too often.") \
    X(EditorPatternTestStopped, "Проверка остановлена безопасным лимитом. Упростите шаблон.", "Testing stopped at a safety limit. Simplify the pattern.") \
    X(EditorPatternMatched, "Триггер сработает.", "The trigger will fire.") \
    X(EditorPatternNoMatch, "Триггер не сработает.", "The trigger will not fire.") \
    X(EditorScenarioHint, "Перетащите ручку слева, чтобы изменить порядок шагов. Пустые строки удаляются при сохранении.", "Drag the handle on the left to reorder steps. Empty rows are removed when saving.") \
    X(EditorAppendStepHint, "Введите сообщение для нового шага", "Type a message for a new step") \
    X(EditorAppendStepTooltip, "Введите текст: строка сразу станет новым шагом. При вставке нескольких строк каждая непустая строка станет отдельным шагом. Пауза и метод копируются из предыдущего шага.", "Type text: the row immediately becomes a new step. When pasting multiple lines, each non-empty line becomes a separate step. Delay and method are copied from the previous step.") \
    X(EditorMoveStep, "Переместить шаг", "Move step") \
    X(EditorVariables, "Переменные", "Variables") \
    X(EditorVariablesTitle, "Переменные бинда", "Bind variables") \
    X(EditorVariablesHint, "Используйте {{KEY}} или {{1}} в сообщениях, чтобы подставить значения параметров.", "Use {{KEY}} or {{1}} in messages to insert parameter values.") \
    X(EditorVariablesEmpty, "Параметры ещё не настроены.", "No parameters are configured yet.") \
    X(EditorVariablesKeyPickerInsertHint, "Щелчок по строке вставляет [keyemulate(code)] в выбранное поле сценария. Если поле не выбрано, токен копируется.", "Clicking a row inserts [keyemulate(code)] into the selected scenario field. If no field is selected, the token is copied.") \
    X(EditorVariablesDialogPickerInsertHint, "Щелчок по строке вставляет токен в выбранное поле сценария. Если поле не выбрано, токен копируется.", "Clicking a row inserts the token into the selected scenario field. If no field is selected, the token is copied.") \
    X(EditorDiscardTitle, "Несохранённые изменения", "Unsaved changes") \
    X(EditorDiscardMessage, "Изменения не сохранены. Продолжить и потерять правки?", "Changes are not saved. Continue and discard them?") \
    X(EditorDiscardAction, "Продолжить", "Continue") \
    X(EditorStay, "Остаться", "Stay") \
    X(EditorColumnMessage, "Сообщение", "Message") \
    X(EditorColumnPauseMs, "Пауза (мс)", "Pause (ms)") \
    X(EditorColumnDestination, "Куда", "Destination") \
    X(EditorConfirmationHint, "После активации по триггеру или команде бинд ждёт отдельные клавиши подтверждения и отклонения.", "After trigger or command activation, the bind waits for separate confirm and cancel keys.") \
    X(EditorConfirmationSettings, "Настройки", "Settings") \
    X(EditorConfirmationSettingsDisabledTooltip, "Включите подтверждение триггера или команды, чтобы настроить клавиши.", "Enable trigger or command confirmation to configure keys.") \
    X(EditorMultiInputHint, "Каждая непустая строка станет отдельным шагом. Пустые строки игнорируются.", "Each non-empty line becomes a separate step. Empty lines are ignored.") \
    X(Enabled, "Включён", "Enabled") \
    X(Folder, "Папка", "Folder") \
    X(HotkeyMode, "Режим хоткея", "Hotkey mode") \
    X(HotkeyNotSet, "Не задано", "Not set") \
    X(HotkeyFormat, "Хоткей: %s", "Hotkey: %s") \
    X(ChangeHotkey, "Изменить хоткей", "Change hotkey") \
    X(ShowInQuickMenu, "Показывать в быстром меню", "Show in quick menu") \
    X(EditorToggleQuickMenu, "Быстрое меню", "Quick menu") \
    X(EditorQuickMenuTooltip, "Показывать бинд в быстром меню.", "Show this bind in the quick menu.") \
    X(EditorTogglePattern, "Шаблон", "Pattern") \
    X(EditorToggleTextConfirm, "Подтв. триггер", "Confirm trigger") \
    X(EditorToggleCommandConfirm, "Подтв. команда", "Confirm command") \
    X(Repeat, "Повтор", "Repeat") \
    X(EditorRepeatTooltip, "Зацикливает бинд, пока зажата клавиша активации. Если выключено, запуск по клавише выполняется один раз.", "Repeats the bind while the activation key is held. When disabled, a key activation runs once.") \
    X(RepeatInterval, "Интервал повтора", "Repeat interval") \
    X(AddRow, "+ Строка", "+ Row") \
    X(ColumnSpacer, " ", " ") \
    X(ColumnEnabledShort, "Вкл", "On") \
    X(ColumnQuickShort, "БМ", "QM") \
    X(ColumnNumberShort, "№", "#") \
    X(ActionRemoveShort, "X", "X") \
    X(ColumnText, "Текст", "Text") \
    X(ColumnDelay, "Задержка", "Delay") \
    X(ColumnMethod, "Метод", "Method") \
    X(TextTrigger, "Триггер по тексту в чате", "Text trigger") \
    X(Command, "Команда", "Command") \
    X(TextConfirmation, "Требовать подтверждение по триггеру", "Require confirmation on trigger") \
    X(CommandConfirmation, "Требовать подтверждение по команде", "Require confirmation on command") \
    X(WaitWithoutTimeout, "Дожидаться подтверждения или отклонения", "Wait for confirmation or rejection") \
    X(TriggerWaitWithoutTimeout, "Ждать подтверждения по триггеру до таймера", "Wait for trigger confirmation until timeout") \
    X(TriggerWaitTimeoutHint, "Таймер настраивается во вкладке «Настройки» -> «Биндер». По умолчанию: 60 секунд.", "The timeout is configured in Settings -> Binder. Default: 60 seconds.") \
    X(CommandWaitWithoutTimeout, "Дожидаться подтверждения или отклонения по команде", "Wait for confirmation or rejection on command") \
    X(ConfirmKeyFormat, "Клавиша подтверждения: %s", "Confirm key: %s") \
    X(CancelKeyFormat, "Клавиша отклонения: %s", "Cancel key: %s") \
    X(Change, "Изменить", "Change") \
    X(BlockingConditions, "Не запускать, если активно", "Do not run when active") \
    X(FolderConditions, "Скрыть папку, если активно", "Hide folder when active") \
    X(ConditionBlockHint, "Сработает блокировка, если активно хотя бы одно отмеченное условие.", "Blocks when any selected condition is active.") \
    X(InputDialogSearchHint, "Поиск по названию, тексту или подсказке", "Search by label, text, or hint") \
    X(InputDialogNoOptions, "Нет доступных вариантов.", "No available options.") \
    X(InputDialogPreviewSummaryFormat, "Предпросмотр · %d сообщений", "Preview · %d messages") \
    X(InputDialogPreviewEmpty, "В сценарии нет сообщений для отправки.", "There are no messages to send in this scenario.") \
    X(InputDialogFieldSummaryFormat, "%s · %s", "%s · %s") \
    X(InputDialogEmptyValue, "не заполнено", "not filled") \
    X(InputDialogCancelHint, "Esc — отмена", "Esc — cancel") \
    X(AddBind, "+ Бинд", "+ Bind") \
    X(BinderAddBindTooltip, "Добавить бинд", "Add bind") \
    X(BinderClearSearch, "Очистить поиск", "Clear search") \
    X(Edit, "Изменить", "Edit") \
    X(Run, "Запустить", "Run") \
    X(Resume, "Продолжить", "Resume") \
    X(Pause, "Пауза", "Pause") \
    X(Stop, "Стоп", "Stop") \
    X(SearchBinds, "Поиск биндов", "Search binds") \
    X(ColumnName, "Название", "Name") \
    X(ColumnHotkey, "Клавиша", "Hotkey") \
    X(ColumnActions, "Действия", "Actions") \
    X(ActionMoveTo, "Переместить в...", "Move to...") \
    X(ActionDuplicate, "Дублировать", "Duplicate") \
    X(ActionBindLines, "Строки бинда...", "Bind lines...") \
    X(BindLinesTitle, "Строки бинда", "Bind lines") \
    X(BindLinesEmpty, "В бинде нет строк.", "This bind has no lines.") \
    X(Send, "Отправить", "Send") \
    X(BindListEntryFormat, "№%d %s", "#%d %s") \
    X(DeleteSelectedBindQuestion, "Удалить выбранный бинд?", "Delete the selected bind?") \
    X(CapturePrompt, "Нажмите комбинацию клавиш. Enter сохранить, Backspace очистить, Esc отменить.", "Press a key combination. Enter saves, Backspace clears, Esc cancels.") \
    X(CurrentCombinationFormat, "Текущая комбинация: %s", "Current combination: %s") \
    X(WaitingMouseButton, "Ожидание кнопки мыши...", "Waiting for a mouse button...") \
    X(Clear, "Очистить", "Clear") \
    X(Mouse, "Мышь", "Mouse") \
    X(Launch, "Запустить", "Launch") \
    X(BinderSectionTitle, "Биндер", "Binder") \
    X(QuickMenuWindowTitle, "Быстрое меню", "Quick menu") \
    X(QuickMenuFormat, "Быстрое меню: %s", "Quick menu: %s") \
    X(ChangeQuickMenuHotkey, "Изменить хоткей быстрого меню", "Change quick menu hotkey") \
    X(QuickMenuMode, "Режим быстрого меню", "Quick menu mode") \
    X(QuickMenuStyle, "Стиль быстрого меню", "Quick menu style") \
    X(QuickMenuStyleTree, "Стиль 1: дерево", "Style 1: tree") \
    X(QuickMenuStyleCascade, "Стиль 2: каскадное меню", "Style 2: cascaded menu") \
    X(QuickMenuShowScrollbar, "Показывать полосу прокрутки", "Show scroll bar") \
    X(QuickMenuShowScrollbarHint, "Скрывает только визуальную полосу справа; прокрутка колесом мыши остаётся рабочей.", "Only hides the visual bar on the right; mouse-wheel scrolling still works.") \
    X(ColumnFolders, "Папки", "Folders") \
    X(ColumnBinds, "Бинды", "Binds")

enum class UiLanguage {
    Russian = 0,
    English,
};

enum class UiLogLevel : int {
    Off = 0,
    Error = 1,
    Info = 2,
};

enum class UiSettingsSection {
    General = 0,
    Binder,
    QuickMenu,
    Notifications,
    Profiles,
    Hotkeys,
    Diagnostics,
};

enum class UiText {
#define APP_UI_TEXT_ENUM(id, ru, en) id,
    APP_UI_TEXTS(APP_UI_TEXT_ENUM)
#undef APP_UI_TEXT_ENUM
    Count,
};

class UiSettings {
public:
    static UiSettings& Instance();

    void Load();

    UiLanguage Language() const;
    void SetLanguage(UiLanguage language);

    bool AutoScaleEnabled() const;
    void SetAutoScaleEnabled(bool enabled);

    float ScaleMultiplier() const;
    float ScaleMultiplierDraft() const;
    void SetScaleMultiplierDraft(float multiplier);
    bool CommitScaleMultiplierDraft();

    ui_fonts::FontFamily FontFamily() const;
    void SetFontFamily(ui_fonts::FontFamily family);
    float FontSize() const;
    void SetFontSize(float fontSize);

    UiLogLevel LogLevel() const;
    void SetLogLevel(UiLogLevel level);
    bool ApplyDamageProtectionEnabled() const;
    void SetApplyDamageProtectionEnabled(bool enabled);

    const std::vector<unsigned int>& MenuToggleHotkey() const;
    void SetMenuToggleHotkey(const std::vector<unsigned int>& hotkey);
    void ResetMenuToggleHotkey();

    UiSettingsSection SettingsActiveSection() const;
    void SetSettingsActiveSection(UiSettingsSection section);

    void ResetToDefaults();

    float UpdateScale(const ImVec2& displaySize);
    float CurrentScale() const;
    float Scale(float value) const;
    ImVec2 Scale(const ImVec2& value) const;

    const char* Text(UiText id) const;
    std::string Format(UiText id, ...) const;
    const char* LanguageDisplayName(UiLanguage language) const;

private:
    void QueueSave() const;
    float ComputeAutoScale(const ImVec2& displaySize) const;
    static float NormalizeScaleMultiplier(float multiplier);

    UiLanguage language_ = UiLanguage::Russian;
    bool autoScaleEnabled_ = true;
    float scaleMultiplier_ = 1.0f;
    float scaleMultiplierDraft_ = 1.0f;
    ui_fonts::FontFamily fontFamily_ = ui_fonts::FontFamily::Tahoma;
    float fontSize_ = ui_fonts::kDefaultFontSize;
    UiLogLevel logLevel_ = UiLogLevel::Info;
    bool applyDamageProtectionEnabled_ = true;
    std::vector<unsigned int> menuToggleHotkey_{};
    UiSettingsSection settingsActiveSection_ = UiSettingsSection::General;
    float currentScale_ = 1.0f;
};
