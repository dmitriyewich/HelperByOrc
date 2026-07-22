#pragma once

#include "binder_types.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace binder_editor::message_edit {

namespace detail {

inline constexpr std::size_t CommonEdgeByteCount(std::string_view left, std::string_view right) {
    const std::size_t commonLimit = std::min(left.size(), right.size());
    std::size_t prefix = 0;
    while (prefix < commonLimit && left[prefix] == right[prefix]) {
        ++prefix;
    }

    std::size_t suffix = 0;
    while (suffix < commonLimit - prefix && left[left.size() - suffix - 1] == right[right.size() - suffix - 1]) {
        ++suffix;
    }
    return prefix + suffix;
}

inline constexpr void CopySettings(HotkeyMessage& target, const HotkeyMessage& source) {
    target.intervalMs = std::max(source.intervalMs, 0);
    target.method = source.method;
}

} // namespace detail

struct MultilinePasteResult {
    std::vector<std::string> lines{};
    int focusLine = 0;
    int caretByte = 0;
};

inline constexpr std::vector<HotkeyMessage> ReconcileMessages(const std::vector<std::string>& lines,
    const std::vector<HotkeyMessage>& reference,
    int defaultIntervalMs,
    int defaultMethod) {
    std::vector<HotkeyMessage> result;
    result.reserve(lines.size());
    for (const std::string& line : lines) {
        result.push_back(HotkeyMessage{ line, std::max(defaultIntervalMs, 0), defaultMethod });
    }

    std::vector<bool> referenceUsed(reference.size(), false);
    std::vector<bool> resultMatched(result.size(), false);

    // Exact text is the strongest identity available in the plain-text
    // multi-input. Match duplicate occurrences in their original order for
    // deterministic metadata ownership.
    for (std::size_t resultIndex = 0; resultIndex < result.size(); ++resultIndex) {
        for (std::size_t referenceIndex = 0; referenceIndex < reference.size(); ++referenceIndex) {
            if (referenceUsed[referenceIndex] || reference[referenceIndex].text != result[resultIndex].text) {
                continue;
            }
            detail::CopySettings(result[resultIndex], reference[referenceIndex]);
            referenceUsed[referenceIndex] = true;
            resultMatched[resultIndex] = true;
            break;
        }
    }

    // Prefer a changed row that still shares its beginning or ending with the old
    // text. This separates an edited row from an unrelated insertion in the same
    // operation.
    while (true) {
        std::size_t bestResult = result.size();
        std::size_t bestReference = reference.size();
        std::size_t bestOverlap = 0;
        std::size_t bestLengthDelta = std::numeric_limits<std::size_t>::max();
        std::size_t bestPositionDelta = std::numeric_limits<std::size_t>::max();

        for (std::size_t resultIndex = 0; resultIndex < result.size(); ++resultIndex) {
            if (resultMatched[resultIndex]) {
                continue;
            }
            for (std::size_t referenceIndex = 0; referenceIndex < reference.size(); ++referenceIndex) {
                if (referenceUsed[referenceIndex]) {
                    continue;
                }

                const std::size_t overlap =
                    detail::CommonEdgeByteCount(result[resultIndex].text, reference[referenceIndex].text);
                if (overlap == 0) {
                    continue;
                }
                const std::size_t lengthDelta =
                    result[resultIndex].text.size() > reference[referenceIndex].text.size()
                        ? result[resultIndex].text.size() - reference[referenceIndex].text.size()
                        : reference[referenceIndex].text.size() - result[resultIndex].text.size();
                const std::size_t positionDelta =
                    resultIndex > referenceIndex ? resultIndex - referenceIndex : referenceIndex - resultIndex;
                if (overlap > bestOverlap || (overlap == bestOverlap && lengthDelta < bestLengthDelta) ||
                    (overlap == bestOverlap && lengthDelta == bestLengthDelta && positionDelta < bestPositionDelta)) {
                    bestResult = resultIndex;
                    bestReference = referenceIndex;
                    bestOverlap = overlap;
                    bestLengthDelta = lengthDelta;
                    bestPositionDelta = positionDelta;
                }
            }
        }

        if (bestResult == result.size()) {
            break;
        }
        detail::CopySettings(result[bestResult], reference[bestReference]);
        referenceUsed[bestReference] = true;
        resultMatched[bestResult] = true;
    }

    const std::size_t unmatchedResults =
        static_cast<std::size_t>(std::count(resultMatched.begin(), resultMatched.end(), false));
    const std::size_t unusedReferences =
        static_cast<std::size_t>(std::count(referenceUsed.begin(), referenceUsed.end(), false));
    if (unmatchedResults > unusedReferences) {
        return result;
    }

    // With no surplus new rows, remaining pairs are replacements/deletions.
    // Preserve the nearest old row's settings even when its text was rewritten
    // completely.
    for (std::size_t resultIndex = 0; resultIndex < result.size(); ++resultIndex) {
        if (resultMatched[resultIndex]) {
            continue;
        }

        std::size_t nearestIndex = reference.size();
        std::size_t nearestDistance = std::numeric_limits<std::size_t>::max();
        for (std::size_t referenceIndex = 0; referenceIndex < reference.size(); ++referenceIndex) {
            if (referenceUsed[referenceIndex]) {
                continue;
            }
            const std::size_t distance =
                referenceIndex > resultIndex ? referenceIndex - resultIndex : resultIndex - referenceIndex;
            if (distance < nearestDistance) {
                nearestIndex = referenceIndex;
                nearestDistance = distance;
            }
        }
        if (nearestIndex == reference.size()) {
            continue;
        }

        detail::CopySettings(result[resultIndex], reference[nearestIndex]);
        referenceUsed[nearestIndex] = true;
    }

    return result;
}

inline constexpr bool IsBlankLine(std::string_view line) {
    return std::all_of(line.begin(), line.end(), [](char ch) {
        return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
    });
}

inline constexpr std::optional<MultilinePasteResult> BuildMultilinePaste(
    std::string_view original, int selectionStartByte, int selectionEndByte, std::string_view normalizedClipboard) {
    if (normalizedClipboard.find('\n') == std::string_view::npos) {
        return std::nullopt;
    }

    const int textSize = static_cast<int>(original.size());
    const int selectionStart = std::clamp(std::min(selectionStartByte, selectionEndByte), 0, textSize);
    const int selectionEnd = std::clamp(std::max(selectionStartByte, selectionEndByte), selectionStart, textSize);

    std::string merged;
    merged.reserve(original.size() + normalizedClipboard.size());
    merged.append(original.substr(0, static_cast<std::size_t>(selectionStart)));
    merged.append(normalizedClipboard);
    const std::size_t insertionEnd = merged.size();
    merged.append(original.substr(static_cast<std::size_t>(selectionEnd)));

    int targetRawLine = 0;
    std::size_t targetRawStart = 0;
    for (std::size_t i = 0; i < insertionEnd; ++i) {
        if (merged[i] == '\n') {
            ++targetRawLine;
            targetRawStart = i + 1;
        }
    }

    MultilinePasteResult result;
    std::vector<int> rawToOutput;
    std::size_t lineStart = 0;
    while (lineStart <= merged.size()) {
        const std::size_t lineEnd = merged.find('\n', lineStart);
        const std::size_t boundedEnd = lineEnd == std::string::npos ? merged.size() : lineEnd;
        const std::string_view line(merged.data() + lineStart, boundedEnd - lineStart);
        if (IsBlankLine(line)) {
            rawToOutput.push_back(-1);
        } else {
            rawToOutput.push_back(static_cast<int>(result.lines.size()));
            result.lines.emplace_back(line);
        }
        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }

    if (result.lines.empty()) {
        result.lines.emplace_back();
        return result;
    }

    targetRawLine = std::clamp(targetRawLine, 0, static_cast<int>(rawToOutput.size()) - 1);
    if (rawToOutput[static_cast<std::size_t>(targetRawLine)] >= 0) {
        result.focusLine = rawToOutput[static_cast<std::size_t>(targetRawLine)];
        result.caretByte = std::clamp(static_cast<int>(insertionEnd - targetRawStart),
            0,
            static_cast<int>(result.lines[static_cast<std::size_t>(result.focusLine)].size()));
        return result;
    }

    for (int rawIndex = targetRawLine - 1; rawIndex >= 0; --rawIndex) {
        const int outputIndex = rawToOutput[static_cast<std::size_t>(rawIndex)];
        if (outputIndex >= 0) {
            result.focusLine = outputIndex;
            result.caretByte = static_cast<int>(result.lines[static_cast<std::size_t>(outputIndex)].size());
            return result;
        }
    }
    for (std::size_t rawIndex = static_cast<std::size_t>(targetRawLine + 1); rawIndex < rawToOutput.size();
        ++rawIndex) {
        const int outputIndex = rawToOutput[rawIndex];
        if (outputIndex >= 0) {
            result.focusLine = outputIndex;
            result.caretByte = 0;
            return result;
        }
    }
    return result;
}

inline constexpr std::vector<HotkeyMessage> BuildMultilinePasteMessages(
    const HotkeyMessage& inherited, const MultilinePasteResult& paste) {
    std::vector<HotkeyMessage> messages;
    messages.reserve(paste.lines.size());
    for (const std::string& line : paste.lines) {
        HotkeyMessage message = inherited;
        message.text = line;
        messages.push_back(std::move(message));
    }
    return messages;
}

namespace detail {

consteval bool ReconcileContract() {
    const std::vector<HotkeyMessage> reference{
        { "A", 100, 1 },
        { "B", 200, 2 },
        { "C", 300, 3 },
    };

    const auto unchanged = ReconcileMessages({ "A", "B", "C" }, reference, 900, 9);
    if (unchanged.size() != reference.size()
        || unchanged[0].text != "A" || unchanged[0].intervalMs != 100 || unchanged[0].method != 1
        || unchanged[1].text != "B" || unchanged[1].intervalMs != 200 || unchanged[1].method != 2
        || unchanged[2].text != "C" || unchanged[2].intervalMs != 300 || unchanged[2].method != 3) {
        return false;
    }

    const auto inserted = ReconcileMessages({ "A", "X", "B", "C" }, reference, 900, 9);
    if (inserted.size() != 4 || inserted[0].intervalMs != 100 || inserted[0].method != 1 ||
        inserted[1].intervalMs != 900 || inserted[1].method != 9 || inserted[2].intervalMs != 200 ||
        inserted[2].method != 2 || inserted[3].intervalMs != 300 || inserted[3].method != 3) {
        return false;
    }

    const auto insertedMultiple = ReconcileMessages({ "A", "X", "Y", "B", "C" }, reference, 900, 9);
    if (insertedMultiple[1].intervalMs != 900 || insertedMultiple[1].method != 9 ||
        insertedMultiple[2].intervalMs != 900 || insertedMultiple[2].method != 9 ||
        insertedMultiple[3].intervalMs != 200 || insertedMultiple[4].intervalMs != 300) {
        return false;
    }

    const auto insertedAtEdges = ReconcileMessages({ "X", "A", "B", "C", "Y" }, reference, 900, 9);
    if (insertedAtEdges.front().intervalMs != 900 || insertedAtEdges.front().method != 9 ||
        insertedAtEdges[1].intervalMs != 100 || insertedAtEdges[2].intervalMs != 200 ||
        insertedAtEdges[3].intervalMs != 300 || insertedAtEdges.back().intervalMs != 900 ||
        insertedAtEdges.back().method != 9) {
        return false;
    }

    const auto edited = ReconcileMessages({ "A", "BX", "C" }, reference, 900, 9);
    if (edited[1].intervalMs != 200 || edited[1].method != 2) {
        return false;
    }

    const auto insertedAndEdited = ReconcileMessages({ "A", "X", "BX", "C" }, reference, 900, 9);
    if (insertedAndEdited[1].intervalMs != 900 || insertedAndEdited[1].method != 9 ||
        insertedAndEdited[2].intervalMs != 200 || insertedAndEdited[2].method != 2) {
        return false;
    }

    const auto removed = ReconcileMessages({ "B", "C" }, reference, 900, 9);
    if (removed[0].intervalMs != 200 || removed[1].intervalMs != 300) {
        return false;
    }
    const auto removedMiddle = ReconcileMessages({ "A", "C" }, reference, 900, 9);
    const auto removedEnd = ReconcileMessages({ "A", "B" }, reference, 900, 9);
    if (removedMiddle[0].intervalMs != 100 || removedMiddle[1].intervalMs != 300 || removedEnd[0].intervalMs != 100 ||
        removedEnd[1].intervalMs != 200) {
        return false;
    }

    const auto rewritten = ReconcileMessages({ "X", "Y", "Z" }, reference, 900, 9);
    if (rewritten[0].intervalMs != 100 || rewritten[1].intervalMs != 200 || rewritten[2].intervalMs != 300) {
        return false;
    }

    const auto replacedMiddle = ReconcileMessages({ "A", "X", "C" }, reference, 900, 9);
    if (replacedMiddle[0].intervalMs != 100 || replacedMiddle[1].intervalMs != 200 ||
        replacedMiddle[1].method != 2 || replacedMiddle[2].intervalMs != 300) {
        return false;
    }

    const auto reordered = ReconcileMessages({ "C", "A", "B" }, reference, 900, 9);
    if (reordered[0].intervalMs != 300 || reordered[1].intervalMs != 100 || reordered[2].intervalMs != 200) {
        return false;
    }

    const std::vector<HotkeyMessage> duplicates{
        { "A", 100, 1 },
        { "A", 200, 2 },
    };
    const auto duplicated = ReconcileMessages({ "A", "A", "A" }, duplicates, 900, 9);
    const auto clampedDefault = ReconcileMessages({ "X" }, {}, -10, 9);
    return duplicated[0].intervalMs == 100 && duplicated[1].intervalMs == 200 && duplicated[2].intervalMs == 900 &&
           clampedDefault[0].intervalMs == 0 && IsBlankLine(" \t\r") && !IsBlankLine(" x ");
}

consteval bool MultilinePasteContract() {
    const auto middle = BuildMultilinePaste("AB", 1, 1, "X\nY");
    if (!middle || middle->lines != std::vector<std::string>{ "AX", "YB" } || middle->focusLine != 1 ||
        middle->caretByte != 1) {
        return false;
    }
    const auto middleMessages = BuildMultilinePasteMessages({ "AB", 1574, 6 }, *middle);
    if (middleMessages.size() != 2 || middleMessages[0].text != "AX" || middleMessages[1].text != "YB" ||
        middleMessages[0].intervalMs != 1574 || middleMessages[1].intervalMs != 1574 || middleMessages[0].method != 6 ||
        middleMessages[1].method != 6) {
        return false;
    }

    const auto selection = BuildMultilinePaste("AB", 0, 2, "X\nY\n");
    if (!selection || selection->lines != std::vector<std::string>{ "X", "Y" } || selection->focusLine != 1 ||
        selection->caretByte != 1) {
        return false;
    }

    const auto before = BuildMultilinePaste("B", 0, 0, "X\nY\n");
    if (!before || before->lines != std::vector<std::string>{ "X", "Y", "B" } || before->focusLine != 2 ||
        before->caretByte != 0) {
        return false;
    }

    const auto after = BuildMultilinePaste("A", 1, 1, "X\nY");
    if (!after || after->lines != std::vector<std::string>{ "AX", "Y" } || after->focusLine != 1 ||
        after->caretByte != 1) {
        return false;
    }

    const auto leadingBreak = BuildMultilinePaste("B", 0, 0, "\nX");
    if (!leadingBreak || leadingBreak->lines != std::vector<std::string>{ "XB" } || leadingBreak->focusLine != 0 ||
        leadingBreak->caretByte != 1) {
        return false;
    }

    const auto duplicates = BuildMultilinePaste("A", 0, 1, "A\nA");
    if (!duplicates || duplicates->lines != std::vector<std::string>{ "A", "A" }) {
        return false;
    }

    if (BuildMultilinePaste("A", 0, 0, "X")) {
        return false;
    }

    const auto blanks = BuildMultilinePaste("AB", 1, 1, "\n \n");
    return blanks && blanks->lines == std::vector<std::string>{ "A", "B" };
}

static_assert(ReconcileContract());
static_assert(MultilinePasteContract());

} // namespace detail

} // namespace binder_editor::message_edit
