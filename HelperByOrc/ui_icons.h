#pragma once

#include <imgui.h>

namespace ui_icons {

inline constexpr char Search[] = "\xEF\x80\x82";        // U+F002
inline constexpr char Star[] = "\xEF\x80\x86";          // U+F006
inline constexpr char Check[] = "\xEF\x80\x8C";         // U+F00C
inline constexpr char Xmark[] = "\xEF\x80\x8D";         // U+F00D
inline constexpr char Gear[] = "\xEF\x80\x93";          // U+F013
inline constexpr char Tags[] = "\xEF\x80\xAC";          // U+F02C
inline constexpr char Book[] = "\xEF\x80\xAD";          // U+F02D
inline constexpr char Image[] = "\xEF\x80\xBE";         // U+F03E
inline constexpr char Edit[] = "\xEF\x81\x84";          // U+F044
inline constexpr char Play[] = "\xEF\x81\x8B";          // U+F04B
inline constexpr char Pause[] = "\xEF\x81\x8C";         // U+F04C
inline constexpr char Stop[] = "\xEF\x81\x8D";          // U+F04D
inline constexpr char ChevronLeft[] = "\xEF\x81\x93";  // U+F053
inline constexpr char ChevronRight[] = "\xEF\x81\x94"; // U+F054
inline constexpr char Plus[] = "\xEF\x81\xA7";          // U+F067
inline constexpr char MoveRows[] = "\xEF\x81\xBD";      // U+F07D
inline constexpr char Copy[] = "\xEF\x83\x85";          // U+F0C5
inline constexpr char SaveDisk[] = "\xEF\x83\x87";      // U+F0C7
inline constexpr char Bars[] = "\xEF\x83\x89";          // U+F0C9
inline constexpr char Comment[] = "\xEF\x83\xA5";       // U+F0E5
inline constexpr char Bolt[] = "\xEF\x83\xA7";          // U+F0E7
inline constexpr char AngleUp[] = "\xEF\x84\x86";       // U+F106
inline constexpr char AngleDown[] = "\xEF\x84\x87";     // U+F107
inline constexpr char Folder[] = "\xEF\x84\x94";        // U+F114
inline constexpr char Keyboard[] = "\xEF\x84\x9C";      // U+F11C
inline constexpr char Terminal[] = "\xEF\x84\xA0";      // U+F120
inline constexpr char Compass[] = "\xEF\x85\x8E";       // U+F14E
inline constexpr char Cubes[] = "\xEF\x86\xB3";         // U+F1B3
inline constexpr char Sliders[] = "\xEF\x87\x9E";       // U+F1DE
inline constexpr char Newspaper[] = "\xEF\x87\xAA";     // U+F1EA
inline constexpr char ToggleOff[] = "\xEF\x88\x84";     // U+F204
inline constexpr char ToggleOn[] = "\xEF\x88\x85";      // U+F205
inline constexpr char Clone[] = "\xEF\x89\x8D";         // U+F24D
inline constexpr char RotateLeft[] = "\xEF\x8B\xAA";    // U+F2EA
inline constexpr char Delete[] = "\xEF\x8B\xAD";        // U+F2ED
inline constexpr char FileExport[] = "\xEF\x95\xAE";    // U+F56E
inline constexpr char FileImport[] = "\xEF\x95\xAF";    // U+F56F
inline constexpr char FolderPlus[] = "\xEF\x99\x9E";    // U+F65E
inline constexpr char House[] = "\xEF\xA0\x8C";         // U+F80C

inline constexpr ImWchar FontAwesomeRanges[] = {
    0xF002, 0xF002,
    0xF006, 0xF006,
    0xF00C, 0xF00D,
    0xF013, 0xF013,
    0xF02C, 0xF02D,
    0xF03E, 0xF03E,
    0xF044, 0xF044,
    0xF04B, 0xF04D,
    0xF053, 0xF054,
    0xF067, 0xF067,
    0xF07D, 0xF07D,
    0xF0C5, 0xF0C7,
    0xF0C9, 0xF0C9,
    0xF0E5, 0xF0E7,
    0xF106, 0xF107,
    0xF114, 0xF114,
    0xF11C, 0xF11C,
    0xF120, 0xF120,
    0xF14E, 0xF14E,
    0xF1B3, 0xF1B3,
    0xF1DE, 0xF1DE,
    0xF1EA, 0xF1EA,
    0xF204, 0xF205,
    0xF24D, 0xF24D,
    0xF2EA, 0xF2EA,
    0xF2ED, 0xF2ED,
    0xF56E, 0xF56F,
    0xF65E, 0xF65E,
    0xF80C, 0xF80C,
    0,
};

} // namespace ui_icons
