#include "tags_module_impl.h"
#include "tags_module_detail.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr unsigned int kCompatibleCp1251 = 1251;

struct CompatibleText {
    std::wstring wide{};
    unsigned int codePage = CP_UTF8;
    bool valid = false;
};

CompatibleText DecodeCompatibleText(std::string_view text) {
    CompatibleText decoded;
    if (text.size() > kClipboardTagMaxLength) {
        return decoded;
    }

    decoded.wide = Utf8ToWide(text);
    if (!text.empty() && decoded.wide.empty()) {
        decoded.wide = MultiByteToWide(text, kCompatibleCp1251);
        decoded.codePage = kCompatibleCp1251;
    }
    decoded.valid = text.empty() || !decoded.wide.empty();
    return decoded;
}

std::string EncodeCompatibleText(const CompatibleText& decoded, std::wstring_view text) {
    if (!decoded.valid) {
        return {};
    }
    std::string result = WideToMultiByte(text, decoded.codePage);
    return result.size() <= kClipboardTagMaxLength ? result : std::string();
}

bool IsHighSurrogate(wchar_t value) {
    return value >= 0xD800 && value <= 0xDBFF;
}

bool IsLowSurrogate(wchar_t value) {
    return value >= 0xDC00 && value <= 0xDFFF;
}

std::size_t NextCodePoint(std::wstring_view text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    return position + 1 < text.size()
            && IsHighSurrogate(text[position])
            && IsLowSurrogate(text[position + 1])
        ? position + 2
        : position + 1;
}

bool IsUnicodeWhitespace(wchar_t value) {
    WORD type = 0;
    return GetStringTypeW(CT_CTYPE1, &value, 1, &type) != FALSE
        && (type & C1_SPACE) != 0;
}

struct TransliterationMapping {
    std::string_view lower;
    std::string_view upper;
    std::string_view latin;
};

constexpr std::array<TransliterationMapping, 33> kCyrillicMappings{{
    {"а", "А", "a"},
    {"б", "Б", "b"},
    {"в", "В", "v"},
    {"г", "Г", "g"},
    {"д", "Д", "d"},
    {"е", "Е", "e"},
    {"ё", "Ё", "yo"},
    {"ж", "Ж", "zh"},
    {"з", "З", "z"},
    {"и", "И", "i"},
    {"й", "Й", "j"},
    {"к", "К", "k"},
    {"л", "Л", "l"},
    {"м", "М", "m"},
    {"н", "Н", "n"},
    {"о", "О", "o"},
    {"п", "П", "p"},
    {"р", "Р", "r"},
    {"с", "С", "s"},
    {"т", "Т", "t"},
    {"у", "У", "u"},
    {"ф", "Ф", "f"},
    {"х", "Х", "kh"},
    {"ц", "Ц", "ts"},
    {"ч", "Ч", "ch"},
    {"ш", "Ш", "sh"},
    {"щ", "Щ", "shch"},
    {"ъ", "Ъ", "\""},
    {"ы", "Ы", "y"},
    {"ь", "Ь", "'"},
    {"э", "Э", "eh"},
    {"ю", "Ю", "yu"},
    {"я", "Я", "ya"},
}};

constexpr std::array<TransliterationMapping, 14> kLatinAliases{{
    {"ц", "Ц", "cz"},
    {"щ", "Щ", "shh"},
    {"ы", "Ы", "y`"},
    {"э", "Э", "e`"},
    {"х", "Х", "x"},
    {"в", "В", "w"},
    {"ь", "Ь", "`"},
    {"ъ", "Ъ", "``"},
    {"т", "Т", "th"},
    {"ф", "Ф", "ph"},
    {"к", "К", "ck"},
    {"к", "К", "c"},
    {"х", "Х", "h"},
    {"к", "К", "q"},
}};

constexpr std::size_t kMaxDictionaryWordBytes = 128;

constexpr bool LatinMappingComesBefore(
    const TransliterationMapping& left,
    const TransliterationMapping& right) {
    const unsigned char leftFirst = static_cast<unsigned char>(left.latin.front());
    const unsigned char rightFirst = static_cast<unsigned char>(right.latin.front());
    return leftFirst != rightFirst ? leftFirst < rightFirst : left.latin.size() > right.latin.size();
}

constexpr auto BuildLatinMappings() {
    std::array<TransliterationMapping, kCyrillicMappings.size() + kLatinAliases.size()> mappings{};
    std::size_t count = 0;
    for (const TransliterationMapping& mapping : kCyrillicMappings) {
        mappings[count++] = mapping;
    }
    for (const TransliterationMapping& mapping : kLatinAliases) {
        mappings[count++] = mapping;
    }

    for (std::size_t i = 1; i < mappings.size(); ++i) {
        const TransliterationMapping current = mappings[i];
        std::size_t position = i;
        while (position > 0 && LatinMappingComesBefore(current, mappings[position - 1])) {
            mappings[position] = mappings[position - 1];
            --position;
        }
        mappings[position] = current;
    }
    return mappings;
}

// Entries with the same first byte are contiguous and ordered longest-first.
constexpr auto kLatinMappings = BuildLatinMappings();

struct LatinMappingRange {
    std::uint8_t begin{};
    std::uint8_t count{};
};

constexpr bool LatinMappingsAreGroupedLongestFirst() {
    std::array<bool, 128> closedGroups{};
    unsigned char previous = 0;

    for (std::size_t i = 0; i < kLatinMappings.size(); ++i) {
        const unsigned char current = static_cast<unsigned char>(kLatinMappings[i].latin.front());
        if (current >= closedGroups.size()) {
            return false;
        }
        if (i != 0 && current != previous) {
            closedGroups[previous] = true;
            if (closedGroups[current]) {
                return false;
            }
        }
        if (i != 0 && current == previous
            && kLatinMappings[i - 1].latin.size() < kLatinMappings[i].latin.size()) {
            return false;
        }
        previous = current;
    }
    return true;
}

static_assert(kLatinMappings.size() <= 255);
static_assert(LatinMappingsAreGroupedLongestFirst());

constexpr std::array<LatinMappingRange, 128> BuildLatinMappingRanges() {
    std::array<LatinMappingRange, 128> ranges{};
    for (std::size_t i = 0; i < kLatinMappings.size(); ++i) {
        const unsigned char first = static_cast<unsigned char>(kLatinMappings[i].latin.front());
        LatinMappingRange& range = ranges[first];
        if (range.count == 0) {
            range.begin = static_cast<std::uint8_t>(i);
        }
        ++range.count;
    }
    return ranges;
}

constexpr auto kLatinMappingRanges = BuildLatinMappingRanges();

struct RomanMapping {
    int value;
    std::string_view text;
};

constexpr std::array<RomanMapping, 13> kRomanMappings{{
    {1000, "M"},
    {900, "CM"},
    {500, "D"},
    {400, "CD"},
    {100, "C"},
    {90, "XC"},
    {50, "L"},
    {40, "XL"},
    {10, "X"},
    {9, "IX"},
    {5, "V"},
    {4, "IV"},
    {1, "I"},
}};

char ToLowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

char ToUpperAscii(char ch) {
    return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
}

bool IsUpperAscii(char ch) {
    return ch >= 'A' && ch <= 'Z';
}

bool IsAsciiLetter(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

enum class WordCase {
    Lower,
    Upper,
    Title,
    Mixed,
};

bool IsAsciiWhitespace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

std::string_view TrimTransliterationAsciiWhitespace(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && IsAsciiWhitespace(value[begin])) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && IsAsciiWhitespace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool DecodeRussianLetter(
    std::string_view text,
    std::size_t position,
    const TransliterationMapping*& mapping,
    bool& uppercase,
    std::size_t& byteCount) {
    if (position + 1 >= text.size()) {
        return false;
    }

    const unsigned char lead = static_cast<unsigned char>(text[position]);
    const unsigned char continuation = static_cast<unsigned char>(text[position + 1]);
    if ((lead != 0xD0 && lead != 0xD1) || (continuation & 0xC0) != 0x80) {
        return false;
    }

    const std::uint16_t codePoint = static_cast<std::uint16_t>(((lead & 0x1F) << 6) | (continuation & 0x3F));
    std::size_t mappingIndex = 0;
    if (codePoint == 0x0401 || codePoint == 0x0451) {
        mappingIndex = 6;
        uppercase = codePoint == 0x0401;
    } else {
        uppercase = codePoint >= 0x0410 && codePoint <= 0x042F;
        const std::uint16_t lowerCodePoint = uppercase ? static_cast<std::uint16_t>(codePoint + 0x20) : codePoint;
        if (lowerCodePoint < 0x0430 || lowerCodePoint > 0x044F) {
            return false;
        }
        mappingIndex = lowerCodePoint <= 0x0435
            ? static_cast<std::size_t>(lowerCodePoint - 0x0430)
            : static_cast<std::size_t>(lowerCodePoint - 0x0430 + 1);
    }

    mapping = &kCyrillicMappings[mappingIndex];
    byteCount = 2;
    return true;
}

void AppendLatinToken(std::string& output, std::string_view token, bool uppercase, bool allUppercaseWord) {
    if (!uppercase) {
        output.append(token);
        return;
    }

    const std::size_t begin = output.size();
    output.append(token);
    if (allUppercaseWord) {
        for (std::size_t i = begin; i < output.size(); ++i) {
            output[i] = ToUpperAscii(output[i]);
        }
    } else if (begin < output.size()) {
        output[begin] = ToUpperAscii(output[begin]);
    }
}

void AppendDictionaryLatin(std::string& output, std::string_view value, WordCase wordCase) {
    if (wordCase == WordCase::Mixed) {
        output.append(value);
        return;
    }

    const std::size_t begin = output.size();
    output.append(value);
    for (std::size_t i = begin; i < output.size(); ++i) {
        output[i] = wordCase == WordCase::Upper ? ToUpperAscii(output[i]) : ToLowerAscii(output[i]);
    }
    if (wordCase == WordCase::Title && begin < output.size()) {
        output[begin] = ToUpperAscii(output[begin]);
    }
}

void AppendDictionaryCyrillic(std::string& output, std::string_view value, WordCase wordCase) {
    if (wordCase == WordCase::Mixed) {
        output.append(value);
        return;
    }

    std::size_t position = 0;
    std::size_t letterIndex = 0;
    while (position < value.size()) {
        const TransliterationMapping* mapping = nullptr;
        bool uppercase = false;
        std::size_t byteCount = 0;
        if (!DecodeRussianLetter(value, position, mapping, uppercase, byteCount)) {
            output.push_back(value[position++]);
            continue;
        }

        const bool outputUppercase = wordCase == WordCase::Upper
            || (wordCase == WordCase::Title && letterIndex == 0);
        output.append(outputUppercase ? mapping->upper : mapping->lower);
        position += byteCount;
        ++letterIndex;
    }
}

template <typename DictionaryLookup>
std::string CyrillicToLatin(
    std::string_view text,
    bool useDictionary,
    DictionaryLookup&& findDictionaryWord) {
    std::string output;
    output.reserve(text.size() <= output.max_size() / 2 ? text.size() * 2 : text.size());
    std::string dictionaryKey;
    if (useDictionary) {
        dictionaryKey.reserve(kMaxDictionaryWordBytes);
    }

    std::size_t position = 0;
    while (position < text.size()) {
        const TransliterationMapping* mapping = nullptr;
        bool uppercase = false;
        std::size_t byteCount = 0;
        if (!DecodeRussianLetter(text, position, mapping, uppercase, byteCount)) {
            output.push_back(text[position++]);
            continue;
        }

        const std::size_t wordBegin = position;
        std::size_t wordEnd = position;
        bool allUppercaseWord = true;
        bool allLowercaseWord = true;
        bool titleWord = true;
        bool dictionaryCandidate = useDictionary;
        std::size_t letterIndex = 0;
        dictionaryKey.clear();
        while (wordEnd < text.size()) {
            const TransliterationMapping* letter = nullptr;
            bool letterUppercase = false;
            std::size_t letterByteCount = 0;
            if (!DecodeRussianLetter(text, wordEnd, letter, letterUppercase, letterByteCount)) {
                break;
            }
            allUppercaseWord = allUppercaseWord && letterUppercase;
            allLowercaseWord = allLowercaseWord && !letterUppercase;
            titleWord = titleWord && (letterIndex == 0 ? letterUppercase : !letterUppercase);
            if (dictionaryCandidate) {
                if (dictionaryKey.size() + letter->lower.size() <= kMaxDictionaryWordBytes) {
                    dictionaryKey.append(letter->lower);
                } else {
                    dictionaryCandidate = false;
                }
            }
            wordEnd += letterByteCount;
            ++letterIndex;
        }

        if (dictionaryCandidate) {
            if (const std::string* const dictionaryValue = findDictionaryWord(dictionaryKey)) {
                const WordCase wordCase = allUppercaseWord
                    ? WordCase::Upper
                    : allLowercaseWord ? WordCase::Lower : titleWord ? WordCase::Title : WordCase::Mixed;
                AppendDictionaryLatin(output, *dictionaryValue, wordCase);
                position = wordEnd;
                continue;
            }
        }

        position = wordBegin;
        while (position < wordEnd) {
            DecodeRussianLetter(text, position, mapping, uppercase, byteCount);
            AppendLatinToken(output, mapping->latin, uppercase, allUppercaseWord);
            position += byteCount;
        }
    }
    return output;
}

bool MatchesLatinToken(std::string_view text, std::size_t position, std::string_view token) {
    if (position > text.size() || token.size() > text.size() - position) {
        return false;
    }
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (ToLowerAscii(text[position + i]) != token[i]) {
            return false;
        }
    }
    return true;
}

bool MatchedTokenIsUppercase(std::string_view text, std::size_t position, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        const char ch = text[position + i];
        if (IsUpperAscii(ch)) {
            return true;
        }
        if (ch >= 'a' && ch <= 'z') {
            return false;
        }
    }
    return false;
}

const TransliterationMapping* FindLatinMapping(std::string_view text, std::size_t position) {
    const unsigned char first = static_cast<unsigned char>(ToLowerAscii(text[position]));
    if (first >= kLatinMappingRanges.size()) {
        return nullptr;
    }

    const LatinMappingRange range = kLatinMappingRanges[first];
    for (std::size_t offset = 0; offset < range.count; ++offset) {
        const TransliterationMapping& mapping = kLatinMappings[range.begin + offset];
        if (MatchesLatinToken(text, position, mapping.latin)) {
            return &mapping;
        }
    }
    return nullptr;
}

template <typename DictionaryLookup>
std::string LatinToCyrillic(
    std::string_view text,
    bool useDictionary,
    DictionaryLookup&& findDictionaryWord) {
    std::string output;
    output.reserve(text.size() <= output.max_size() / 2 ? text.size() * 2 : text.size());
    std::string dictionaryKey;
    if (useDictionary) {
        dictionaryKey.reserve(kMaxDictionaryWordBytes);
    }

    std::size_t position = 0;
    while (position < text.size()) {
        if (useDictionary
            && IsAsciiLetter(text[position])
            && (position == 0 || !IsAsciiLetter(text[position - 1]))) {
            std::size_t wordEnd = position;
            bool allUppercaseWord = true;
            bool allLowercaseWord = true;
            bool titleWord = true;
            bool dictionaryCandidate = true;
            std::size_t letterIndex = 0;
            dictionaryKey.clear();
            while (wordEnd < text.size() && IsAsciiLetter(text[wordEnd])) {
                const bool uppercase = IsUpperAscii(text[wordEnd]);
                allUppercaseWord = allUppercaseWord && uppercase;
                allLowercaseWord = allLowercaseWord && !uppercase;
                titleWord = titleWord && (letterIndex == 0 ? uppercase : !uppercase);
                if (dictionaryCandidate) {
                    if (dictionaryKey.size() < kMaxDictionaryWordBytes) {
                        dictionaryKey.push_back(ToLowerAscii(text[wordEnd]));
                    } else {
                        dictionaryCandidate = false;
                    }
                }
                ++wordEnd;
                ++letterIndex;
            }

            if (dictionaryCandidate) {
                if (const std::string* const dictionaryValue = findDictionaryWord(dictionaryKey)) {
                    const WordCase wordCase = allUppercaseWord
                        ? WordCase::Upper
                        : allLowercaseWord ? WordCase::Lower : titleWord ? WordCase::Title : WordCase::Mixed;
                    AppendDictionaryCyrillic(
                        output,
                        *dictionaryValue,
                        wordCase);
                    position = wordEnd;
                    continue;
                }
            }
        }

        const TransliterationMapping* const mapping = FindLatinMapping(text, position);
        if (!mapping) {
            output.push_back(text[position++]);
            continue;
        }

        output.append(MatchedTokenIsUppercase(text, position, mapping->latin.size()) ? mapping->upper : mapping->lower);
        position += mapping->latin.size();
    }
    return output;
}

std::optional<int> ParseArabicNumber(std::string_view rawValue) {
    const std::string_view value = TrimTransliterationAsciiWhitespace(rawValue);
    if (value.empty()) {
        return std::nullopt;
    }

    int parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        parsed = parsed * 10 + (ch - '0');
        if (parsed > 3999) {
            return std::nullopt;
        }
    }
    return parsed >= 1 ? std::optional<int>(parsed) : std::nullopt;
}

std::string ArabicToRoman(int value) {
    std::string output;
    output.reserve(15);
    for (const RomanMapping& mapping : kRomanMappings) {
        while (value >= mapping.value) {
            output.append(mapping.text);
            value -= mapping.value;
        }
    }
    return output;
}

int RomanDigitValue(char ch) {
    switch (ch) {
    case 'I':
        return 1;
    case 'V':
        return 5;
    case 'X':
        return 10;
    case 'L':
        return 50;
    case 'C':
        return 100;
    case 'D':
        return 500;
    case 'M':
        return 1000;
    default:
        return 0;
    }
}

std::optional<int> ParseCanonicalRoman(std::string_view rawValue) {
    const std::string_view trimmed = TrimTransliterationAsciiWhitespace(rawValue);
    if (trimmed.empty() || trimmed.size() > 15) {
        return std::nullopt;
    }

    std::array<char, 15> normalized{};
    int value = 0;
    int previous = 0;
    for (std::size_t i = 0; i < trimmed.size(); ++i) {
        const char ch = trimmed[i];
        const char uppercase = ToUpperAscii(ch);
        const int current = RomanDigitValue(uppercase);
        if (current == 0) {
            return std::nullopt;
        }
        normalized[i] = uppercase;
        value += current;
        if (previous < current) {
            value -= previous * 2;
        }
        previous = current;
    }

    const std::string canonical = value >= 1 && value <= 3999 ? ArabicToRoman(value) : std::string();
    if (canonical.size() != trimmed.size()
        || canonical.compare(0, canonical.size(), normalized.data(), trimmed.size()) != 0) {
        return std::nullopt;
    }
    return value;
}
} // namespace

std::optional<std::string> TagsModule::Impl::ResolveBuiltinCyrToLatFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    return CyrillicToLatin(
        param,
        HasTransliterationDictionary(),
        [this](std::string_view word) { return FindCyrillicDictionaryWord(word); });
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinLatToCyrFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    return LatinToCyrillic(
        param,
        HasTransliterationDictionary(),
        [this](std::string_view word) { return FindLatinDictionaryWord(word); });
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinToRomanFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::optional<int> value = ParseArabicNumber(param);
    return value.has_value() ? ArabicToRoman(*value) : std::string(param);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinFromRomanFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const std::optional<int> value = ParseCanonicalRoman(param);
    return value.has_value() ? std::to_string(*value) : std::string(param);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinStrUpperFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    CompatibleText decoded = DecodeCompatibleText(param);
    if (!decoded.valid || decoded.wide.empty()) {
        return std::string();
    }

    const int required = LCMapStringEx(
        LOCALE_NAME_INVARIANT,
        LCMAP_UPPERCASE,
        decoded.wide.data(),
        static_cast<int>(decoded.wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr,
        0);
    if (required <= 0 || required > static_cast<int>(kClipboardTagMaxLength)) {
        return std::string();
    }

    std::wstring upper(static_cast<std::size_t>(required), L'\0');
    if (LCMapStringEx(
            LOCALE_NAME_INVARIANT,
            LCMAP_UPPERCASE,
            decoded.wide.data(),
            static_cast<int>(decoded.wide.size()),
            upper.data(),
            required,
            nullptr,
            nullptr,
            0) != required) {
        return std::string();
    }
    return EncodeCompatibleText(decoded, upper);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinTrimFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const CompatibleText decoded = DecodeCompatibleText(param);
    if (!decoded.valid) {
        return std::string();
    }

    std::size_t begin = 0;
    while (begin < decoded.wide.size() && IsUnicodeWhitespace(decoded.wide[begin])) {
        ++begin;
    }
    std::size_t end = decoded.wide.size();
    while (end > begin && IsUnicodeWhitespace(decoded.wide[end - 1])) {
        --end;
    }
    return EncodeCompatibleText(decoded, std::wstring_view(decoded.wide).substr(begin, end - begin));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinSubstrFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    if (param.size() > kClipboardTagMaxLength) {
        return std::string();
    }

    const std::vector<std::string_view> parts = SplitTopLevelDelimitedParts(param, ';');
    if (parts.size() != 2 && parts.size() != 3) {
        return std::string();
    }

    return ResolveBuiltinSubstrFunctionParts(
        parts[0],
        parts[1],
        parts.size() == 3 ? std::optional<std::string_view>(parts[2]) : std::nullopt);
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinSubstrFunctionParts(
    std::string_view text,
    std::string_view startText,
    std::optional<std::string_view> lengthText) const {
    if (text.size() > kClipboardTagMaxLength) {
        return std::string();
    }

    const std::optional<int> start = ParseInteger(TrimView(startText));
    const std::optional<int> length =
        lengthText.has_value() ? ParseInteger(TrimView(*lengthText)) : std::optional<int>{};
    if (!start.has_value() || *start <= 0
        || (lengthText.has_value() && (!length.has_value() || *length < 0))) {
        return std::string();
    }

    const CompatibleText decoded = DecodeCompatibleText(text);
    if (!decoded.valid) {
        return std::string();
    }

    std::size_t begin = 0;
    for (int index = 1; index < *start && begin < decoded.wide.size(); ++index) {
        begin = NextCodePoint(decoded.wide, begin);
    }
    if (begin >= decoded.wide.size()) {
        return std::string();
    }

    std::size_t end = begin;
    if (!lengthText.has_value()) {
        end = decoded.wide.size();
    } else {
        for (int index = 0; index < *length && end < decoded.wide.size(); ++index) {
            end = NextCodePoint(decoded.wide, end);
        }
    }
    return EncodeCompatibleText(decoded, std::wstring_view(decoded.wide).substr(begin, end - begin));
}

std::optional<std::string> TagsModule::Impl::ResolveBuiltinStrlenFunctionTag(
    std::string_view param,
    const EvaluationContext&) const {
    const CompatibleText decoded = DecodeCompatibleText(param);
    if (!decoded.valid) {
        return std::string();
    }

    std::size_t count = 0;
    for (std::size_t position = 0; position < decoded.wide.size(); ++count) {
        position = NextCodePoint(decoded.wide, position);
    }
    return std::to_string(count);
}
