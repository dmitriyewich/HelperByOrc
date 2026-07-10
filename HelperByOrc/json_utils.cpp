#include "json_utils.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace jsonutil {
namespace {

std::string EscapeJsonString(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);

    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buffer[7]{};
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", ch);
                result += buffer;
            } else {
                result.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    return result;
}

void AppendCodePointUtf8(std::string& out, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        out.push_back(static_cast<char>(codePoint));
        return;
    }

    if (codePoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0u | ((codePoint >> 6) & 0x1Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        return;
    }

    if (codePoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0u | ((codePoint >> 12) & 0x0Fu)));
        out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
        return;
    }

    out.push_back(static_cast<char>(0xF0u | ((codePoint >> 18) & 0x07u)));
    out.push_back(static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
}

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {
    }

    std::optional<JsonValue> Parse(std::string& error) {
        SkipWhitespace();

        JsonValue value;
        if (!ParseValue(value, error)) {
            return std::nullopt;
        }

        SkipWhitespace();
        if (pos_ != source_.size()) {
            error = "unexpected trailing characters";
            return std::nullopt;
        }

        return value;
    }

private:
    void SkipWhitespace() {
        while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_])) != 0) {
            ++pos_;
        }
    }

    bool Match(std::string_view token) {
        if (source_.substr(pos_, token.size()) == token) {
            pos_ += token.size();
            return true;
        }
        return false;
    }

    bool ParseValue(JsonValue& out, std::string& error) {
        if (pos_ >= source_.size()) {
            error = "unexpected end of file";
            return false;
        }

        const char ch = source_[pos_];
        if (ch == '{') {
            return ParseObject(out, error);
        }
        if (ch == '[') {
            return ParseArray(out, error);
        }
        if (ch == '"') {
            std::string text;
            if (!ParseString(text, error)) {
                return false;
            }
            out = JsonValue(std::move(text));
            return true;
        }
        if (Match("true")) {
            out = JsonValue(true);
            return true;
        }
        if (Match("false")) {
            out = JsonValue(false);
            return true;
        }
        if (Match("null")) {
            out = JsonValue(nullptr);
            return true;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            double number = 0.0;
            if (!ParseNumber(number, error)) {
                return false;
            }
            out = JsonValue(number);
            return true;
        }

        error = "unexpected token";
        return false;
    }

    bool ParseObject(JsonValue& out, std::string& error) {
        JsonObject object;
        ++pos_;
        SkipWhitespace();

        if (pos_ < source_.size() && source_[pos_] == '}') {
            ++pos_;
            out = JsonValue(std::move(object));
            return true;
        }

        while (true) {
            SkipWhitespace();

            std::string key;
            if (!ParseString(key, error)) {
                return false;
            }

            SkipWhitespace();
            if (pos_ >= source_.size() || source_[pos_] != ':') {
                error = "expected ':'";
                return false;
            }
            ++pos_;

            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, error)) {
                return false;
            }

            object[std::move(key)] = std::move(value);

            SkipWhitespace();
            if (pos_ >= source_.size()) {
                error = "unexpected end of object";
                return false;
            }
            if (source_[pos_] == '}') {
                ++pos_;
                out = JsonValue(std::move(object));
                return true;
            }
            if (source_[pos_] != ',') {
                error = "expected ','";
                return false;
            }
            ++pos_;
        }
    }

    bool ParseArray(JsonValue& out, std::string& error) {
        JsonArray array;
        ++pos_;
        SkipWhitespace();

        if (pos_ < source_.size() && source_[pos_] == ']') {
            ++pos_;
            out = JsonValue(std::move(array));
            return true;
        }

        while (true) {
            SkipWhitespace();
            JsonValue value;
            if (!ParseValue(value, error)) {
                return false;
            }
            array.push_back(std::move(value));

            SkipWhitespace();
            if (pos_ >= source_.size()) {
                error = "unexpected end of array";
                return false;
            }
            if (source_[pos_] == ']') {
                ++pos_;
                out = JsonValue(std::move(array));
                return true;
            }
            if (source_[pos_] != ',') {
                error = "expected ','";
                return false;
            }
            ++pos_;
        }
    }

    bool ParseString(std::string& out, std::string& error) {
        if (pos_ >= source_.size() || source_[pos_] != '"') {
            error = "expected string";
            return false;
        }

        ++pos_;
        out.clear();

        while (pos_ < source_.size()) {
            const unsigned char ch = static_cast<unsigned char>(source_[pos_++]);
            if (ch == '"') {
                return true;
            }

            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }

            if (pos_ >= source_.size()) {
                error = "unterminated escape sequence";
                return false;
            }

            const char escaped = source_[pos_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escaped);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                if (pos_ + 4 > source_.size()) {
                    error = "invalid unicode escape";
                    return false;
                }

                unsigned int codePoint = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = source_[pos_++];
                    codePoint <<= 4;
                    if (hex >= '0' && hex <= '9') {
                        codePoint |= static_cast<unsigned int>(hex - '0');
                    } else if (hex >= 'a' && hex <= 'f') {
                        codePoint |= static_cast<unsigned int>(hex - 'a' + 10);
                    } else if (hex >= 'A' && hex <= 'F') {
                        codePoint |= static_cast<unsigned int>(hex - 'A' + 10);
                    } else {
                        error = "invalid unicode escape";
                        return false;
                    }
                }

                AppendCodePointUtf8(out, codePoint);
                break;
            }
            default:
                error = "invalid escape sequence";
                return false;
            }
        }

        error = "unterminated string";
        return false;
    }

    bool ParseNumber(double& out, std::string& error) {
        const std::size_t start = pos_;
        if (source_[pos_] == '-') {
            ++pos_;
        }

        if (pos_ >= source_.size()) {
            error = "invalid number";
            return false;
        }

        if (source_[pos_] == '0') {
            ++pos_;
        } else if (std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
            while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
                ++pos_;
            }
        } else {
            error = "invalid number";
            return false;
        }

        if (pos_ < source_.size() && source_[pos_] == '.') {
            ++pos_;
            if (pos_ >= source_.size() || std::isdigit(static_cast<unsigned char>(source_[pos_])) == 0) {
                error = "invalid number";
                return false;
            }

            while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
                ++pos_;
            }
        }

        if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) {
                ++pos_;
            }

            if (pos_ >= source_.size() || std::isdigit(static_cast<unsigned char>(source_[pos_])) == 0) {
                error = "invalid number";
                return false;
            }

            while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_])) != 0) {
                ++pos_;
            }
        }

        const std::string token(source_.substr(start, pos_ - start));
        char* end = nullptr;
        out = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0') {
            error = "invalid number";
            return false;
        }

        return true;
    }

    std::string_view source_;
    std::size_t pos_ = 0;
};

} // namespace

std::optional<JsonValue> ParseJson(std::string_view source, std::string& error) {
    JsonParser parser(source);
    return parser.Parse(error);
}

bool JsonEquals(const JsonValue& left, const JsonValue& right) {
    if (left.storage.index() != right.storage.index()) {
        return false;
    }

    if (left.IsNull()) {
        return true;
    }

    if (const bool* leftBool = left.TryBool()) {
        const bool* rightBool = right.TryBool();
        return rightBool && *leftBool == *rightBool;
    }

    if (const double* leftNumber = left.TryNumber()) {
        const double* rightNumber = right.TryNumber();
        return rightNumber && *leftNumber == *rightNumber;
    }

    if (const std::string* leftString = left.TryString()) {
        const std::string* rightString = right.TryString();
        return rightString && *leftString == *rightString;
    }

    if (const JsonArray* leftArray = left.TryArray()) {
        const JsonArray* rightArray = right.TryArray();
        if (!rightArray || leftArray->size() != rightArray->size()) {
            return false;
        }

        for (std::size_t index = 0; index < leftArray->size(); ++index) {
            if (!JsonEquals((*leftArray)[index], (*rightArray)[index])) {
                return false;
            }
        }
        return true;
    }

    if (const JsonObject* leftObject = left.TryObject()) {
        const JsonObject* rightObject = right.TryObject();
        if (!rightObject || leftObject->size() != rightObject->size()) {
            return false;
        }

        auto leftIt = leftObject->begin();
        auto rightIt = rightObject->begin();
        for (; leftIt != leftObject->end(); ++leftIt, ++rightIt) {
            if (leftIt->first != rightIt->first || !JsonEquals(leftIt->second, rightIt->second)) {
                return false;
            }
        }
        return true;
    }

    return false;
}

void WriteJson(const JsonValue& value, std::string& out, int indent) {
    if (const auto* nullValue = std::get_if<std::nullptr_t>(&value.storage)) {
        (void)nullValue;
        out += "null";
        return;
    }

    if (const bool* boolValue = value.TryBool()) {
        out += *boolValue ? "true" : "false";
        return;
    }

    if (const double* numberValue = value.TryNumber()) {
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.15g", *numberValue);
        out += buffer;
        return;
    }

    if (const std::string* stringValue = value.TryString()) {
        out.push_back('"');
        out += EscapeJsonString(*stringValue);
        out.push_back('"');
        return;
    }

    if (const JsonArray* array = value.TryArray()) {
        out += "[";
        if (!array->empty()) {
            out += "\n";
            for (std::size_t i = 0; i < array->size(); ++i) {
                out.append(static_cast<std::size_t>(indent + 2), ' ');
                WriteJson((*array)[i], out, indent + 2);
                if (i + 1 != array->size()) {
                    out += ",";
                }
                out += "\n";
            }
            out.append(static_cast<std::size_t>(indent), ' ');
        }
        out += "]";
        return;
    }

    if (const JsonObject* object = value.TryObject()) {
        out += "{";
        if (!object->empty()) {
            out += "\n";
            std::size_t index = 0;
            for (const auto& [key, child] : *object) {
                out.append(static_cast<std::size_t>(indent + 2), ' ');
                out.push_back('"');
                out += EscapeJsonString(key);
                out += "\": ";
                WriteJson(child, out, indent + 2);
                if (++index != object->size()) {
                    out += ",";
                }
                out += "\n";
            }
            out.append(static_cast<std::size_t>(indent), ' ');
        }
        out += "}";
    }
}

std::string JsonStringOr(const JsonObject* object, const char* key, std::string fallback) {
    if (!object || !key) {
        return fallback;
    }

    const auto it = object->find(key);
    if (it == object->end()) {
        return fallback;
    }

    const std::string* value = it->second.TryString();
    return value ? *value : fallback;
}

bool JsonBoolOr(const JsonObject* object, const char* key, bool fallback) {
    if (!object || !key) {
        return fallback;
    }

    const auto it = object->find(key);
    if (it == object->end()) {
        return fallback;
    }

    const bool* value = it->second.TryBool();
    return value ? *value : fallback;
}

const JsonArray* JsonArrayOrNull(const JsonObject* object, const char* key) {
    if (!object || !key) {
        return nullptr;
    }

    const auto it = object->find(key);
    if (it == object->end()) {
        return nullptr;
    }

    return it->second.TryArray();
}

const JsonObject* JsonObjectOrNull(const JsonObject* object, const char* key) {
    if (!object || !key) {
        return nullptr;
    }

    const auto it = object->find(key);
    if (it == object->end()) {
        return nullptr;
    }

    return it->second.TryObject();
}

} // namespace jsonutil
