#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "external/raknet/RakClient.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class SampApi {
public:
    enum class Version {
        Unknown,
        E,
        R1,
        R2,
        R3,
        R3_1,
        R4,
        R4_2,
        R5_1,
        DL_R1,
    };

    enum class DialogStyle : int {
        MsgBox = 0,
        Input = 1,
        List = 2,
        Password = 3,
        TabList = 4,
        TabListHeaders = 5,
    };

    struct SampEntryPoint {
        std::uint32_t address;
        Version version;
        const char* name;
        bool supported;
    };

    struct VersionedOffset {
        std::uint32_t R1 = 0;
        std::uint32_t R2 = 0;
        std::uint32_t R3 = 0;
        std::uint32_t R3_1 = 0;
        std::uint32_t R4 = 0;
        std::uint32_t R4_2 = 0;
        std::uint32_t R5_1 = 0;
        std::uint32_t DL_R1 = 0;

        std::uint32_t Get(Version version) const;
    };

    struct MainOffsets {
        VersionedOffset SAMP_INFO_OFFSET;
        VersionedOffset rakclient_interface;
        VersionedOffset SAMP_DIALOG_INFO_OFFSET;
        VersionedOffset SAMP_DIALOG_ACTIVE_OFFSET;
        VersionedOffset SAMP_DIALOG_ID_OFFSET;
        VersionedOffset SAMP_DIALOG_TEXT_OFFSET;
        VersionedOffset CDialog_Show;
        VersionedOffset CDialog_Close;
        VersionedOffset SAMP_DIALOG_CAPTION_OFFSET;
        VersionedOffset CDXUTEditBox_GetText;
        VersionedOffset CDXUTEditBox_SetText;
        VersionedOffset pDialogInput_pEditBox;
        VersionedOffset pChatInput_pEditBox;
        VersionedOffset CDXUTDialog;
        VersionedOffset SetCursorMode;
        VersionedOffset RefGame;
        VersionedOffset CGameCursorMode;
        VersionedOffset AddEntry;
        VersionedOffset RenderEntry;
        VersionedOffset pChat;
        VersionedOffset CHAT_TEXT_OFFSET;
        VersionedOffset CHAT_PREFIX_TEXT_OFFSET;
        VersionedOffset CHAT_COLOR_OFFSET;
        VersionedOffset CHAT_PREFIX_COLOR_OFFSET;
        VersionedOffset AddChatMessage;
        VersionedOffset AddMessage;
        VersionedOffset SAMP_CHAT_INPUT_INFO_OFFSET;
        VersionedOffset CInput_Opened;
        VersionedOffset OnResetDevice;
        VersionedOffset CInput_Open;
        VersionedOffset CInput_Close;
        VersionedOffset CInput_Close_fix;
        VersionedOffset SetPageSize;
        VersionedOffset PageSize_MAX;
        VersionedOffset PageSize_StringInfo;
        VersionedOffset CInput_Send;
        VersionedOffset CInput_SendSay;
        VersionedOffset CInput_ProcessInput;
        VersionedOffset HotkeyDispatcher;
        VersionedOffset InputHotkeyHandler;
        VersionedOffset GetName;
        VersionedOffset SAMP_SLOCALPLAYERID_OFFSET;
        VersionedOffset SAMP_INFO_OFFSET_Pools;
        VersionedOffset SAMP_INFO_OFFSET_Pools_Player;
        VersionedOffset SAMP_INFO_OFFSET_Pools_Veh;
        VersionedOffset SAMP_COLOR_OFFSET;
        VersionedOffset ID_Find;
        VersionedOffset CPlayerPool_IsConnected;
        VersionedOffset IDcar_Find;
        VersionedOffset SAMP_PREMOTEPLAYER_OFFSET;
        VersionedOffset SAMP_REMOTEPLAYERDATA_OFFSET;
        VersionedOffset pSAMP_Actor;
        VersionedOffset pGTA_Ped;
        VersionedOffset IsConnected;
        VersionedOffset SAMP_REMOTEPLAYERDATA_HEALTH_OFFSET;
        VersionedOffset SAMP_REMOTEPLAYERDATA_ARMOR_OFFSET;
        VersionedOffset CDXUTListBox__GetSelectedIndex;
        VersionedOffset CDXUTListBox__GetItem;
        VersionedOffset SAMP_SET_DIALOG_LIST_ITEM_OFFSET;
        VersionedOffset CChatMode;
        VersionedOffset RefScoreboard;
        VersionedOffset CScoreboardEnabled;
        VersionedOffset CNetGameState;
        VersionedOffset RakHandleRpc;
        VersionedOffset RakStringWriteEncoder;
        VersionedOffset RakStringReadDecoder;
        VersionedOffset RakCompressorPtr;
    };

    struct DialogPosition {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool valid = false;
    };

    struct CursorRange {
        int start = -1;
        int end = -1;
        bool valid = false;
    };

    struct ChatEntry {
        std::string text;
        std::string prefix;
        std::uint32_t color = 0;
        std::uint32_t prefixColor = 0;
        bool valid = false;
    };

    struct DialogSelectionText {
        bool found = false;
        std::string text;
    };

    struct HealthAndArmour {
        float health = 100.0f;
        float armour = 100.0f;
        bool valid = false;
    };

    struct DialogSubmitResult {
        bool ok = false;
        int style = -1;
        std::string error;
    };

    struct BackendGlobalStatus {
        std::string name;
        std::string source;
    };

    struct FunctionBackendStatus {
        std::string desired;
        std::string active;
        bool hasSampfuncs = false;
        std::vector<BackendGlobalStatus> globals;
        std::vector<std::string> requiredNativeGlobals;
        std::vector<std::string> missingRequiredNativeGlobals;
    };

    using TextTransformCallback = std::function<std::string(std::string_view)>;

    static constexpr std::size_t entryPointCount = 9;
    static constexpr const char* BACKEND_STANDARD = "standard";
    static constexpr const char* BACKEND_SAMPFUNCS = "sampfuncs";
    static constexpr int DIALOG_STYLE_MSGBOX = static_cast<int>(DialogStyle::MsgBox);
    static constexpr int DIALOG_STYLE_INPUT = static_cast<int>(DialogStyle::Input);
    static constexpr int DIALOG_STYLE_LIST = static_cast<int>(DialogStyle::List);
    static constexpr int DIALOG_STYLE_PASSWORD = static_cast<int>(DialogStyle::Password);
    static constexpr int DIALOG_STYLE_TABLIST = static_cast<int>(DialogStyle::TabList);
    static constexpr int DIALOG_STYLE_TABLIST_HEADERS = static_cast<int>(DialogStyle::TabListHeaders);

    static const SampEntryPoint entryPoint[entryPointCount];
    static const MainOffsets main_offsets;

    void attachModules(TextTransformCallback changeTagsCallback);
    bool hasSampfuncs() const;

    void Refresh();
    bool isSampLoadedLua();
    bool isSAMPInitilizeLua();
    void LogReadinessDiagnostics(const char* context) const;

    std::uintptr_t PedPool();
    std::string GetNameID(int id);
    std::optional<int> GetIDByName(std::string_view name);
    bool IsConnected(int id);

    std::uintptr_t pDialog_func();
    bool sampSetCurrentDialogEditboxTextFix(std::string_view newString, bool alreadyDecoded = false);
    bool isDialogActive();
    DialogPosition getCurrentDialogPosition();
    bool Set_CursorMode(int mode, bool enabled);
    std::optional<int> GetCursorMode() const;
    bool IsSampCursorActive() const;
    bool hideDialog();
    bool CDialog_Close_func(int button);
    bool isDialogHookBypassActive() const;
    std::uintptr_t pDialogInput_pEditBox_func();
    bool pDialogInput_pEditBox_active_func();
    int SAMP_DIALOG_ID();
    bool sampSetDialogInputCursor(int cursor);
    CursorRange sampGetDialogInputCursor();
    std::uintptr_t SAMP_CHAT_INPUT_INFO_OFFSET_func();
    std::uintptr_t SAMP_CHAT_INPUT_INFO_OFFSET_func_test();
    std::string sampGetChatEditboxText();
    bool pCInput_Open_Close(bool open);
    bool sampSetChatInputCursor(int cursor);
    std::string sampGetDialogText();
    int getListItemNumberByText(std::string_view text);
    std::string get_dialog_caption();
    std::string sampGetDialogEditboxText();
    bool sampSetDialogEditboxText(std::string_view text, bool alreadyDecoded = false);
    bool SetCurrentDialogListItem(int index);
    int GetCurrentDialogListItem();
    DialogSelectionText getDialogSelectedItemText();
    int GetCurrentDialogStyle();
    bool isDialogInputStyle(int style) const;
    bool isDialogListStyle(int style) const;
    DialogSubmitResult submitCurrentDialog(
        int button,
        std::optional<int> listItem = std::nullopt,
        std::optional<std::string> inputText = std::nullopt,
        bool alreadyDecoded = false);
    int GetCurrentDialogListboxItemsCount();

    RakClientInterface* getRakClientInterface();

    template <typename Ret, typename... Args>
    static Ret callVirtualMethod(std::uintptr_t instance, std::size_t methodIndex, Args... args) {
        auto* vtable = *reinterpret_cast<std::uintptr_t**>(instance);
        auto fn = reinterpret_cast<Ret(__thiscall*)(void*, Args...)>(vtable[methodIndex]);
        return fn(reinterpret_cast<void*>(instance), std::forward<Args>(args)...);
    }

    bool sendRpc(int id, BitStream& bitStream);
    bool sendPacket(BitStream& bitStream, PacketPriority priority, PacketReliability reliability, char orderingChannel);
    bool sendDialogResponse(
        int dialogId,
        int button,
        int listItem,
        std::string_view inputText = {},
        bool alreadyDecoded = false);

    bool is_chat_opened();
    bool IsChatVisible();
    bool IsScoreboardOpen();
    bool IsServerConnected();
    int Local_ID();
    std::pair<bool, int> getPedID(const void* ped);
    const void* GetPlayerPedPointer(int id, bool trace = false, const char* traceLabel = nullptr);
    bool GetPlayerPosition(int id, float& x, float& y, float& z);
    int getChatMode();
    bool SetPageSize(int pageSize);
    HealthAndArmour GetHealthAndArmour(int id);
    std::optional<std::uint32_t> GetPlayerColor(int id);
    bool send_chat_internal(std::string_view text, bool alreadyDecoded = false);
    bool send_chat(std::string_view text, bool alreadyDecoded = false);
    bool process_chat_input(std::string_view text, bool alreadyDecoded = false);
    bool memoryAddMessageSamp(std::string_view text, std::uint32_t color, bool alreadyDecoded = false);
    ChatEntry pGetChatString(int index);
    bool Set_ChatInputText(std::string_view text, bool openInput = false, bool alreadyDecoded = false);

    bool restoreOriginalFunctionGlobals();
    bool applyFunctionBackend(std::string_view mode);
    bool setFunctionBackendMode(std::string_view mode);
    std::string getFunctionBackendMode() const;
    FunctionBackendStatus getFunctionBackendStatus() const;
    bool installSampfuncsCompat(std::string_view mode);
    void onTerminate();

    HMODULE sampModule() const;
    Version currentVersion() const;
    const char* currentVersionName() const;
    bool isSupportedVersion() const;
    const std::string& lastError() const;

private:
    struct ChatAsiInputDiscovery {
        HMODULE module = nullptr;
        bool attempted = false;
        std::uintptr_t inputLabel = 0;
        std::uintptr_t inputWrapper = 0;
        std::uintptr_t inputBuffer = 0;
        std::uintptr_t inputWriter = 0;
        std::uintptr_t inputSubmit = 0;
        std::uintptr_t inputDirectSend = 0;
    };

    bool DetectVersion();
    void SetError(std::string message);
    void ClearError();
    std::uintptr_t ModuleBase() const;
    std::uintptr_t GetAddress(const VersionedOffset& offset) const;
    std::string PrepareOutgoingText(std::string_view text, bool alreadyDecoded, bool applyTransform) const;
    std::string PrepareIncomingText(std::string_view text) const;
    std::string ApplyTextTransform(std::string_view utf8Text) const;
    std::uintptr_t GetCurrentDialogListBoxPointer();
    int GetCurrentDialogSelectedIndex();
    bool ResolveSampInfo(std::uint32_t& sampInfo) const;
    bool ResolvePedPool(std::uint32_t& pedPool) const;
    bool ResolveRemotePlayer(int id, std::uint32_t& remotePlayer, bool trace = false, const char* traceLabel = nullptr);
    /// Resolves the `CRemotePlayerData*` (or equivalent) pointer used for health/armour and actor fields.
    /// When `SAMP_REMOTEPLAYERDATA_OFFSET` is 0 (legacy 0.3.7 R1 / R3 / R3-1 layouts), those fields live inside
    /// `CRemotePlayer` itself; offset 0 must not be dereferenced as a pointer (that would read `m_pPed`).
    bool ResolveRemotePlayerData(std::uint32_t remotePlayer, std::uint32_t& remoteData) const;
    bool ResolveChat(std::uint32_t& chat) const;
    bool ResolveChatInput(std::uint32_t& chatInput) const;
    bool ResolveDialog(std::uint32_t& dialog) const;
    bool ResolveScoreboard(std::uint32_t& scoreboard) const;
    std::optional<int> GetNetGameState() const;
    bool IsSampReadyByFallback(std::string& reason) const;
    void ResetChatAsiInputDiscovery(HMODULE module = nullptr);
    bool EnsureChatAsiInputDiscovery();
    bool TrySetChatInputTextViaChatAsi(std::string_view utf8Text);
    bool TryProcessChatInputViaChatAsi(std::string_view utf8Text);
    HMODULE sampModule_ = nullptr;
    bool versionResolved_ = false;
    Version currentVersion_ = Version::Unknown;
    const SampEntryPoint* currentEntryPoint_ = nullptr;
    std::uint32_t entryPointAddress_ = 0;
    bool supportedVersion_ = false;
    std::string lastError_;
    TextTransformCallback textTransformCallback_;
    int dialogHookBypassDepth_ = 0;
    std::string functionBackendMode_ = BACKEND_STANDARD;
    std::string functionBackendActive_ = BACKEND_STANDARD;
    ChatAsiInputDiscovery chatAsiInputDiscovery_{};
};
