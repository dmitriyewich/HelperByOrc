#pragma once

#include "tags_module.h"
#include "binder_tag_selector.h"
#include "code_variables.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class CVehicle;
class RakNetBitStreamView;
class SampRakHooks;

class TagsModule::Impl {
public:
    using TagKind = TagsModule::TagKind;
    using CatalogEntry = TagsModule::CatalogEntry;
    using VirtualKeyPickerEntry = TagsModule::VirtualKeyPickerEntry;
    using CursorRange = TagsModule::CursorRange;
    using CursorIntents = TagsModule::CursorIntents;
    using ExpandedText = TagsModule::ExpandedText;
    using DialogInputSetResult = TagsModule::DialogInputSetResult;
    using ExternalTextPath = TagsModule::ExternalTextPath;
    using EvaluationContext = TagsModule::EvaluationContext;
    using OwnedEvaluationContext = TagsModule::OwnedEvaluationContext;

    Impl();

    void OnProcessAttach();
    void Shutdown();
    void ReloadConfig();

    void SetSampApi(SampApi* sampApi);
    void SetSampRakHooks(SampRakHooks* sampRakHooks);
    void SetBinderModule(BinderModule* binderModule);
    void SetNotificationManager(NotificationManager* notificationManager);
    void SetArizonaCefDialogs(ArizonaCefDialogs* arizonaCefDialogs);

    void PushContext(const EvaluationContext& context) const;
    void PopContext() const;

    std::optional<int> ConsumePendingBindDelayOverride(std::uint64_t runtimeId) const;
    bool ConsumeCurrentDispatchBlocked(std::uint64_t runtimeId) const;
    void Tick(bool sampReady);
    bool ExpandExternalTagsEnabled() const;
    void SetExpandExternalTagsEnabled(bool enabled);
    bool ProcessExternalText(std::string& text, ExternalTextPath path) const;
    bool IsMiscHomePage() const;
    void DrawMiscTab();
    std::string ExpandText(std::string_view text) const;
    std::string ExpandText(std::string_view text, const EvaluationContext& context) const;
    ExpandedText ExpandTextWithCursorIntents(std::string_view text) const;
    ExpandedText ExpandTextWithCursorIntents(std::string_view text, const EvaluationContext& context) const;
    DialogInputSetResult SetActiveDialogInputTextAuto(
        std::string_view text,
        const CursorIntents* cursorIntents,
        bool alreadyDecoded) const;
    std::string ExpandHudText(std::string_view text) const;
    std::string ExpandOutgoingText(
        std::string_view text,
        std::string_view activationSource,
        std::string_view activationText) const;
    const std::vector<CatalogEntry>& CatalogEntries() const;
    const std::vector<std::pair<std::string, std::string>>& CustomVariables() const;
    const std::vector<VirtualKeyPickerEntry>& VirtualKeyPickerEntries() const;
    static std::string MakeKeyEmulateToken(unsigned int keyCode);
    void OpenKeyEmulatePicker();
    void OpenDialogItemPicker();
    void OpenArizonaDialogItemPicker();
    void OpenSampDialogTextPicker();
    void OpenArizonaDialogTextPicker();
    void OpenBindSelectorBuilder(std::string_view action);
    void DrawVariableHelperPopups(std::function<void(std::string_view)> tokenAction = {});
    std::vector<variables_picker::Entry> BuildVariablePickerEntriesForInsert() const;
    void HandleVariablePickerUtilityRequest(const variables_picker::Request& request);

    struct TransparentStringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    struct TransparentStringEqual {
        using is_transparent = void;

        bool operator()(std::string_view left, std::string_view right) const noexcept {
            return left == right;
        }
    };

    using StringIndexMap =
        std::unordered_map<std::string, std::size_t, TransparentStringHash, TransparentStringEqual>;

    struct TagEntry {
        using SimpleResolver = std::function<std::optional<std::string>(const Impl&, const EvaluationContext&)>;
        using FunctionResolver =
            std::function<std::optional<std::string>(const Impl&, std::string_view, const EvaluationContext&, int)>;

        TagKind kind = TagKind::Simple;
        CatalogCategory category = CatalogCategory::Text;
        bool action = false;
        std::string name{};
        std::string token{};
        std::string example{};
        UiText descriptionText = UiText::Count;
        SimpleResolver simpleResolver{};
        FunctionResolver functionResolver{};
    };

    class TagRegistry {
    public:
        void Clear();
        void RegisterSimple(
            CatalogCategory category,
            bool action,
            std::string name,
            std::string token,
            std::string example,
            UiText descriptionText,
            TagEntry::SimpleResolver resolver);
        void RegisterFunction(
            CatalogCategory category,
            bool action,
            std::string name,
            std::string token,
            std::string example,
            UiText descriptionText,
            TagEntry::FunctionResolver resolver);

        const std::vector<TagEntry>& Entries() const;
        const TagEntry* Find(TagKind kind, std::string_view name) const;
        const TagEntry* FindByIndex(int index) const;
        std::size_t Count(TagKind kind) const;

    private:
        std::vector<TagEntry> entries_{};
        StringIndexMap simpleIndex_{};
        StringIndexMap functionIndex_{};
    };

    enum class MiscPage {
        Home = 0,
        Variables,
    };

    enum class MyCarOccupantScope {
        Players,
        Passengers,
        AllPlayers,
        AllPassengers,
    };

    enum class MyCarOccupantField {
        Id,
        Name,
        Surname,
        Nick,
        RpNick,
    };

    enum class CursorTarget {
        SampChat,
        ArizonaChat,
        SampDialog,
        ArizonaDialog,
    };

    struct ActiveVirtualKeyHold {
        unsigned int keyCode = 0;
        std::uint64_t pressAtMs = 0;
        std::uint64_t releaseAtMs = 0;
        bool pressed = false;
    };

    struct PendingBindDelayOverride {
        std::uint64_t runtimeId = 0;
        int delayMs = 0;
    };

    struct PendingKeyHoldWait {
        std::uint64_t runtimeId = 0;
        unsigned int keyCode = 0;
        std::uint64_t releaseAtMs = 0;
    };

    struct TargetTrackerState {
        int currentId = -1;
        int lastId = -1;
        std::uintptr_t lastLoggedPed = 0;
        int lastLoggedResolvedId = -1;
        bool lastLoggedValid = false;
        bool sessionActive = false;
    };

    enum class PendingDialogWaitKind {
        Open,
        Close,
        SpecificId,
    };

    enum class DialogTextPickerSource {
        Samp,
        Arizona,
    };

    enum class DialogItemPickerSource {
        Samp,
        Arizona,
    };

    struct BindSelectorBuilderState {
        bool openPending = false;
        std::string action{ "bindstart" };
        binder_tags::Catalog catalog{};
        int categoryIndex = 0;
        int folderChoice = -1;
        int bindIndex = -1;
        int outputMode = 0;
        int randomScope = 0;
        std::string search{};
    };

    struct PendingDialogWait {
        std::uint64_t runtimeId = 0;
        PendingDialogWaitKind kind = PendingDialogWaitKind::Open;
        std::uint64_t deadlineAtMs = 0;
        int expectedDialogId = -1;
    };

    enum class PendingArzDialogQueryKind {
        InputText,
        ListItem,
        ListItems,
    };

    struct PendingArzDialogQueryWait {
        std::uint64_t runtimeId = 0;
        PendingArzDialogQueryKind kind = PendingArzDialogQueryKind::InputText;
        std::uint64_t deadlineAtMs = 0;
    };

    struct ReadyArzDialogQuery {
        std::uint64_t runtimeId = 0;
        PendingArzDialogQueryKind kind = PendingArzDialogQueryKind::InputText;
    };

    struct ClosestPlayerQueryResult {
        int nearestId = -1;
        int nearestToCenterId = -1;
        int nearestDriverId = -1;
        int viewportWidth = 0;
        int viewportHeight = 0;
        float nearestDistanceSq = -1.0f;
        float nearestToCenterDistanceSq = -1.0f;
        float nearestDriverDistanceSq = -1.0f;
        float screenCenterX = -1.0f;
        float screenCenterY = -1.0f;
        float nearestToCenterScreenX = -1.0f;
        float nearestToCenterScreenY = -1.0f;
    };

    struct ClosestPlayerDetails {
        std::string nick{};
        std::string color{};
        std::string vehicle{};
        bool nickResolved = false;
        bool colorResolved = false;
        bool vehicleResolved = false;
    };

    struct ClosestPlayerQueryStats {
        std::size_t candidates = 0;
        std::size_t driverCandidates = 0;
        std::size_t notStreamed = 0;
        std::size_t invalidPed = 0;
        std::size_t invalidPosition = 0;
        std::size_t projectionFailed = 0;
        std::size_t offscreen = 0;
    };

    struct ClosestPlayerCache {
        ClosestPlayerQueryResult result{};
        ClosestPlayerQueryResult lastLoggedResult{};
        ClosestPlayerDetails nearestDetails{};
        ClosestPlayerDetails driverDetails{};
        std::uint64_t tickGeneration = 0;
        std::uint64_t updatedAtMs = 0;
        std::uint64_t lastSlowLogAtMs = 0;
        std::uintptr_t localPed = 0;
        int viewportWidth = 0;
        int viewportHeight = 0;
        int localId = -1;
        bool valid = false;
        bool snapshotLogged = false;
    };

    struct MyCarSnapshotOccupant {
        int id = -1;
        bool passenger = false;
        bool localPlayer = false;
        bool nickResolved = false;
        std::string nick{};
        std::string name{};
        std::string surname{};
        std::string rpNick{};
    };

    struct MyCarOccupantCollectStats {
        std::size_t driverSlots = 0;
        std::size_t passengerSlots = 0;
        std::size_t validSlots = 0;
        std::size_t skippedInvalidPed = 0;
        std::size_t skippedSeatMismatch = 0;
        std::size_t skippedDuplicate = 0;
        std::size_t skippedUnresolvedId = 0;
        std::uint64_t maxIdResolveMs = 0;
        std::uintptr_t slowIdResolvePed = 0;
    };

    struct MyCarSnapshotCache {
        std::vector<MyCarSnapshotOccupant> occupants{};
        std::string name{};
        std::string health{};
        std::string speed{};
        std::string window{};
        std::string modelId{};
        std::string primaryColor{};
        std::string secondaryColor{};
        std::string engine{};
        std::string lights{};
        std::string sirenOrAlarm{};
        std::string seat{};
        MyCarOccupantCollectStats occupantStats{};
        std::uint64_t tickGeneration = 0;
        std::uint64_t updatedAtMs = 0;
        std::uint64_t lastSlowLogAtMs = 0;
        std::uintptr_t localPed = 0;
        std::uintptr_t vehicle = 0;
        int localId = -1;
        bool valid = false;
        bool hasVehicle = false;
        bool sampReady = false;
        bool occupantsResolved = false;
        bool nameResolved = false;
        bool windowResolved = false;
    };

    struct OnlineCountCache {
        std::uintptr_t playerPool = 0;
        std::uint64_t updatedAtMs = 0;
        int value = 0;
        bool valid = false;
    };

    struct MyCarSampIdCache {
        std::uintptr_t sessionKey = 0;
        std::uintptr_t vehicle = 0;
        std::uint64_t updatedAtMs = 0;
        int value = -1;
        bool valid = false;
    };

    struct DamagePlayerSnapshot {
        int id = -1;
        std::string name{};
        std::string surname{};
        bool valid = false;
    };

    enum class DamagePlayerField : std::uint8_t {
        Id,
        Name,
        Surname,
    };

    struct VehicleNameCacheEntry {
        std::array<char, 9> gxtKey{};
        std::string displayName{};
    };

    struct MyCarSnapshotPerfStats {
        std::uint64_t windowStartMs = 0;
        std::uint64_t requests = 0;
        std::uint64_t cacheHits = 0;
        std::uint64_t rebuilds = 0;
        std::uint64_t occupantRequests = 0;
        std::uint64_t occupantRebuilds = 0;
        std::uint64_t noVehicle = 0;
        std::uint64_t noSamp = 0;
        std::uint64_t totalRebuildMs = 0;
        std::uint64_t maxRebuildMs = 0;
        std::size_t maxOccupants = 0;
        std::uint64_t nameRequests = 0;
        std::uint64_t nameCacheHits = 0;
        std::uint64_t nameResolved = 0;
        std::uint64_t totalNameMs = 0;
        std::uint64_t maxNameMs = 0;
    };

    struct ClosestPlayerPerfStats {
        std::uint64_t windowStartMs = 0;
        std::uint64_t requests = 0;
        std::uint64_t cacheHits = 0;
        std::uint64_t rebuilds = 0;
        std::uint64_t noSamp = 0;
        std::uint64_t noLocal = 0;
        double totalRebuildMs = 0.0;
        double maxRebuildMs = 0.0;
        std::size_t maxCandidates = 0;
        std::size_t maxDriverCandidates = 0;
        std::uint64_t notStreamed = 0;
        std::uint64_t invalidPed = 0;
        std::uint64_t invalidPosition = 0;
        std::uint64_t projectionFailed = 0;
        std::uint64_t offscreen = 0;
    };

    struct PlayerNamePerfStats {
        std::uint64_t windowStartMs = 0;
        std::uint64_t calls = 0;
        std::uint64_t failed = 0;
        std::uint64_t unknown = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;
    };

    struct ClipboardCache {
        std::string text{};
        std::uint32_t sequenceNumber = 0;
        bool valid = false;
    };

    using TransliterationDictionaryMap =
        std::unordered_map<std::string, std::string, TransparentStringHash, TransparentStringEqual>;

    struct TransliterationDictionarySnapshot {
        TransliterationDictionaryMap latinToCyrillic{};
        TransliterationDictionaryMap cyrillicToLatin{};
    };

    enum class TransliterationDictionaryState {
        Missing,
        Loaded,
        LoadedWithWarnings,
        Error,
    };

    struct TransliterationDictionaryStatus {
        TransliterationDictionaryState state = TransliterationDictionaryState::Missing;
        std::size_t loadedPairs = 0;
        std::size_t invalidLines = 0;
        std::size_t duplicateLines = 0;
        std::size_t conflictLines = 0;
        std::size_t limitLines = 0;
    };

    enum class TagPerfSource {
        Unknown,
        Hud,
        Binder,
        Outgoing,
        External,
        Notepad,
        Ui,
        Count,
    };

    enum class TagPerfGroup {
        Builtin,
        Custom,
        Code,
        MyCar,
        Closest,
        Dialog,
        Arizona,
        Action,
        Count,
    };

    struct TagExpansionTrace {
        bool telemetryEnabled = false;
        TagPerfSource source = TagPerfSource::Unknown;
        std::size_t inputBytes = 0;
        std::size_t outputBytes = 0;
        std::uint64_t simpleTags = 0;
        std::uint64_t functionTags = 0;
        std::uint64_t customTags = 0;
        std::uint64_t actionTags = 0;
        std::uint64_t unresolvedTags = 0;
        std::uint64_t recursionLimitHits = 0;
        std::array<std::uint64_t, static_cast<std::size_t>(TagPerfGroup::Count)> groupCounts{};
        TagKind hotTagKind = TagKind::Simple;
        std::string hotTagName{};
        double hotTagMs = 0.0;
        int maxDepth = 0;
        codevars::ExpansionCache codeCache{};
    };

    struct TagExpansionPerfBucket {
        std::uint64_t calls = 0;
        std::uint64_t slowCalls = 0;
        std::uint64_t inputBytes = 0;
        std::uint64_t outputBytes = 0;
        std::uint64_t simpleTags = 0;
        std::uint64_t functionTags = 0;
        std::uint64_t customTags = 0;
        std::uint64_t actionTags = 0;
        std::uint64_t unresolvedTags = 0;
        std::uint64_t recursionLimitHits = 0;
        double totalMs = 0.0;
        double maxMs = 0.0;
        std::array<std::uint64_t, static_cast<std::size_t>(TagPerfGroup::Count)> groupCounts{};
    };

    struct TagExpansionPerfStats {
        std::uint64_t windowStartMs = 0;
        std::array<TagExpansionPerfBucket, static_cast<std::size_t>(TagPerfSource::Count)> buckets{};
    };

    struct ExternalTagPerfStats {
        std::atomic_uint64_t send{};
        std::atomic_uint64_t local{};
        std::atomic_uint64_t changed{};
        std::atomic_uint64_t suppressed{};
        std::atomic_uint64_t bypassed{};
        std::atomic_uint64_t failed{};
        std::uint64_t windowStartMs = 0;
        std::atomic_uint64_t lastFailureLogAtMs{};
    };

    void InitializeRegistry();
    void LoadConfig();
    void SaveConfig() const;
    void LoadTransliterationDictionary();
    bool OpenTransliterationDictionaryFile();
    bool HasTransliterationDictionary() const;
    const std::string* FindLatinDictionaryWord(std::string_view normalizedWord) const;
    const std::string* FindCyrillicDictionaryWord(std::string_view normalizedWord) const;
    void DrawMiscHomePage();
    void DrawVariablesPage();
    void DrawTransliterationDictionaryCard();
    static void DrawVariablePickerInspectorExtra(void* context, const variables_picker::Entry& entry);
    const std::vector<variables_picker::Entry>& BuildVariablePickerEntries() const;
    void HandleVariablePickerRequest(const variables_picker::Request& request);
    bool UpsertCustomVariable(std::string originalName, std::string name, std::string value);
    bool DeleteCustomVariable(std::string_view name);
    std::string ValidateCustomVariableName(std::string_view originalName, std::string_view name) const;
    void RefreshCatalogEntries();
    void EnsureCodeCatalogEntries() const;
    void RefreshCodeVariableReservedNames();
    void DrawLuaVariablesTab();
    void RebuildCustomVariableIndex();
    void InvalidateVariablePickerEntriesCache() const;
    void OpenDialogItemPicker(DialogItemPickerSource source);
    void OpenDialogTextPicker(DialogTextPickerSource source = DialogTextPickerSource::Samp);
    void DrawDialogItemPickerPopup(const std::function<void(std::string_view)>& tokenAction = {});
    void DrawDialogTextPickerPopup(const std::function<void(std::string_view)>& tokenAction = {});
    void DrawBindSelectorBuilderPopup(const std::function<void(std::string_view)>& tokenAction = {});
    void ProcessPendingKeyHoldWaits();
    void ProcessPendingDialogWaits();
    void ProcessPendingArzDialogQueryWaits();
    void QueuePendingDialogWait(
        std::uint64_t runtimeId,
        PendingDialogWaitKind kind,
        std::uint64_t deadlineAtMs,
        int expectedDialogId = -1);
    void ClearPendingDialogWait(std::uint64_t runtimeId);
    void NotifyTagError(std::string_view text, double durationMs = 2800.0) const;
    void NotifyDialogError(std::string_view text, double durationMs = 2800.0) const;
    void NotifyTagSuccess(std::string_view text, double durationMs = 2200.0) const;
    void NotifyClipboardSuccess(std::string_view text, double durationMs = 2200.0) const;
    void QueuePendingKeyHoldWait(std::uint64_t runtimeId, unsigned int keyCode, std::uint64_t releaseAtMs) const;
    void ClearPendingKeyHoldWaitsByKeyCode(unsigned int keyCode) const;
    bool HasPendingDialogWait(std::uint64_t runtimeId) const;
    bool HasPendingKeyHoldWait(std::uint64_t runtimeId) const;
    void QueuePendingArzDialogQueryWait(
        std::uint64_t runtimeId,
        PendingArzDialogQueryKind kind,
        std::uint64_t deadlineAtMs) const;
    bool HasPendingArzDialogQueryWait(std::uint64_t runtimeId) const;
    bool ConsumeReadyArzDialogQuery(std::uint64_t runtimeId, PendingArzDialogQueryKind kind) const;
    void MarkCurrentDispatchBlocked(std::uint64_t runtimeId) const;

    std::string ExpandTextRecursive(std::string_view text, const EvaluationContext& context, int depth) const;
    std::string ExpandTextRecursive(
        std::string_view text,
        const EvaluationContext& context,
        int depth,
        TagExpansionTrace* trace) const;
    std::string ExpandFunctionTags(
        std::string_view text,
        const EvaluationContext& context,
        int depth,
        TagExpansionTrace* trace) const;
    std::string ExpandSimpleTags(std::string_view text, const EvaluationContext& context, TagExpansionTrace* trace) const;
    bool TryGetCursorTarget(std::string_view normalizedName, CursorTarget& target) const;
    std::optional<std::pair<int, int>> ParseCursorFunctionRange(std::string_view param) const;
    static std::string MakeCursorFunctionSentinel(CursorTarget target, int start, int finish);
    static bool TryParseCursorFunctionSentinel(std::string_view name, CursorTarget& target, int& start, int& finish);
    static int CursorPositionForOutput(CursorTarget target, const std::string& output);
    void RecordCursorMarker(CursorTarget target, const std::string& currentOutput, const EvaluationContext& context) const;
    void RecordCursorRange(CursorTarget target, int start, int finish, const EvaluationContext& context) const;

    std::optional<std::string> ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const;
    std::optional<std::string> ResolveSimpleTagNormalized(
        std::string_view normalizedName,
        const EvaluationContext& context,
        TagExpansionTrace* trace) const;
    std::optional<std::string> ResolveFunctionTag(
        std::string_view name,
        std::string_view param,
        const EvaluationContext& context,
        int depth,
        TagExpansionTrace* trace) const;
    std::optional<std::string> ResolveBuiltinIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThisbindTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThisbindNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThisbindFolderTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinThiscategoryTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinBindStopAllTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetRpNickTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetHealthTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetArmourTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestIdToCenterTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestColorTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDriverCarTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDriverColorTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDriverIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDriverNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDriverSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArmourTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHealthTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinPingTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinScoreTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinOnlineTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyXTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyYTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyZTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyPosTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyDirectionShortTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyDirectionTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyDirectionShortEnTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyDirectionEnTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMySquareTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMySquareEnTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCityTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCityEnTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClipboardTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyColorTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarHealthTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarSpeedTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarWindowTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarModelIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarColor1Tag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarColor2Tag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarEngineTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarLightsTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarSirenOrAlarmTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarSeatTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyStaminaTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyOxygenTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinWeatherTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinWeatherEnTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTargetDistanceTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDistanceTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClosestDriverDistanceTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHitMeIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHitMeNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHitMeSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHitByMeIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHitByMeNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHitByMeSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyWantedTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyInteriorTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyHeadingTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinGameTimeTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinWeatherIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyCarOccupantsTag(
        MyCarOccupantScope scope,
        MyCarOccupantField field,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDateTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMySkinTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyWeaponTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyWeaponIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyWeaponClipTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyWeaponAmmoTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMyMoneyTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinFpsTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinGetVehTypeTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinScreenTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTPhotoTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinChatClearTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCursorMarkerTag(CursorTarget target, const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickRpTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSurnameTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTimeTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTimeNoSecTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogActiveTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogCaptionTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogGetSelectedItemTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogEditboxTextTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogSelectedIndexTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogWaitOpenTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogWaitCloseTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogGetIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetInputTextTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetListItemTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogIsDialogActiveTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetStyleTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetTitleTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetButton1Tag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetButton2Tag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetDialogTextTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetDialogTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetRespondTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogRespondIdTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogRespondButtonTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogRespondListTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogRespondInputTag(const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickFunctionTag(std::string_view param, const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinRpNickFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNameFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSurnameFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinParamcmdFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinKeyEmulateFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinMathFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNumberWithDotsFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCyrToLatFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinLatToCyrFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinToRomanFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinFromRomanFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArmourFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinHealthFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinPingFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinScoreFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDistanceFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSkinFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinNickColorFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCarFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCarHealthFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCarWindowFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinKeyDownFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinStrLowFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinStrUpperFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinTrimFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSubstrFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSubstrFunctionParts(
        std::string_view text,
        std::string_view start,
        std::optional<std::string_view> length) const;
    std::optional<std::string> ResolveBuiltinStrlenFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinClipboardSetFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinAddTimeFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinRandomFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinIfAndOrFunctionTag(
        std::string_view rawParam,
        const EvaluationContext& context,
        int depth) const;
    std::optional<std::string> ResolveBuiltinTimefFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinGetVehTypeFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinScreenFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinWaitFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinCursorFunctionTag(
        CursorTarget target,
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogCloseFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogSetTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogItemFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogSelectFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogWaitIdFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinDialogResponseFunctionTag(
        std::string_view rawParam,
        const EvaluationContext& context,
        int depth) const;
    std::optional<std::string> ResolveBuiltinDialogTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinSaveDialogFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogSetInputTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetInputTextFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogCloseWithButtonFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogSetListItemFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogItemFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogGetListItemFunctionTag(
        std::string_view param,
        const EvaluationContext& context) const;
    std::optional<std::string> ResolveBuiltinArzDialogSendRespondFunctionTag(
        std::string_view rawParam,
        const EvaluationContext& context,
        int depth) const;
    std::optional<std::string> ResolveBinderActionFunctionTag(
        std::string_view action,
        std::string_view param,
        const EvaluationContext& context) const;

    EvaluationContext ResolveActiveContext(std::string_view defaultSource = {}, std::string_view defaultText = {}) const;
    void DrawKeyEmulatePickerPopup();
    void UpdateTargetTracker();
    void ResetTargetTracker();
    void RefreshSampSessionState(bool sampReady);
    bool HandleOutgoingDamageRpc(std::uint8_t rpcId, RakNetBitStreamView& view);
    std::string ResolveDamagePlayerValue(bool hitMe, DamagePlayerField field) const;
    std::string ResolveVehicleDisplayName(const CVehicle* vehicle) const;
    ClosestPlayerCache& QueryClosestPlayers(const EvaluationContext& context) const;
    void ResolveClosestPlayerDetails(
        ClosestPlayerCache& snapshot,
        bool driver,
        bool requireNick,
        bool requireColor,
        bool requireVehicle,
        const EvaluationContext& context) const;
    void MaybeLogClosestPlayersSnapshot(
        ClosestPlayerCache& snapshot,
        const ClosestPlayerQueryStats& queryStats,
        double elapsedMs,
        const EvaluationContext& context) const;
    MyCarSnapshotCache& QueryMyCarSnapshot(const EvaluationContext& context, bool requireOccupants) const;
    std::optional<int> ResolveMyCarSampId(const MyCarSnapshotCache& snapshot, const EvaluationContext& context) const;
    void ResolveMyCarOccupantNames(MyCarSnapshotCache& snapshot, const EvaluationContext& context) const;
    void RecordMyCarSnapshotPerf(
        bool cacheHit,
        bool requireOccupants,
        bool hasVehicle,
        bool sampReady,
        std::size_t occupants,
        std::uint64_t elapsedMs) const;
    void RecordMyCarNameResolvePerf(std::size_t requested, std::size_t resolved, std::uint64_t elapsedMs) const;
    void MaybeLogMyCarPerf(std::uint64_t nowMs) const;
    void RecordClosestPlayersPerf(
        bool cacheHit,
        bool sampReady,
        bool localReady,
        const ClosestPlayerQueryStats& queryStats,
        double elapsedMs) const;
    void MaybeLogClosestPlayersPerf(std::uint64_t nowMs) const;
    void RecordPlayerNamePerf(bool failed, bool unknown, double elapsedMs) const;
    void MaybeLogPlayerNamePerf(std::uint64_t nowMs) const;
    TagPerfSource ResolveTagPerfSource(const EvaluationContext& context) const;
    static const char* TagPerfSourceName(TagPerfSource source);
    static const char* TagPerfGroupName(TagPerfGroup group);
    static const char* TagKindPerfName(TagKind kind);
    static TagPerfGroup ClassifyTagPerfGroup(std::string_view normalizedName, bool action);
    static TagPerfGroup DominantTagPerfGroup(const TagExpansionTrace& trace);
    void RecordTagGroup(TagExpansionTrace& trace, TagPerfGroup group) const;
    void RecordHotTag(TagExpansionTrace& trace, TagKind kind, std::string_view name, double elapsedMs) const;
    void RecordTagExpansionPerf(const TagExpansionTrace& trace, double elapsedMs) const;
    void MaybeLogTagExpansionPerf(std::uint64_t nowMs) const;
    void MaybeLogExternalTagPerf(std::uint64_t nowMs) const;
    std::string ResolvePlayerNickById(int id, const EvaluationContext& context) const;
    std::string ResolveLocalNick(const EvaluationContext& context) const;
    std::string ResolveLastTargetNick(const EvaluationContext& context) const;
    static std::string FormatCurrentTime(std::string_view format);
    static std::string FormatWholeStatValue(float value);
    static std::string MakeRpNick(std::string_view nick);
    static std::string ExtractName(std::string_view nick);
    static std::string ExtractSurname(std::string_view nick);

    static std::string_view TrimView(std::string_view value);
    static std::string Trim(std::string_view value);
    static std::string ToLower(std::string_view value);
    static bool StartsWith(std::string_view value, std::string_view prefix);
    static std::vector<std::string> SplitCommandArgs(std::string_view value);
    static std::optional<int> ParseInteger(std::string_view value);
    static OwnedEvaluationContext MakeOwnedContext(const EvaluationContext& context, SampApi* fallbackSampApi);
    static EvaluationContext MakeViewContext(const OwnedEvaluationContext& context);
    std::uint64_t QueueVirtualKeyHold(unsigned int keyCode, int startDelayMs, int holdDurationMs) const;
    void ReleaseVirtualKeyHold(ActiveVirtualKeyHold& hold) const;
    void QueuePendingBindDelayOverride(std::uint64_t runtimeId, int delayMs) const;

private:
    SampApi* sampApi_ = nullptr;
    SampRakHooks* sampRakHooks_ = nullptr;
    BinderModule* binderModule_ = nullptr;
    NotificationManager* notificationManager_ = nullptr;
    ArizonaCefDialogs* arizonaCefDialogs_ = nullptr;
    MiscPage currentPage_ = MiscPage::Home;
    std::atomic_bool expandExternalTags_{ true };
    TagRegistry tagRegistry_{};
    std::vector<CatalogEntry> builtinCatalogEntries_{};
    mutable std::vector<CatalogEntry> catalogEntries_{};
    std::vector<std::pair<std::string, std::string>> customVariables_{};
    StringIndexMap customVariableIndex_{};
    std::uint64_t catalogEntriesRevision_ = 0;
    std::uint64_t customVariablesRevision_ = 0;
    mutable std::uint64_t codeCatalogRevision_ = 0;
    mutable std::vector<variables_picker::Entry> variablePickerEntriesCache_{};
    mutable std::uint64_t variablePickerEntriesCatalogRevision_ = 0;
    mutable std::uint64_t variablePickerEntriesCustomRevision_ = 0;
    mutable std::uint64_t variablePickerEntriesCodeRevision_ = 0;
    bool reloadingConfig_ = false;
    variables_picker::State variablesPickerState_{};
    std::string luaHostInstallMessage_{};
    bool luaHostInstallFailed_ = false;
    mutable std::vector<ActiveVirtualKeyHold> activeVirtualKeyHolds_{};
    mutable std::vector<PendingBindDelayOverride> pendingBindDelayOverrides_{};
    mutable std::vector<PendingKeyHoldWait> pendingKeyHoldWaits_{};
    mutable std::vector<PendingArzDialogQueryWait> pendingArzDialogQueryWaits_{};
    mutable std::vector<ReadyArzDialogQuery> readyArzDialogQueries_{};
    mutable std::vector<std::uint64_t> blockedCurrentDispatchRuntimes_{};
    std::uint64_t tickGeneration_ = 1;
    mutable ClosestPlayerCache closestPlayerCache_{};
    mutable ClosestPlayerPerfStats closestPlayerPerfStats_{};
    mutable PlayerNamePerfStats playerNamePerfStats_{};
    mutable MyCarSnapshotCache myCarSnapshotCache_{};
    mutable OnlineCountCache onlineCountCache_{};
    mutable MyCarSampIdCache myCarSampIdCache_{};
    mutable MyCarSnapshotPerfStats myCarSnapshotPerfStats_{};
    mutable std::unordered_map<int, VehicleNameCacheEntry> vehicleNameCache_{};
    mutable TagExpansionPerfStats tagExpansionPerfStats_{};
    mutable ExternalTagPerfStats externalTagPerfStats_{};
    mutable std::uint64_t lastTagExpansionSlowLogAtMs_ = 0;
    mutable ClipboardCache clipboardCache_{};
    mutable std::mutex damageStateMutex_{};
    DamagePlayerSnapshot hitMeSnapshot_{};
    DamagePlayerSnapshot hitByMeSnapshot_{};
    std::uintptr_t sampSessionKey_ = 0;
    std::uint64_t nextSampSessionProbeAtMs_ = 0;
    bool sampSessionActive_ = false;
    bool sampRakHandlerAttached_ = false;
    TransliterationDictionarySnapshot transliterationDictionary_{};
    TransliterationDictionaryStatus transliterationDictionaryStatus_{};
    std::filesystem::path transliterationDictionaryPath_{};
    std::string transliterationDictionaryPathUtf8_{};
    bool transliterationDictionaryOpenFailed_ = false;
    std::vector<PendingDialogWait> pendingDialogWaits_{};
    TargetTrackerState targetTracker_{};
    std::string keyPickerSearchQuery_{};
    std::string dialogItemPickerSearchQuery_{};
    std::string dialogTextPickerSearchQuery_{};
    DialogItemPickerSource dialogItemPickerSource_ = DialogItemPickerSource::Samp;
    DialogTextPickerSource dialogTextPickerSource_ = DialogTextPickerSource::Samp;
    bool dialogItemPickerOpenPending_ = false;
    bool dialogItemPickerArizonaQueryStarted_ = false;
    bool dialogTextPickerOpenPending_ = false;
    BindSelectorBuilderState bindSelectorBuilder_{};
};
