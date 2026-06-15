#include "tags_module_impl.h"
#include "tags_module_detail.h"

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
        if (normalized == "ifandor" || normalized == "dialogresponse" || normalized == "arzdialogsendrespond") {
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
