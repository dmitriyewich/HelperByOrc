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

} // namespace

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

std::optional<std::string> TagsModule::Impl::ResolveSimpleTag(std::string_view name, const EvaluationContext& context) const {
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

std::optional<std::string> TagsModule::Impl::ResolveFunctionTag(
    std::string_view name,
    std::string_view param,
    const EvaluationContext& context,
    int depth) const {
    const std::string normalized = ToLower(name);
    if (const TagEntry* entry = tagRegistry_.Find(TagKind::Function, normalized);
        entry && entry->functionResolver) {
        if (normalized == "ifandor" || normalized == "dialogsettext" || normalized == "dialogresponse"
            || normalized == "arzdialogsetinputtext" || normalized == "arzdialogsendrespond") {
            return entry->functionResolver(*this, param, context, depth);
        }
        return entry->functionResolver(*this, ExpandTextRecursive(param, context, depth + 1), context, depth);
    }

    return std::nullopt;
}

std::string TagsModule::Impl::ExpandSimpleTags(std::string_view text, const EvaluationContext& context) const {
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
        const std::string normalizedName = ToLower(name);
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

        CursorTarget cursorTarget = CursorTarget::SampChat;
        if (TryGetCursorTarget(normalizedName, cursorTarget)) {
            RecordCursorMarker(cursorTarget, output, context);
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

std::string TagsModule::Impl::ExpandFunctionTags(std::string_view text, const EvaluationContext& context, int depth) const {
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
            CursorTarget cursorTarget = CursorTarget::SampChat;
            if (TryGetCursorTarget(ToLower(name), cursorTarget)) {
                pos = openParen + 1;
                continue;
            }
            if (const std::optional<std::string> value = ResolveFunctionTag(name, {}, context, depth);
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
        CursorTarget cursorTarget = CursorTarget::SampChat;
        if (TryGetCursorTarget(ToLower(name), cursorTarget)) {
            if (const std::optional<std::pair<int, int>> range = ParseCursorFunctionRange(rawParam); range.has_value()) {
                output += MakeCursorFunctionSentinel(cursorTarget, range->first, range->second);
            }
            pos = cursor + 2;
            continue;
        }
        if (const std::optional<std::string> value = ResolveFunctionTag(
                name,
                rawParam,
                context,
                depth);
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
    if (depth > kRecursionLimit) {
        return std::string(text);
    }

    const std::string withFunctions = ExpandFunctionTags(text, context, depth);
    return ExpandSimpleTags(withFunctions, context);
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
    return ExpandTextRecursive(text, effective, 0);
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
    result.text = ExpandTextRecursive(text, effective, 0);
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
