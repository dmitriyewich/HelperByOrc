#include "hotkey_utils.h"

#include "ui_settings.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <set>
#include <sstream>

UINT hotkeys::NormalizeKey(UINT key) {
    switch (key) {
    case VK_LCONTROL:
    case VK_RCONTROL:
        return VK_CONTROL;
    case VK_LSHIFT:
    case VK_RSHIFT:
        return VK_SHIFT;
    case VK_LMENU:
    case VK_RMENU:
        return VK_MENU;
    default:
        return key;
    }
}

bool hotkeys::IsMouseKey(UINT key) {
    switch (NormalizeKey(key)) {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        return false;
    }
}

bool hotkeys::IsModifierKey(UINT key) {
    switch (NormalizeKey(key)) {
    case VK_CONTROL:
    case VK_SHIFT:
    case VK_MENU:
    case VK_LWIN:
    case VK_RWIN:
        return true;
    default:
        return false;
    }
}

bool hotkeys::IsHotkeyKey(UINT key) {
    const UINT normalized = NormalizeKey(key);
    return normalized != 0 && normalized <= 0xFF;
}

bool hotkeys::HasTriggerKey(const std::vector<UINT>& keys) {
    return std::any_of(keys.begin(), keys.end(), [](UINT key) { return !IsModifierKey(key); });
}

std::vector<UINT> hotkeys::NormalizeCombo(const std::vector<UINT>& keys, HotkeyMode mode) {
    std::vector<UINT> normalized;
    std::set<UINT> seen;
    normalized.reserve(keys.size());
    for (const UINT key : keys) {
        const UINT normalizedKey = NormalizeKey(key);
        if (!IsHotkeyKey(normalizedKey) || !seen.insert(normalizedKey).second) {
            continue;
        }
        normalized.push_back(normalizedKey);
    }

    if (mode == HotkeyMode::OrderedCombo) {
        return normalized;
    }

    std::vector<UINT> modifiers;
    std::vector<UINT> triggers;
    modifiers.reserve(normalized.size());
    triggers.reserve(normalized.size());
    for (const UINT key : normalized) {
        if (IsModifierKey(key)) {
            modifiers.push_back(key);
        } else {
            triggers.push_back(key);
        }
    }

    auto modifierOrder = [](UINT key) {
        switch (key) {
        case VK_CONTROL:
            return 1;
        case VK_SHIFT:
            return 2;
        case VK_MENU:
            return 3;
        case VK_LWIN:
            return 4;
        case VK_RWIN:
            return 5;
        default:
            return 100;
        }
    };

    std::sort(modifiers.begin(), modifiers.end(), [&](UINT lhs, UINT rhs) {
        const int leftOrder = modifierOrder(lhs);
        const int rightOrder = modifierOrder(rhs);
        return leftOrder == rightOrder ? lhs < rhs : leftOrder < rightOrder;
    });

    modifiers.insert(modifiers.end(), triggers.begin(), triggers.end());
    return modifiers;
}

bool hotkeys::ComboMatch(const std::vector<UINT>& pressed, const std::vector<UINT>& combo, HotkeyMode mode) {
    if (combo.empty()) {
        return false;
    }

    if (mode == HotkeyMode::OrderedCombo) {
        if (pressed.size() != combo.size()) {
            return false;
        }
        for (std::size_t i = 0; i < combo.size(); ++i) {
            if (NormalizeKey(pressed[i]) != NormalizeKey(combo[i])) {
                return false;
            }
        }
        return true;
    }

    return NormalizeCombo(pressed, mode) == NormalizeCombo(combo, mode);
}

bool hotkeys::ContainsCombo(const std::vector<UINT>& pressed, const std::vector<UINT>& combo, HotkeyMode mode) {
    if (combo.empty()) {
        return false;
    }

    if (mode == HotkeyMode::OrderedCombo) {
        return ComboMatch(pressed, combo, mode);
    }

    const auto normalizedPressed = NormalizeCombo(pressed, mode);
    const auto normalizedCombo = NormalizeCombo(combo, mode);
    if (normalizedCombo.empty() || normalizedPressed.size() < normalizedCombo.size()) {
        return false;
    }

    for (const UINT key : normalizedCombo) {
        if (std::find(normalizedPressed.begin(), normalizedPressed.end(), key) == normalizedPressed.end()) {
            return false;
        }
    }
    return true;
}

bool hotkeys::CombosConflict(
    const std::vector<UINT>& lhsKeys,
    HotkeyMode lhsMode,
    const std::vector<UINT>& rhsKeys,
    HotkeyMode rhsMode) {
    const auto normalizedLeft = NormalizeCombo(lhsKeys, lhsMode);
    const auto normalizedRight = NormalizeCombo(rhsKeys, rhsMode);
    if (normalizedLeft.empty() || normalizedRight.empty()) {
        return false;
    }

    if (lhsMode == HotkeyMode::OrderedCombo && rhsMode == HotkeyMode::OrderedCombo) {
        return normalizedLeft == normalizedRight;
    }

    return NormalizeCombo(lhsKeys, HotkeyMode::ModifierTrigger)
        == NormalizeCombo(rhsKeys, HotkeyMode::ModifierTrigger);
}

std::vector<UINT> hotkeys::CollectPressedKeys() {
    std::vector<UINT> pressed;
    pressed.reserve(16);
    for (UINT key = 1; key <= 0xFF; ++key) {
        if ((GetAsyncKeyState(static_cast<int>(key)) & 0x8000) != 0) {
            pressed.push_back(key);
        }
    }
    return NormalizeCombo(pressed, HotkeyMode::ModifierTrigger);
}

std::string hotkeys::KeyName(UINT key) {
    key = NormalizeKey(key);
    switch (key) {
    case VK_CONTROL:
        return "Ctrl";
    case VK_SHIFT:
        return "Shift";
    case VK_MENU:
        return "Alt";
    case VK_LWIN:
        return "LWin";
    case VK_RWIN:
        return "RWin";
    case VK_RETURN:
        return "Enter";
    case VK_SPACE:
        return "Space";
    case VK_TAB:
        return "Tab";
    case VK_ESCAPE:
        return "Esc";
    case VK_BACK:
        return "Backspace";
    case VK_DELETE:
        return "Delete";
    case VK_INSERT:
        return "Insert";
    case VK_HOME:
        return "Home";
    case VK_END:
        return "End";
    case VK_PRIOR:
        return "PageUp";
    case VK_NEXT:
        return "PageDown";
    case VK_LEFT:
        return "Left";
    case VK_RIGHT:
        return "Right";
    case VK_UP:
        return "Up";
    case VK_DOWN:
        return "Down";
    case VK_LBUTTON:
        return "Mouse1";
    case VK_RBUTTON:
        return "Mouse2";
    case VK_MBUTTON:
        return "Mouse3";
    case VK_XBUTTON1:
        return "XButton1";
    case VK_XBUTTON2:
        return "XButton2";
    default:
        break;
    }

    UINT scanCode = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
    if (key == VK_LEFT || key == VK_UP || key == VK_RIGHT || key == VK_DOWN || key == VK_PRIOR || key == VK_NEXT
        || key == VK_END || key == VK_HOME || key == VK_INSERT || key == VK_DELETE || key == VK_DIVIDE
        || key == VK_NUMLOCK) {
        scanCode |= 0x100;
    }

    char buffer[128]{};
    if (GetKeyNameTextA(static_cast<LONG>(scanCode << 16), buffer, static_cast<int>(std::size(buffer))) > 0) {
        return buffer;
    }

    if (key >= 'A' && key <= 'Z') {
        return std::string(1, static_cast<char>(key));
    }
    if (key >= '0' && key <= '9') {
        return std::string(1, static_cast<char>(key));
    }

    char fallback[16]{};
    std::snprintf(fallback, sizeof(fallback), "0x%02X", key);
    return fallback;
}

std::string hotkeys::ToString(const std::vector<UINT>& keys, HotkeyMode mode) {
    const auto normalized = NormalizeCombo(keys, mode);
    if (normalized.empty()) {
        return UiSettings::Instance().Text(UiText::HotkeyNotSet);
    }

    std::ostringstream stream;
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (i != 0) {
            stream << " + ";
        }
        stream << KeyName(normalized[i]);
    }
    return stream.str();
}

std::optional<hotkeys::CaptureKeyInfo> hotkeys::GetMessageKeyInfo(UINT message, WPARAM wparam) {
    std::optional<hotkeys::CaptureKeyInfo> result;

    auto setDown = [&](UINT key) {
        if (!IsHotkeyKey(key)) {
            return;
        }
        result = CaptureKeyInfo{ NormalizeKey(key), true, false };
    };
    auto setUp = [&](UINT key) {
        if (!IsHotkeyKey(key)) {
            return;
        }
        result = CaptureKeyInfo{ NormalizeKey(key), false, true };
    };

    switch (message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        setDown(static_cast<UINT>(wparam));
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        setUp(static_cast<UINT>(wparam));
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        setDown(VK_LBUTTON);
        break;
    case WM_LBUTTONUP:
        setUp(VK_LBUTTON);
        break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        setDown(VK_RBUTTON);
        break;
    case WM_RBUTTONUP:
        setUp(VK_RBUTTON);
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        setDown(VK_MBUTTON);
        break;
    case WM_MBUTTONUP:
        setUp(VK_MBUTTON);
        break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK: {
        const UINT button = GET_XBUTTON_WPARAM(wparam);
        setDown(button == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1);
        break;
    }
    case WM_XBUTTONUP: {
        const UINT button = GET_XBUTTON_WPARAM(wparam);
        setUp(button == XBUTTON2 ? VK_XBUTTON2 : VK_XBUTTON1);
        break;
    }
    default:
        break;
    }

    return result;
}

void hotkeys::KeyTracker::Reset() {
    held_.clear();
    ordered_.clear();
    counter_ = 0;
}

void hotkeys::KeyTracker::KeyDown(UINT key) {
    key = NormalizeKey(key);
    if (!IsHotkeyKey(key) || held_.contains(key)) {
        return;
    }
    held_[key] = ++counter_;
    Rebuild();
}

void hotkeys::KeyTracker::KeyUp(UINT key) {
    key = NormalizeKey(key);
    const auto it = held_.find(key);
    if (it == held_.end()) {
        return;
    }
    held_.erase(it);
    Rebuild();
    if (held_.empty()) {
        counter_ = 0;
    }
}

void hotkeys::KeyTracker::Rebuild() {
    std::vector<std::pair<UINT, int>> orderedPairs;
    orderedPairs.reserve(held_.size());
    for (const auto& [key, order] : held_) {
        orderedPairs.emplace_back(key, order);
    }
    std::sort(orderedPairs.begin(), orderedPairs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second < rhs.second;
    });

    ordered_.clear();
    ordered_.reserve(orderedPairs.size());
    for (const auto& [key, order] : orderedPairs) {
        (void)order;
        ordered_.push_back(key);
    }
}

bool hotkeys::KeyTracker::OnWindowMessage(UINT message, WPARAM wparam) {
    const auto keyInfo = GetMessageKeyInfo(message, wparam);
    if (!keyInfo.has_value()) {
        return false;
    }

    if (keyInfo->isDown) {
        KeyDown(keyInfo->keyCode);
    } else if (keyInfo->isUp) {
        KeyUp(keyInfo->keyCode);
    }
    return true;
}

const std::vector<UINT>& hotkeys::KeyTracker::Ordered() const {
    return ordered_;
}

void hotkeys::Capture::Start(const std::vector<UINT>& initial) {
    active_ = true;
    tracker_.Reset();
    lastCombo_ = initial;
    mouseCaptureArmed_ = false;
    mousePendingKey_ = 0;
}

void hotkeys::Capture::Stop() {
    active_ = false;
    tracker_.Reset();
    lastCombo_.clear();
    mouseCaptureArmed_ = false;
    mousePendingKey_ = 0;
}

bool hotkeys::Capture::Active() const {
    return active_;
}

void hotkeys::Capture::Clear() {
    tracker_.Reset();
    lastCombo_.clear();
    mouseCaptureArmed_ = false;
    mousePendingKey_ = 0;
}

void hotkeys::Capture::ArmMouseCapture() {
    if (!active_) {
        return;
    }
    mouseCaptureArmed_ = true;
    mousePendingKey_ = 0;
}

bool hotkeys::Capture::MouseCaptureArmed() const {
    return mouseCaptureArmed_;
}

std::vector<UINT> hotkeys::Capture::Draft() const {
    if (!tracker_.Ordered().empty()) {
        return tracker_.Ordered();
    }
    return lastCombo_;
}

bool hotkeys::Capture::Save(std::vector<UINT>& outKeys) {
    outKeys = Draft();
    Stop();
    return true;
}

bool hotkeys::Capture::OnWindowMessage(UINT message, WPARAM wparam, bool& canceled, bool& saved, std::vector<UINT>& outKeys) {
    canceled = false;
    saved = false;
    outKeys.clear();
    if (!active_) {
        return false;
    }

    const auto keyInfo = GetMessageKeyInfo(message, wparam);
    if (!keyInfo.has_value()) {
        return false;
    }

    const UINT key = keyInfo->keyCode;
    if (IsMouseKey(key) && !mouseCaptureArmed_) {
        return false;
    }

    if (keyInfo->isDown) {
        if (key == VK_ESCAPE) {
            Stop();
            canceled = true;
            return true;
        }
        if (key == VK_RETURN) {
            Save(outKeys);
            saved = true;
            return true;
        }
        if (key == VK_BACK) {
            Clear();
            return true;
        }

        tracker_.KeyDown(key);
        if (!tracker_.Ordered().empty()) {
            lastCombo_ = tracker_.Ordered();
        }
        if (IsMouseKey(key)) {
            mousePendingKey_ = key;
        }
        return true;
    }

    if (keyInfo->isUp) {
        tracker_.KeyUp(key);
        if (IsMouseKey(key) && mousePendingKey_ == key) {
            mouseCaptureArmed_ = false;
            mousePendingKey_ = 0;
        }
        return true;
    }

    return false;
}
