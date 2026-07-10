#include "tags_module_impl.h"
#include "tags_module_detail.h"

#include "text_encoding.h"

namespace {

constexpr std::string_view kCursorFunctionSentinelPrefix = "__helperbyorc_cursor_fn$";

int Utf16CodeUnitLength(std::string_view utf8Text) {
    if (utf8Text.empty()) {
        return 0;
    }

    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        utf8Text.data(),
        static_cast<int>(utf8Text.size()),
        nullptr,
        0);
    return length > 0 ? length : static_cast<int>(utf8Text.size());
}

double TagsPerfNowMs() {
    static LARGE_INTEGER frequency{};
    static bool initialized = false;
    if (!initialized) {
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
            frequency.QuadPart = 1;
        }
        initialized = true;
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

bool MightContainAnyTag(std::string_view text) {
    return text.find('{') != std::string_view::npos || text.find('[') != std::string_view::npos;
}

bool IsSampColorLiteralName(std::string_view name) {
    if (name.size() != 6 && name.size() != 8) {
        return false;
    }
    for (const unsigned char ch : name) {
        if (std::isxdigit(ch) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace

const char* TagsModule::Impl::TagPerfSourceName(TagPerfSource source) {
    switch (source) {
    case TagPerfSource::Hud:
        return "hud";
    case TagPerfSource::Binder:
        return "binder";
    case TagPerfSource::Outgoing:
        return "outgoing";
    case TagPerfSource::Notepad:
        return "notepad";
    case TagPerfSource::Ui:
        return "ui";
    case TagPerfSource::Unknown:
    default:
        return "unknown";
    }
}

const char* TagsModule::Impl::TagPerfGroupName(TagPerfGroup group) {
    switch (group) {
    case TagPerfGroup::Custom:
        return "custom";
    case TagPerfGroup::MyCar:
        return "mycar";
    case TagPerfGroup::Closest:
        return "closest";
    case TagPerfGroup::Dialog:
        return "dialog";
    case TagPerfGroup::Arizona:
        return "arizona";
    case TagPerfGroup::Action:
        return "action";
    case TagPerfGroup::Builtin:
    default:
        return "builtin";
    }
}

const char* TagsModule::Impl::TagKindPerfName(TagKind kind) {
    return kind == TagKind::Function ? "function" : "simple";
}

TagsModule::Impl::TagPerfGroup TagsModule::Impl::ClassifyTagPerfGroup(std::string_view normalizedName, bool action) {
    if (action) {
        return TagPerfGroup::Action;
    }
    if (normalizedName.rfind("mycar", 0) == 0) {
        return TagPerfGroup::MyCar;
    }
    if (normalizedName.rfind("closest", 0) == 0) {
        return TagPerfGroup::Closest;
    }
    if (normalizedName.rfind("arzdialog", 0) == 0) {
        return TagPerfGroup::Arizona;
    }
    if (normalizedName.find("dialog") != std::string_view::npos) {
        return TagPerfGroup::Dialog;
    }
    return TagPerfGroup::Builtin;
}

TagsModule::Impl::TagPerfGroup TagsModule::Impl::DominantTagPerfGroup(const TagExpansionTrace& trace) {
    std::size_t bestIndex = 0;
    std::uint64_t bestCount = 0;
    for (std::size_t i = 0; i < trace.groupCounts.size(); ++i) {
        if (trace.groupCounts[i] > bestCount) {
            bestCount = trace.groupCounts[i];
            bestIndex = i;
        }
    }
    return static_cast<TagPerfGroup>(bestIndex);
}

bool TagsModule::Impl::TryGetCursorTarget(std::string_view normalizedName, CursorTarget& target) const {
    if (normalizedName == "cursor") {
        target = CursorTarget::SampChat;
        return true;
    }
    if (normalizedName == "arzcursor") {
        target = CursorTarget::ArizonaChat;
        return true;
    }
    if (normalizedName == "cursordialog") {
        target = CursorTarget::SampDialog;
        return true;
    }
    if (normalizedName == "arzcursordialog") {
        target = CursorTarget::ArizonaDialog;
        return true;
    }
    return false;
}

std::optional<std::pair<int, int>> TagsModule::Impl::ParseCursorFunctionRange(std::string_view param) const {
    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(param, ';');
    if (parts.empty() || parts.size() > 2) {
        return std::nullopt;
    }

    const std::optional<int> start = ParseInteger(Unquote(Trim(parts[0])));
    if (!start.has_value()) {
        return std::nullopt;
    }

    int finish = *start;
    if (parts.size() == 2) {
        const std::optional<int> parsedFinish = ParseInteger(Unquote(Trim(parts[1])));
        if (!parsedFinish.has_value()) {
            return std::nullopt;
        }
        finish = *parsedFinish;
    }

    int normalizedStart = std::max(*start, 0);
    int normalizedFinish = std::max(finish, 0);
    if (normalizedFinish < normalizedStart) {
        std::swap(normalizedStart, normalizedFinish);
    }
    return std::make_pair(normalizedStart, normalizedFinish);
}

std::string TagsModule::Impl::MakeCursorFunctionSentinel(CursorTarget target, int start, int finish) {
    int targetId = -1;
    switch (target) {
    case CursorTarget::SampChat:
        targetId = 0;
        break;
    case CursorTarget::ArizonaChat:
        targetId = 1;
        break;
    case CursorTarget::SampDialog:
        targetId = 2;
        break;
    case CursorTarget::ArizonaDialog:
        targetId = 3;
        break;
    default:
        break;
    }

    if (targetId < 0) {
        return {};
    }

    return std::string("{")
        + std::string(kCursorFunctionSentinelPrefix)
        + std::to_string(targetId)
        + "_"
        + std::to_string(start)
        + "_"
        + std::to_string(finish)
        + "}";
}

bool TagsModule::Impl::TryParseCursorFunctionSentinel(
    std::string_view name,
    CursorTarget& target,
    int& start,
    int& finish) {
    if (name.rfind(kCursorFunctionSentinelPrefix, 0) != 0) {
        return false;
    }

    const std::string_view payload = name.substr(kCursorFunctionSentinelPrefix.size());
    const std::size_t firstSep = payload.find('_');
    if (firstSep == std::string_view::npos) {
        return false;
    }
    const std::size_t secondSep = payload.find('_', firstSep + 1);
    if (secondSep == std::string_view::npos) {
        return false;
    }

    const std::optional<int> targetId = ParseInteger(payload.substr(0, firstSep));
    const std::optional<int> parsedStart = ParseInteger(payload.substr(firstSep + 1, secondSep - firstSep - 1));
    const std::optional<int> parsedFinish = ParseInteger(payload.substr(secondSep + 1));
    if (!targetId.has_value() || !parsedStart.has_value() || !parsedFinish.has_value()) {
        return false;
    }

    switch (*targetId) {
    case 0:
        target = CursorTarget::SampChat;
        break;
    case 1:
        target = CursorTarget::ArizonaChat;
        break;
    case 2:
        target = CursorTarget::SampDialog;
        break;
    case 3:
        target = CursorTarget::ArizonaDialog;
        break;
    default:
        return false;
    }

    start = *parsedStart;
    finish = *parsedFinish;
    return true;
}

int TagsModule::Impl::CursorPositionForOutput(CursorTarget target, const std::string& output) {
    switch (target) {
    case CursorTarget::SampChat:
    case CursorTarget::SampDialog:
        return static_cast<int>(textencoding::Utf8ToGame(output).size());
    case CursorTarget::ArizonaChat:
        return static_cast<int>(output.size());
    case CursorTarget::ArizonaDialog:
        return Utf16CodeUnitLength(output);
    default:
        return static_cast<int>(output.size());
    }
}

TagsModule::Impl::TagPerfSource TagsModule::Impl::ResolveTagPerfSource(const EvaluationContext& context) const {
    if (context.runningBindRuntimeId != 0 || !context.bindCommand.empty()) {
        return TagPerfSource::Binder;
    }
    if (context.activationSource == "hud") {
        return TagPerfSource::Hud;
    }
    if (context.activationSource == "command" || context.activationSource == "chat") {
        return TagPerfSource::Outgoing;
    }
    if (context.activationSource == "notepad") {
        return TagPerfSource::Notepad;
    }
    if (context.activationSource == "ui" || context.activationSource == "binder_preview") {
        return TagPerfSource::Ui;
    }
    return TagPerfSource::Unknown;
}

void TagsModule::Impl::RecordTagGroup(TagExpansionTrace& trace, TagPerfGroup group) const {
    const std::size_t index = static_cast<std::size_t>(group);
    if (index < trace.groupCounts.size()) {
        ++trace.groupCounts[index];
    }
}

void TagsModule::Impl::RecordHotTag(
    TagExpansionTrace& trace,
    TagKind kind,
    std::string_view name,
    double elapsedMs) const {
    if (elapsedMs <= trace.hotTagMs) {
        return;
    }

    trace.hotTagKind = kind;
    trace.hotTagName.assign(name.begin(), name.end());
    trace.hotTagMs = elapsedMs;
}

void TagsModule::Impl::RecordTagExpansionPerf(const TagExpansionTrace& trace, double elapsedMs) const {
    const std::uint64_t now = GetTickCount64();
    TagExpansionPerfStats& stats = tagExpansionPerfStats_;
    if (stats.windowStartMs == 0 || now < stats.windowStartMs) {
        stats.windowStartMs = now;
    }

    const std::size_t bucketIndex = static_cast<std::size_t>(trace.source);
    TagExpansionPerfBucket& bucket = stats.buckets[bucketIndex < stats.buckets.size() ? bucketIndex : 0];
    ++bucket.calls;
    bucket.inputBytes += trace.inputBytes;
    bucket.outputBytes += trace.outputBytes;
    bucket.simpleTags += trace.simpleTags;
    bucket.functionTags += trace.functionTags;
    bucket.customTags += trace.customTags;
    bucket.actionTags += trace.actionTags;
    bucket.unresolvedTags += trace.unresolvedTags;
    bucket.recursionLimitHits += trace.recursionLimitHits;
    bucket.totalMs += elapsedMs;
    bucket.maxMs = std::max(bucket.maxMs, elapsedMs);
    for (std::size_t i = 0; i < bucket.groupCounts.size(); ++i) {
        bucket.groupCounts[i] += trace.groupCounts[i];
    }

    if (elapsedMs >= kTagExpansionSlowLogMs) {
        ++bucket.slowCalls;
        if (lastTagExpansionSlowLogAtMs_ == 0 || now - lastTagExpansionSlowLogAtMs_ >= kTagExpansionSlowLogThrottleMs) {
            lastTagExpansionSlowLogAtMs_ = now;
            const bool hasHotTag = !trace.hotTagName.empty();
            debuglog::WriteInfo(
                "[tags][perf] slow source=%s elapsed=%.2fms group=%s hot=%s:%s hotMs=%.2fms input=%zu output=%zu simple=%llu function=%llu "
                "custom=%llu action=%llu unresolved=%llu depth=%d",
                TagPerfSourceName(trace.source),
                elapsedMs,
                TagPerfGroupName(DominantTagPerfGroup(trace)),
                hasHotTag ? TagKindPerfName(trace.hotTagKind) : "none",
                hasHotTag ? trace.hotTagName.c_str() : "-",
                trace.hotTagMs,
                trace.inputBytes,
                trace.outputBytes,
                static_cast<unsigned long long>(trace.simpleTags),
                static_cast<unsigned long long>(trace.functionTags),
                static_cast<unsigned long long>(trace.customTags),
                static_cast<unsigned long long>(trace.actionTags),
                static_cast<unsigned long long>(trace.unresolvedTags),
                trace.maxDepth);
        }
    }

    MaybeLogTagExpansionPerf(now);
}

void TagsModule::Impl::MaybeLogTagExpansionPerf(std::uint64_t nowMs) const {
    TagExpansionPerfStats& stats = tagExpansionPerfStats_;
    if (stats.windowStartMs == 0 || nowMs < stats.windowStartMs) {
        stats.windowStartMs = nowMs;
        return;
    }

    const std::uint64_t windowMs = nowMs - stats.windowStartMs;
    if (windowMs < kTagsPerfTelemetryWindowMs) {
        return;
    }

    for (std::size_t i = 0; i < stats.buckets.size(); ++i) {
        const TagExpansionPerfBucket& bucket = stats.buckets[i];
        if (bucket.calls == 0) {
            continue;
        }

        std::size_t bestGroupIndex = 0;
        std::uint64_t bestGroupCount = 0;
        for (std::size_t groupIndex = 0; groupIndex < bucket.groupCounts.size(); ++groupIndex) {
            if (bucket.groupCounts[groupIndex] > bestGroupCount) {
                bestGroupCount = bucket.groupCounts[groupIndex];
                bestGroupIndex = groupIndex;
            }
        }

        const double avgMs = bucket.calls > 0
            ? bucket.totalMs / static_cast<double>(bucket.calls)
            : 0.0;
        const double avgInput = bucket.calls > 0
            ? static_cast<double>(bucket.inputBytes) / static_cast<double>(bucket.calls)
            : 0.0;
        const double avgOutput = bucket.calls > 0
            ? static_cast<double>(bucket.outputBytes) / static_cast<double>(bucket.calls)
            : 0.0;
        debuglog::WriteInfo(
            "[tags][perf] window=%llums source=%s calls=%llu slow=%llu avg=%.3fms max=%.2fms "
            "avgIn=%.1f avgOut=%.1f simple=%llu function=%llu custom=%llu action=%llu unresolved=%llu "
            "recursionLimit=%llu topGroup=%s",
            static_cast<unsigned long long>(windowMs),
            TagPerfSourceName(static_cast<TagPerfSource>(i)),
            static_cast<unsigned long long>(bucket.calls),
            static_cast<unsigned long long>(bucket.slowCalls),
            avgMs,
            bucket.maxMs,
            avgInput,
            avgOutput,
            static_cast<unsigned long long>(bucket.simpleTags),
            static_cast<unsigned long long>(bucket.functionTags),
            static_cast<unsigned long long>(bucket.customTags),
            static_cast<unsigned long long>(bucket.actionTags),
            static_cast<unsigned long long>(bucket.unresolvedTags),
            static_cast<unsigned long long>(bucket.recursionLimitHits),
            TagPerfGroupName(static_cast<TagPerfGroup>(bestGroupIndex)));
    }

    stats = TagExpansionPerfStats{};
    stats.windowStartMs = nowMs;
}

std::optional<std::string> TagsModule::Impl::ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const {
    return ResolveSimpleTagNormalized(ToLower(name), context, nullptr);
}

std::optional<std::string> TagsModule::Impl::ResolveSimpleTagNormalized(
    std::string_view normalizedName,
    const EvaluationContext& context,
    TagExpansionTrace* trace) const {
    if (const auto customIt = customVariableIndex_.find(std::string(normalizedName)); customIt != customVariableIndex_.end()) {
        if (trace) {
            ++trace->customTags;
            RecordTagGroup(*trace, TagPerfGroup::Custom);
        }
        return customVariables_[customIt->second].second;
    }

    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Simple, normalizedName);
        entry && entry->simpleResolver) {
        if (trace) {
            const bool action = variables_picker::IsActionBuiltin(variables_picker::EntryKind::Simple, entry->name);
            if (action) {
                ++trace->actionTags;
            }
            RecordTagGroup(*trace, ClassifyTagPerfGroup(normalizedName, action));
        }
        const double resolverBeginMs = trace ? TagsPerfNowMs() : 0.0;
        const std::optional<std::string> resolved = entry->simpleResolver(*this, context);
        if (trace) {
            RecordHotTag(*trace, TagKind::Simple, entry->name, TagsPerfNowMs() - resolverBeginMs);
        }
        return resolved;
    }

    if (trace) {
        ++trace->unresolvedTags;
    }
    return std::nullopt;
}

std::optional<std::string> TagsModule::Impl::ResolveFunctionTag(
    std::string_view name,
    std::string_view param,
    const EvaluationContext& context,
    int depth,
    TagExpansionTrace* trace) const {
    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Function, name);
        entry && entry->functionResolver) {
        if (trace) {
            const bool action = variables_picker::IsActionBuiltin(variables_picker::EntryKind::Function, entry->name);
            if (action) {
                ++trace->actionTags;
            }
            RecordTagGroup(*trace, ClassifyTagPerfGroup(name, action));
        }
        const bool rawParam = name == "ifandor" || name == "dialogsettext" || name == "dialogresponse"
            || name == "arzdialogsetinputtext" || name == "arzdialogsendrespond";
        std::optional<std::string> resolved;
        if (rawParam) {
            const double resolverBeginMs = trace ? TagsPerfNowMs() : 0.0;
            resolved = entry->functionResolver(*this, param, context, depth);
            if (trace) {
                RecordHotTag(*trace, TagKind::Function, entry->name, TagsPerfNowMs() - resolverBeginMs);
            }
            return resolved;
        }

        const std::string expandedParam = ExpandTextRecursive(param, context, depth + 1, trace);
        const double resolverBeginMs = trace ? TagsPerfNowMs() : 0.0;
        resolved = entry->functionResolver(*this, expandedParam, context, depth);
        if (trace) {
            RecordHotTag(*trace, TagKind::Function, entry->name, TagsPerfNowMs() - resolverBeginMs);
        }
        return resolved;
    }

    if (trace) {
        ++trace->unresolvedTags;
    }
    return std::nullopt;
}

std::string TagsModule::Impl::ExpandSimpleTags(
    std::string_view text,
    const EvaluationContext& context,
    TagExpansionTrace* trace) const {
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
        CursorTarget sentinelTarget = CursorTarget::SampChat;
        int sentinelStart = 0;
        int sentinelFinish = 0;
        if (TryParseCursorFunctionSentinel(name, sentinelTarget, sentinelStart, sentinelFinish)) {
            RecordCursorRange(sentinelTarget, sentinelStart, sentinelFinish, context);
            pos = end + 1;
            continue;
        }

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

        if (IsSampColorLiteralName(name)) {
            output.append(text.substr(start, end - start + 1));
            pos = end + 1;
            continue;
        }

        if (trace) {
            ++trace->simpleTags;
        }
        const std::string normalizedName = ToLower(name);
        CursorTarget cursorTarget = CursorTarget::SampChat;
        if (TryGetCursorTarget(normalizedName, cursorTarget)) {
            if (trace) {
                RecordTagGroup(*trace, TagPerfGroup::Action);
            }
            RecordCursorMarker(cursorTarget, output, context);
            pos = end + 1;
            continue;
        }

        if (const std::optional<std::string> value = ResolveSimpleTagNormalized(normalizedName, context, trace); value.has_value()) {
            output += *value;
        } else {
            output.append(text.substr(start, end - start + 1));
        }
        pos = end + 1;
    }

    return output;
}

std::string TagsModule::Impl::ExpandFunctionTags(
    std::string_view text,
    const EvaluationContext& context,
    int depth,
    TagExpansionTrace* trace) const {
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
        if (openParen < text.size() && text[openParen] == ']') {
            const std::string_view name = text.substr(start + 1, nameEnd - start - 1);
            const std::string normalizedName = ToLower(name);
            if (trace) {
                ++trace->functionTags;
            }
            CursorTarget cursorTarget = CursorTarget::SampChat;
            if (TryGetCursorTarget(normalizedName, cursorTarget)) {
                if (trace) {
                    RecordTagGroup(*trace, TagPerfGroup::Action);
                }
                pos = openParen + 1;
                continue;
            }
            if (const std::optional<std::string> value = ResolveFunctionTag(normalizedName, {}, context, depth, trace);
                value.has_value()) {
                output += *value;
            } else {
                output.append(text.substr(start, openParen - start + 1));
            }
            pos = openParen + 1;
            continue;
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
        const std::string normalizedName = ToLower(name);
        if (trace) {
            ++trace->functionTags;
        }
        CursorTarget cursorTarget = CursorTarget::SampChat;
        if (TryGetCursorTarget(normalizedName, cursorTarget)) {
            if (trace) {
                RecordTagGroup(*trace, TagPerfGroup::Action);
            }
            if (const std::optional<std::pair<int, int>> range = ParseCursorFunctionRange(rawParam); range.has_value()) {
                output += MakeCursorFunctionSentinel(cursorTarget, range->first, range->second);
            }
            pos = cursor + 2;
            continue;
        }
        if (const std::optional<std::string> value = ResolveFunctionTag(
                normalizedName,
                rawParam,
                context,
                depth,
                trace);
            value.has_value()) {
            output += *value;
        } else {
            output.append(text.substr(start, cursor - start + 2));
        }
        pos = cursor + 2;
    }

    return output;
}

std::string TagsModule::Impl::ExpandTextRecursive(std::string_view text, const EvaluationContext& context, int depth) const {
    return ExpandTextRecursive(text, context, depth, nullptr);
}

std::string TagsModule::Impl::ExpandTextRecursive(
    std::string_view text,
    const EvaluationContext& context,
    int depth,
    TagExpansionTrace* trace) const {
    if (depth > kRecursionLimit) {
        if (trace) {
            ++trace->recursionLimitHits;
        }
        return std::string(text);
    }
    if (trace) {
        trace->maxDepth = std::max(trace->maxDepth, depth);
    }

    const std::string withFunctions = text.find('[') == std::string_view::npos
        ? std::string(text)
        : ExpandFunctionTags(text, context, depth, trace);
    if (withFunctions.find('{') == std::string::npos) {
        return withFunctions;
    }
    return ExpandSimpleTags(withFunctions, context, trace);
}

void TagsModule::Impl::RecordCursorMarker(
    CursorTarget target,
    const std::string& currentOutput,
    const EvaluationContext& context) const {
    const int position = CursorPositionForOutput(target, currentOutput);
    RecordCursorRange(target, position, position, context);
}

void TagsModule::Impl::RecordCursorRange(
    CursorTarget target,
    int start,
    int finish,
    const EvaluationContext& context) const {
    if (!context.allowSideEffects || !context.cursorIntents) {
        return;
    }

    start = std::max(start, 0);
    finish = std::max(finish, 0);
    if (finish < start) {
        std::swap(start, finish);
    }

    CursorRange* range = nullptr;
    switch (target) {
    case CursorTarget::SampChat:
        range = &context.cursorIntents->sampChat;
        break;
    case CursorTarget::ArizonaChat:
        range = &context.cursorIntents->arizonaChat;
        break;
    case CursorTarget::SampDialog:
        range = &context.cursorIntents->sampDialog;
        break;
    case CursorTarget::ArizonaDialog:
        range = &context.cursorIntents->arizonaDialog;
        break;
    default:
        break;
    }

    if (!range) {
        return;
    }

    range->start = start;
    range->finish = finish;
    range->valid = true;
}

std::string TagsModule::Impl::ExpandText(std::string_view text) const {
    return ExpandText(text, ResolveActiveContext());
}

std::string TagsModule::Impl::ExpandText(std::string_view text, const EvaluationContext& context) const {
    EvaluationContext effective = context;
    if (!effective.sampApi) {
        effective.sampApi = sampApi_;
    }
    if (!MightContainAnyTag(text)) {
        return std::string(text);
    }

    TagExpansionTrace trace;
    trace.source = ResolveTagPerfSource(effective);
    trace.inputBytes = text.size();
    const double beginMs = TagsPerfNowMs();
    std::string result = ExpandTextRecursive(text, effective, 0, &trace);
    trace.outputBytes = result.size();
    RecordTagExpansionPerf(trace, TagsPerfNowMs() - beginMs);
    return result;
}

TagsModule::Impl::ExpandedText TagsModule::Impl::ExpandTextWithCursorIntents(std::string_view text) const {
    return ExpandTextWithCursorIntents(text, ResolveActiveContext());
}

TagsModule::Impl::ExpandedText TagsModule::Impl::ExpandTextWithCursorIntents(
    std::string_view text,
    const EvaluationContext& context) const {
    ExpandedText result;
    EvaluationContext effective = context;
    if (!effective.sampApi) {
        effective.sampApi = sampApi_;
    }
    effective.cursorIntents = &result.cursors;
    if (!MightContainAnyTag(text)) {
        result.text.assign(text.begin(), text.end());
        return result;
    }

    TagExpansionTrace trace;
    trace.source = ResolveTagPerfSource(effective);
    trace.inputBytes = text.size();
    const double beginMs = TagsPerfNowMs();
    result.text = ExpandTextRecursive(text, effective, 0, &trace);
    trace.outputBytes = result.text.size();
    RecordTagExpansionPerf(trace, TagsPerfNowMs() - beginMs);
    return result;
}

std::string TagsModule::Impl::ExpandHudText(std::string_view text) const {
    EvaluationContext context = ResolveActiveContext("hud", {});
    context.allowSideEffects = false;
    return ExpandText(text, context);
}

std::string TagsModule::Impl::ExpandOutgoingText(
    std::string_view text,
    std::string_view activationSource,
    std::string_view activationText) const {
    return ExpandText(text, ResolveActiveContext(activationSource, activationText));
}
