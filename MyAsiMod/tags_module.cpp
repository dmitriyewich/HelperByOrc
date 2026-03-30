#include "tags_module.h"

#include "app_config.h"
#include "json_utils.h"
#include "samp_api.h"
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kTagsSectionName = "tags";
constexpr std::string_view kCustomVarsKey = "custom_vars";
constexpr int kPreviewLaunchManual = 0;
constexpr int kPreviewLaunchCommand = 1;
constexpr int kRecursionLimit = 10;
thread_local std::vector<TagsModule::OwnedEvaluationContext> g_activeContextStack;

struct ImGuiStringUserData {
    std::string* value = nullptr;
    ImGuiInputTextCallback chain = nullptr;
    void* chainUserData = nullptr;
};

int ImGuiStringResizeCallback(ImGuiInputTextCallbackData* data) {
    auto* userData = static_cast<ImGuiStringUserData*>(data->UserData);
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        IM_ASSERT(userData && userData->value);
        userData->value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = userData->value->data();
        return 0;
    }
    if (userData && userData->chain) {
        data->UserData = userData->chainUserData;
        return userData->chain(data);
    }
    return 0;
}

bool InputTextString(const char* label, std::string& value, ImGuiInputTextFlags flags = 0, std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputText(label, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

bool InputTextWithHintString(
    const char* label,
    const char* hint,
    std::string& value,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 256) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextWithHint(label, hint, value.data(), value.capacity() + 1, flags, ImGuiStringResizeCallback, &userData);
}

bool InputTextMultilineString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 1024) {
    if (value.capacity() < minBuffer) {
        value.reserve(minBuffer);
    }

    ImGuiStringUserData userData{ &value, nullptr, nullptr };
    flags |= ImGuiInputTextFlags_CallbackResize;
    return ImGui::InputTextMultiline(label, value.data(), value.capacity() + 1, size, flags, ImGuiStringResizeCallback, &userData);
}

std::size_t CountUtf8Codepoints(std::string_view value) {
    std::size_t count = 0;
    for (const unsigned char ch : value) {
        if ((ch & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

bool InputTextMultilineWithCounterString(
    const char* label,
    std::string& value,
    const ImVec2& size,
    ImGuiInputTextFlags flags = 0,
    std::size_t minBuffer = 1024) {
    const bool changed = InputTextMultilineString(label, value, size, flags, minBuffer);
    const std::string counter = std::to_string(CountUtf8Codepoints(value));
    const float counterWidth = ImGui::CalcTextSize(counter.c_str()).x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    if (availableWidth > counterWidth) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - counterWidth));
    }
    ImGui::TextDisabled("%s", counter.c_str());
    return changed;
}

float ScaleUi(float value) {
    return UiSettings::Instance().Scale(value);
}

ImVec2 ScaleUi(float x, float y) {
    return UiSettings::Instance().Scale(ImVec2(x, y));
}

const char* TagKindLabel(TagsModule::TagKind kind, UiSettings& ui) {
    return ui.Text(kind == TagsModule::TagKind::Simple ? UiText::TagsKindSimple : UiText::TagsKindFunction);
}

void DrawStatBlock(const char* label, const std::string& value, const ImVec4& accent) {
    ImGui::BeginGroup();
    ImGui::TextColored(accent, "%s", value.c_str());
    ImGui::TextDisabled("%s", label);
    ImGui::EndGroup();
}

ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t);
}

bool DrawNavigationCardButton(
    const char* id,
    const char* title,
    const char* description,
    const char* actionLabel,
    const ImVec4& accent,
    float height) {
    const float cardHeight = ScaleUi(height);
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImVec2 screenMax(screenMin.x + ImGui::GetContentRegionAvail().x, screenMin.y + cardHeight);
    const bool hovered = ImGui::IsMouseHoveringRect(screenMin, screenMax);
    const bool held = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    const ImVec4 baseBg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    const ImVec4 hoverBg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    const ImVec4 activeBg = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    const ImVec4 childBg = held
        ? LerpColor(baseBg, activeBg, 0.45f)
        : hovered ? LerpColor(baseBg, hoverBg, 0.35f) : baseBg;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, childBg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ScaleUi(14.0f));
    const bool began = ImGui::BeginChild(id, ImVec2(0.0f, cardHeight), ImGuiChildFlags_FrameStyle);
    if (began) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 childMin = ImGui::GetWindowPos();
        const ImVec2 childMax(childMin.x + ImGui::GetWindowSize().x, childMin.y + ImGui::GetWindowSize().y);
        drawList->AddRectFilled(
            childMin,
            ImVec2(childMin.x + ScaleUi(6.0f), childMax.y),
            ImGui::GetColorU32(accent),
            ScaleUi(14.0f),
            ImDrawFlags_RoundCornersLeft);

        ImGui::SetCursorPos(ScaleUi(20.0f, 18.0f));
        ImGui::TextColored(accent, "%s", title);
        ImGui::Spacing();

        ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - ScaleUi(20.0f));
        ImGui::TextWrapped("%s", description);
        ImGui::PopTextWrapPos();

        const ImVec2 actionSize = ImGui::CalcTextSize(actionLabel);
        ImGui::SetCursorPosX(std::max(ScaleUi(20.0f), ImGui::GetWindowWidth() - actionSize.x - ScaleUi(20.0f)));
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - ScaleUi(34.0f)));
        ImGui::TextDisabled("%s", actionLabel);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    return ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);
}

} // namespace

TagsModule::TagsModule() = default;

void TagsModule::TagRegistry::Clear() {
    entries_.clear();
}

void TagsModule::TagRegistry::RegisterSimple(
    std::string name,
    std::string token,
    std::string example,
    UiText descriptionText,
    TagEntry::SimpleResolver resolver) {
    entries_.push_back(TagEntry{
        TagKind::Simple,
        std::move(name),
        std::move(token),
        std::move(example),
        descriptionText,
        std::move(resolver),
        {},
    });
}

void TagsModule::TagRegistry::RegisterFunction(
    std::string name,
    std::string token,
    std::string example,
    UiText descriptionText,
    TagEntry::FunctionResolver resolver) {
    entries_.push_back(TagEntry{
        TagKind::Function,
        std::move(name),
        std::move(token),
        std::move(example),
        descriptionText,
        {},
        std::move(resolver),
    });
}

const std::vector<TagsModule::TagEntry>& TagsModule::TagRegistry::Entries() const {
    return entries_;
}

const TagsModule::TagEntry* TagsModule::TagRegistry::Find(TagKind kind, std::string_view name) const {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const TagEntry& entry) {
        return entry.kind == kind && entry.name == name;
    });
    return it == entries_.end() ? nullptr : &(*it);
}

const TagsModule::TagEntry* TagsModule::TagRegistry::FindByIndex(int index) const {
    if (index < 0 || index >= static_cast<int>(entries_.size())) {
        return nullptr;
    }
    return &entries_[static_cast<std::size_t>(index)];
}

std::size_t TagsModule::TagRegistry::Count(TagKind kind) const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [&](const TagEntry& entry) {
        return entry.kind == kind;
    }));
}

void TagsModule::InitializeRegistry() {
    tagRegistry_.Clear();

    tagRegistry_.RegisterSimple(
        "id",
        "{id}",
        "{id}",
        UiText::TagsBuiltinIdDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinIdTag(context);
        });

    tagRegistry_.RegisterSimple(
        "nick",
        "{nick}",
        "{nick}",
        UiText::TagsBuiltinNickDescription,
        [](const TagsModule& module, const EvaluationContext& context) {
            return module.ResolveBuiltinNickTag(context);
        });

    tagRegistry_.RegisterFunction(
        "nick",
        "[nick(...)]",
        "[nick(15)]",
        UiText::TagsBuiltinNickFunctionDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinNickFunctionTag(param, context);
        });

    tagRegistry_.RegisterFunction(
        "paramcmd",
        "[paramcmd(...)]",
        "[paramcmd(1+)]",
        UiText::TagsBuiltinParamcmdDescription,
        [](const TagsModule& module, std::string_view param, const EvaluationContext& context) {
            return module.ResolveBuiltinParamcmdFunctionTag(param, context);
        });
}

void TagsModule::OnProcessAttach() {
    InitializeRegistry();
    LoadConfig();
    currentPage_ = MiscPage::Home;
    if (selectedTagIndex_ < 0 || selectedTagIndex_ >= static_cast<int>(tagRegistry_.Entries().size())) {
        selectedTagIndex_ = 0;
    }
}

void TagsModule::Shutdown() {
    searchQuery_.clear();
    currentPage_ = MiscPage::Home;
    g_activeContextStack.clear();
}

void TagsModule::SetSampApi(SampApi* sampApi) {
    sampApi_ = sampApi;
}

TagsModule::OwnedEvaluationContext TagsModule::MakeOwnedContext(const EvaluationContext& context, SampApi* fallbackSampApi) {
    OwnedEvaluationContext owned;
    owned.sampApi = context.sampApi ? context.sampApi : fallbackSampApi;
    owned.activationSource = std::string(context.activationSource);
    owned.activationText = std::string(context.activationText);
    owned.bindCommand = std::string(context.bindCommand);
    return owned;
}

TagsModule::EvaluationContext TagsModule::MakeViewContext(const OwnedEvaluationContext& context) {
    return EvaluationContext{
        context.sampApi,
        context.activationSource,
        context.activationText,
        context.bindCommand,
    };
}

void TagsModule::PushContext(const EvaluationContext& context) const {
    g_activeContextStack.push_back(MakeOwnedContext(context, sampApi_));
}

void TagsModule::PopContext() const {
    if (!g_activeContextStack.empty()) {
        g_activeContextStack.pop_back();
    }
}

void TagsModule::LoadConfig() {
    customVariables_.clear();

    const jsonutil::JsonObject section = AppConfig::Instance().ReadSectionObject(kTagsSectionName);
    const jsonutil::JsonObject* customVars = jsonutil::JsonObjectOrNull(&section, kCustomVarsKey.data());
    if (!customVars) {
        return;
    }

    for (const auto& [key, value] : *customVars) {
        if (const std::string* text = value.TryString()) {
            customVariables_.emplace_back(key, *text);
        }
    }

    std::sort(customVariables_.begin(), customVariables_.end(), [](const auto& left, const auto& right) {
        return left.first < right.first;
    });
}

void TagsModule::SaveConfig() const {
    jsonutil::JsonObject section;
    jsonutil::JsonObject customVars;
    for (const auto& [name, value] : customVariables_) {
        customVars[name] = value;
    }
    section[std::string(kCustomVarsKey)] = jsonutil::JsonValue(std::move(customVars));
    AppConfig::Instance().QueueSectionReplace(std::string(kTagsSectionName), jsonutil::JsonValue(std::move(section)));
}

std::string TagsModule::Trim(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string TagsModule::ToLower(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

bool TagsModule::StartsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> TagsModule::SplitCommandArgs(std::string_view value) {
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos < value.size()) {
        while (pos < value.size() && std::isspace(static_cast<unsigned char>(value[pos])) != 0) {
            ++pos;
        }
        if (pos >= value.size()) {
            break;
        }

        std::size_t end = pos;
        while (end < value.size() && std::isspace(static_cast<unsigned char>(value[end])) == 0) {
            ++end;
        }
        result.emplace_back(value.substr(pos, end - pos));
        pos = end;
    }
    return result;
}

std::optional<int> TagsModule::ParseInteger(std::string_view value) {
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int sign = 1;
    std::size_t pos = 0;
    if (trimmed.front() == '+') {
        pos = 1;
    } else if (trimmed.front() == '-') {
        sign = -1;
        pos = 1;
    }

    if (pos >= trimmed.size()) {
        return std::nullopt;
    }

    int parsed = 0;
    for (; pos < trimmed.size(); ++pos) {
        const unsigned char ch = static_cast<unsigned char>(trimmed[pos]);
        if (std::isdigit(ch) == 0) {
            return std::nullopt;
        }
        parsed = parsed * 10 + static_cast<int>(ch - '0');
    }

    return parsed * sign;
}

TagsModule::EvaluationContext TagsModule::ResolveActiveContext(
    std::string_view defaultSource,
    std::string_view defaultText) const {
    if (!g_activeContextStack.empty()) {
        return MakeViewContext(g_activeContextStack.back());
    }

    return EvaluationContext{
        sampApi_,
        defaultSource,
        defaultText,
        {},
    };
}

std::string TagsModule::ResolvePlayerNickById(int id, const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion() || id < 0) {
        return std::string();
    }

    const std::string nick = sampApi->GetNameID(id);
    return nick.empty() || nick == "UNKNOWN" ? std::string() : nick;
}

std::optional<std::string> TagsModule::ResolveBuiltinIdTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }
    return std::to_string(sampApi->Local_ID());
}

std::optional<std::string> TagsModule::ResolveBuiltinNickTag(const EvaluationContext& context) const {
    SampApi* sampApi = context.sampApi ? context.sampApi : sampApi_;
    if (!sampApi || !sampApi->sampModule() || !sampApi->isSupportedVersion()) {
        return std::string();
    }
    return ResolvePlayerNickById(sampApi->Local_ID(), context);
}

std::optional<std::string> TagsModule::ResolveBuiltinNickFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    const std::optional<int> id = ParseInteger(param);
    if (!id.has_value()) {
        return std::string();
    }
    return ResolvePlayerNickById(*id, context);
}

std::optional<std::string> TagsModule::ResolveBuiltinParamcmdFunctionTag(
    std::string_view param,
    const EvaluationContext& context) const {
    if (context.activationSource != "command") {
        return std::string();
    }

    std::string command = Trim(context.bindCommand);
    std::string input = Trim(context.activationText);
    if (!command.empty() && command.front() != '/') {
        command.insert(command.begin(), '/');
    }
    if (!input.empty() && input.front() != '/') {
        input.insert(input.begin(), '/');
    }

    if (command.empty() || input.empty() || !StartsWith(input, command)) {
        return std::string();
    }
    if (input.size() > command.size()
        && std::isspace(static_cast<unsigned char>(input[command.size()])) == 0) {
        return std::string();
    }

    const std::string argsText = Trim(input.substr(command.size()));
    const std::vector<std::string> args = SplitCommandArgs(argsText);
    if (args.empty()) {
        return std::string();
    }

    std::string selector = Trim(param);
    selector.erase(
        std::remove_if(selector.begin(), selector.end(), [](const unsigned char ch) {
            return std::isspace(ch) != 0;
        }),
        selector.end());
    if (selector.empty()) {
        return std::string();
    }

    const auto parsePositiveIndex = [](std::string_view raw) -> int {
        if (raw.empty()) {
            return -1;
        }
        int value = 0;
        for (const unsigned char ch : raw) {
            if (std::isdigit(ch) == 0) {
                return -1;
            }
            value = value * 10 + static_cast<int>(ch - '0');
        }
        return value;
    };

    if (const int single = parsePositiveIndex(selector); single >= 0) {
        if (single < 1 || single > static_cast<int>(args.size())) {
            return std::string();
        }
        return args[static_cast<std::size_t>(single - 1)];
    }

    if (selector.back() == '+' && selector.size() > 1) {
        const int from = parsePositiveIndex(std::string_view(selector).substr(0, selector.size() - 1));
        if (from < 1 || from > static_cast<int>(args.size())) {
            return std::string();
        }

        std::string joined;
        for (int i = from - 1; i < static_cast<int>(args.size()); ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    if (selector.back() == '-' && selector.size() > 1) {
        const int upto = parsePositiveIndex(std::string_view(selector).substr(0, selector.size() - 1));
        if (upto < 1) {
            return std::string();
        }

        const int clamped = std::min(upto, static_cast<int>(args.size()));
        std::string joined;
        for (int i = 0; i < clamped; ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    if (const std::size_t dashPos = selector.find('-'); dashPos != std::string::npos) {
        const int from = parsePositiveIndex(std::string_view(selector).substr(0, dashPos));
        const int to = parsePositiveIndex(std::string_view(selector).substr(dashPos + 1));
        if (from < 1 || to < from || from > static_cast<int>(args.size())) {
            return std::string();
        }

        const int clampedTo = std::min(to, static_cast<int>(args.size()));
        std::string joined;
        for (int i = from - 1; i < clampedTo; ++i) {
            if (!joined.empty()) {
                joined += ' ';
            }
            joined += args[static_cast<std::size_t>(i)];
        }
        return joined;
    }

    return std::string();
}

std::optional<std::string> TagsModule::ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const {
    const std::string normalized = ToLower(name);

    for (const auto& [customName, customValue] : customVariables_) {
        if (normalized == ToLower(customName)) {
            return customValue;
        }
    }

    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Simple, normalized);
        entry && entry->simpleResolver) {
        return entry->simpleResolver(*this, context);
    }

    return std::nullopt;
}

std::optional<std::string> TagsModule::ResolveFunctionTag(
    std::string_view name,
    std::string_view param,
    const EvaluationContext& context) const {
    const std::string normalized = ToLower(name);
    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Function, normalized);
        entry && entry->functionResolver) {
        return entry->functionResolver(*this, param, context);
    }

    return std::nullopt;
}

std::string TagsModule::ExpandSimpleTags(std::string_view text, const EvaluationContext& context) const {
    std::string output;
    output.reserve(text.size());

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t start = text.find('{', pos);
        if (start == std::string_view::npos) {
            output.append(text.substr(pos));
            break;
        }

        output.append(text.substr(pos, start - pos));
        const std::size_t end = text.find('}', start + 1);
        if (end == std::string_view::npos) {
            output.append(text.substr(start));
            break;
        }

        const std::string_view name = text.substr(start + 1, end - start - 1);
        bool validName = !name.empty();
        for (const unsigned char ch : name) {
            if (std::isalnum(ch) == 0 && ch != '_') {
                validName = false;
                break;
            }
        }

        if (!validName) {
            output.append(text.substr(start, end - start + 1));
            pos = end + 1;
            continue;
        }

        if (const std::optional<std::string> value = ResolveSimpleTag(name, context); value.has_value()) {
            output += *value;
        } else {
            output.append(text.substr(start, end - start + 1));
        }
        pos = end + 1;
    }

    return output;
}

std::string TagsModule::ExpandFunctionTags(std::string_view text, const EvaluationContext& context, int depth) const {
    std::string output;
    output.reserve(text.size());

    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t start = text.find('[', pos);
        if (start == std::string_view::npos) {
            output.append(text.substr(pos));
            break;
        }

        output.append(text.substr(pos, start - pos));

        std::size_t nameEnd = start + 1;
        while (nameEnd < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[nameEnd]);
            if (std::isalnum(ch) == 0 && ch != '_') {
                break;
            }
            ++nameEnd;
        }

        if (nameEnd == start + 1) {
            output.push_back(text[start]);
            pos = start + 1;
            continue;
        }

        std::size_t openParen = nameEnd;
        while (openParen < text.size() && std::isspace(static_cast<unsigned char>(text[openParen])) != 0) {
            ++openParen;
        }
        if (openParen >= text.size() || text[openParen] != '(') {
            output.append(text.substr(start, openParen - start));
            pos = openParen;
            continue;
        }

        int parenDepth = 1;
        std::size_t cursor = openParen + 1;
        for (; cursor < text.size(); ++cursor) {
            if (text[cursor] == '(') {
                ++parenDepth;
            } else if (text[cursor] == ')') {
                --parenDepth;
                if (parenDepth == 0) {
                    break;
                }
            }
        }

        if (cursor >= text.size() || cursor + 1 >= text.size() || text[cursor + 1] != ']') {
            output.append(text.substr(start));
            break;
        }

        const std::string_view name = text.substr(start + 1, nameEnd - start - 1);
        const std::string_view rawParam = text.substr(openParen + 1, cursor - openParen - 1);
        if (const std::optional<std::string> value = ResolveFunctionTag(
                name,
                ExpandTextRecursive(rawParam, context, depth + 1),
                context);
            value.has_value()) {
            output += *value;
        } else {
            output.append(text.substr(start, cursor - start + 2));
        }
        pos = cursor + 2;
    }

    return output;
}

std::string TagsModule::ExpandTextRecursive(std::string_view text, const EvaluationContext& context, int depth) const {
    if (depth > kRecursionLimit) {
        return std::string(text);
    }

    const std::string withFunctions = ExpandFunctionTags(text, context, depth);
    return ExpandSimpleTags(withFunctions, context);
}

std::string TagsModule::ExpandText(std::string_view text) const {
    return ExpandText(text, ResolveActiveContext());
}

std::string TagsModule::ExpandText(std::string_view text, const EvaluationContext& context) const {
    EvaluationContext effective = context;
    if (!effective.sampApi) {
        effective.sampApi = sampApi_;
    }
    return ExpandTextRecursive(text, effective, 0);
}

std::string TagsModule::ExpandOutgoingText(
    std::string_view text,
    std::string_view activationSource,
    std::string_view activationText) const {
    return ExpandText(text, ResolveActiveContext(activationSource, activationText));
}

void TagsModule::DrawMiscHomePage() {
    UiSettings& ui = UiSettings::Instance();

    ImGui::SeparatorText(ui.Text(UiText::TabMisc));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscHomeIntro));
    ImGui::Spacing();

    if (DrawNavigationCardButton(
            "##misc_variables_card",
            ui.Text(UiText::MiscVariablesTitle),
            ui.Text(UiText::MiscVariablesEntryDesc),
            ui.Text(UiText::MiscOpenSectionAction),
            ImVec4(0.97f, 0.83f, 0.46f, 1.0f),
            136.0f)) {
        currentPage_ = MiscPage::Variables;
    }
}

void TagsModule::DrawVariablesPage() {
    UiSettings& ui = UiSettings::Instance();

    if (ImGui::Button(ui.Text(UiText::EditorBack), ScaleUi(120.0f, 0.0f))) {
        currentPage_ = MiscPage::Home;
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", ui.Text(UiText::TabMisc));
    ImGui::Spacing();

    ImGui::SeparatorText(ui.Text(UiText::MiscVariablesTitle));
    ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesIntro));
    ImGui::Spacing();

    if (ImGui::BeginChild("##tags_overview_card", ImVec2(0.0f, ScaleUi(132.0f)), ImGuiChildFlags_FrameStyle)) {
        ImGui::TextColored(ImVec4(0.97f, 0.83f, 0.46f, 1.0f), "%s", ui.Text(UiText::MiscVariablesCardTitle));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.Text(UiText::MiscVariablesCardDesc));
        ImGui::Spacing();
        if (ImGui::BeginTable("##tags_overview_stats", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesBuiltinsLabel),
                std::to_string(tagRegistry_.Entries().size()),
                ImVec4(0.97f, 0.83f, 0.46f, 1.0f));
            ImGui::TableSetColumnIndex(1);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesSimpleLabel),
                std::to_string(tagRegistry_.Count(TagKind::Simple)),
                ImVec4(0.55f, 0.86f, 0.98f, 1.0f));
            ImGui::TableSetColumnIndex(2);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesFunctionLabel),
                std::to_string(tagRegistry_.Count(TagKind::Function)),
                ImVec4(0.75f, 0.92f, 0.62f, 1.0f));
            ImGui::TableSetColumnIndex(3);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesCustomLabel),
                std::to_string(customVariables_.size()),
                ImVec4(0.88f, 0.70f, 0.96f, 1.0f));
            ImGui::TableSetColumnIndex(4);
            DrawStatBlock(
                ui.Text(UiText::MiscVariablesConfigLabel),
                std::string(kTagsSectionName),
                ImVec4(0.92f, 0.92f, 0.92f, 1.0f));
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    if (ImGui::BeginTable(
            "##tags_main_layout",
            2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
            ImVec2(0.0f, ScaleUi(308.0f)))) {
        ImGui::TableSetupColumn("catalog", ImGuiTableColumnFlags_WidthStretch, 0.48f);
        ImGui::TableSetupColumn("inspector", ImGuiTableColumnFlags_WidthStretch, 0.52f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("##tags_catalog_card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
            ImGui::SeparatorText(ui.Text(UiText::MiscVariablesCatalogTitle));
            InputTextWithHintString(
                "##tags_search_query",
                ui.Text(UiText::MiscVariablesSearchHint),
                searchQuery_,
                ImGuiInputTextFlags_AutoSelectAll,
                128);
            ImGui::Spacing();

            std::vector<int> visibleIndices;
            const std::string query = ToLower(searchQuery_);
            const auto& entries = tagRegistry_.Entries();
            for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
                const TagEntry& tag = entries[static_cast<std::size_t>(i)];
                const std::string haystack = ToLower(std::string(tag.token) + " " + ui.Text(tag.descriptionText));
                if (query.empty() || haystack.find(query) != std::string::npos) {
                    visibleIndices.push_back(i);
                }
            }

            if (ImGui::BeginChild("##tags_catalog_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
                for (const int index : visibleIndices) {
                    const TagEntry& tag = entries[static_cast<std::size_t>(index)];
                    const bool selected = selectedTagIndex_ == index;
                    if (ImGui::Selectable(tag.token.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        selectedTagIndex_ = index;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("[%s]", TagKindLabel(tag.kind, ui));
                    if (ImGui::IsItemHovered() || (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))) {
                        ImGui::SetTooltip("%s", ui.Text(tag.descriptionText));
                    }
                }

                if (visibleIndices.empty()) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesCatalogEmpty));
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("##tags_inspector_card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
            ImGui::SeparatorText(ui.Text(UiText::MiscVariablesInspectorTitle));
            const TagEntry* selectedTag = tagRegistry_.FindByIndex(selectedTagIndex_);
            if (!selectedTag) {
                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesInspectorEmpty));
            } else {
                ImGui::TextColored(ImVec4(0.55f, 0.86f, 0.98f, 1.0f), "%s", selectedTag->token.c_str());
                ImGui::TextDisabled("%s", TagKindLabel(selectedTag->kind, ui));
                ImGui::Separator();

                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesDescriptionLabel));
                ImGui::TextWrapped("%s", ui.Text(selectedTag->descriptionText));
                ImGui::Spacing();

                ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesExampleLabel));
                ImGui::TextWrapped("%s", selectedTag->example.c_str());
                ImGui::Spacing();

                if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyToken), ScaleUi(170.0f, 0.0f))) {
                    ImGui::SetClipboardText(selectedTag->token.c_str());
                }
                ImGui::SameLine();
                if (ImGui::Button(ui.Text(UiText::MiscVariablesCopyExample), ScaleUi(170.0f, 0.0f))) {
                    ImGui::SetClipboardText(selectedTag->example.c_str());
                }

                if (selectedTag->kind == TagKind::Function && std::string_view(selectedTag->name) == "paramcmd") {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesParamcmdNote));
                }
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }

    ImGui::Spacing();
    if (ImGui::BeginChild("##tags_preview_card", ImVec2(0.0f, 0.0f), ImGuiChildFlags_FrameStyle)) {
        ImGui::SeparatorText(ui.Text(UiText::MiscVariablesPreviewTitle));
        ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesPreviewHint));
        ImGui::Spacing();

        if (ImGui::BeginTable("##tags_preview_layout", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("left", ImGuiTableColumnFlags_WidthStretch, 0.56f);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthStretch, 0.44f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesTemplateLabel));
            InputTextMultilineWithCounterString(
                "##tags_preview_template",
                previewTemplate_,
                ImVec2(-FLT_MIN, ScaleUi(152.0f)),
                ImGuiInputTextFlags_AllowTabInput,
                1024);
            ImGui::Spacing();

            const char* launchLabels[] = {
                ui.Text(UiText::MiscVariablesPreviewLaunchManual),
                ui.Text(UiText::MiscVariablesPreviewLaunchCommand),
            };
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesPreviewSourceLabel));
            ImGui::SetNextItemWidth(ScaleUi(260.0f));
            ImGui::Combo("##tags_preview_launch_source", &previewLaunchSource_, launchLabels, IM_ARRAYSIZE(launchLabels));

            const bool commandMode = previewLaunchSource_ == kPreviewLaunchCommand;
            ImGui::BeginDisabled(!commandMode);
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesPreviewBindCommandLabel));
            InputTextString("##tags_preview_bind_command", previewBindCommand_, ImGuiInputTextFlags_AutoSelectAll, 128);
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesPreviewCommandTextLabel));
            InputTextString("##tags_preview_command_text", previewCommandText_, ImGuiInputTextFlags_AutoSelectAll, 256);
            ImGui::EndDisabled();

            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesPreviewResultLabel));
            EvaluationContext previewContext{};
            previewContext.sampApi = sampApi_;
            if (commandMode) {
                previewContext.activationSource = "command";
                previewContext.activationText = previewCommandText_;
                previewContext.bindCommand = previewBindCommand_;
            }

            const std::string previewResult = ExpandText(previewTemplate_, previewContext);
            if (ImGui::BeginChild("##tags_preview_result", ImVec2(0.0f, ScaleUi(232.0f)), ImGuiChildFlags_Borders)) {
                if (previewResult.empty()) {
                    ImGui::TextDisabled("%s", ui.Text(UiText::MiscVariablesPreviewEmpty));
                } else {
                    ImGui::TextWrapped("%s", previewResult.c_str());
                }
            }
            ImGui::EndChild();

            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

void TagsModule::DrawMiscTab() {
    if (currentPage_ == MiscPage::Variables) {
        DrawVariablesPage();
        return;
    }

    DrawMiscHomePage();
}
