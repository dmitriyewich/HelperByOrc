#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

enum class HotkeyMode {
    ModifierTrigger,
    OrderedCombo,
};

namespace hotkeys {

struct CaptureKeyInfo {
    UINT keyCode = 0;
    bool isDown = false;
    bool isUp = false;
};

UINT NormalizeKey(UINT key);
bool IsMouseKey(UINT key);
bool IsModifierKey(UINT key);
bool IsHotkeyKey(UINT key);
bool HasTriggerKey(const std::vector<UINT>& keys);
std::vector<UINT> NormalizeCombo(const std::vector<UINT>& keys, HotkeyMode mode = HotkeyMode::ModifierTrigger);
bool ComboMatch(const std::vector<UINT>& pressed, const std::vector<UINT>& combo, HotkeyMode mode = HotkeyMode::ModifierTrigger);
bool ContainsCombo(const std::vector<UINT>& pressed, const std::vector<UINT>& combo, HotkeyMode mode = HotkeyMode::ModifierTrigger);
bool CombosConflict(const std::vector<UINT>& lhsKeys, HotkeyMode lhsMode, const std::vector<UINT>& rhsKeys, HotkeyMode rhsMode);
std::vector<UINT> CollectPressedKeys();
std::string KeyName(UINT key);
std::string ToString(const std::vector<UINT>& keys, HotkeyMode mode = HotkeyMode::ModifierTrigger);
std::optional<CaptureKeyInfo> GetMessageKeyInfo(UINT message, WPARAM wparam);

class KeyTracker {
public:
    void Reset();
    bool OnWindowMessage(UINT message, WPARAM wparam);
    const std::vector<UINT>& Ordered() const;
    void KeyDown(UINT key);
    void KeyUp(UINT key);
    void Rebuild();

private:
    std::map<UINT, int> held_{};
    std::vector<UINT> ordered_{};
    int counter_ = 0;
};

class Capture {
public:
    void Start(const std::vector<UINT>& initial);
    void Stop();
    bool Active() const;
    void Clear();
    void ArmMouseCapture();
    bool MouseCaptureArmed() const;
    std::vector<UINT> Draft() const;
    bool Save(std::vector<UINT>& outKeys);
    bool OnWindowMessage(UINT message, WPARAM wparam, bool& canceled, bool& saved, std::vector<UINT>& outKeys);

private:
    bool active_ = false;
    KeyTracker tracker_{};
    std::vector<UINT> lastCombo_{};
    bool mouseCaptureArmed_ = false;
    UINT mousePendingKey_ = 0;
};

} // namespace hotkeys
