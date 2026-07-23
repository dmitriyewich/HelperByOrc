#pragma once

#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace jsonutil {

struct JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Storage storage = nullptr;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : storage(nullptr) {
    }
    JsonValue(bool value) : storage(value) {
    }
    JsonValue(double value) : storage(value) {
    }
    JsonValue(int value) : storage(static_cast<double>(value)) {
    }
    JsonValue(std::string value) : storage(std::move(value)) {
    }
    JsonValue(const char* value) : storage(std::string(value ? value : "")) {
    }
    JsonValue(JsonArray value) : storage(std::move(value)) {
    }
    JsonValue(JsonObject value) : storage(std::move(value)) {
    }

    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(storage); }
    bool IsBool() const { return std::holds_alternative<bool>(storage); }
    bool IsNumber() const { return std::holds_alternative<double>(storage); }
    bool IsString() const { return std::holds_alternative<std::string>(storage); }
    bool IsArray() const { return std::holds_alternative<JsonArray>(storage); }
    bool IsObject() const { return std::holds_alternative<JsonObject>(storage); }

    const JsonArray* TryArray() const { return std::get_if<JsonArray>(&storage); }
    const JsonObject* TryObject() const { return std::get_if<JsonObject>(&storage); }
    const std::string* TryString() const { return std::get_if<std::string>(&storage); }
    const bool* TryBool() const { return std::get_if<bool>(&storage); }
    const double* TryNumber() const { return std::get_if<double>(&storage); }
};

std::optional<JsonValue> ParseJson(std::string_view source, std::string& error);
bool JsonEquals(const JsonValue& left, const JsonValue& right);
void WriteJson(const JsonValue& value, std::string& out, int indent = 0);

template <typename T>
T JsonNumberOr(const JsonObject* object, const char* key, T fallback) {
    if (!object || !key) {
        return fallback;
    }

    const auto it = object->find(key);
    if (it == object->end()) {
        return fallback;
    }

    const double* number = it->second.TryNumber();
    if (!number) {
        return fallback;
    }

    if (!std::isfinite(*number)) {
        return fallback;
    }
    if constexpr (std::is_integral_v<T>) {
        const long double candidate = static_cast<long double>(*number);
        if (candidate < static_cast<long double>(std::numeric_limits<T>::lowest())
            || candidate > static_cast<long double>(std::numeric_limits<T>::max())) {
            return fallback;
        }
        if constexpr (std::numeric_limits<T>::digits > std::numeric_limits<double>::digits) {
            if (*number >= static_cast<double>(std::numeric_limits<T>::max())) {
                return fallback;
            }
        }
    } else if constexpr (std::is_floating_point_v<T> && sizeof(T) < sizeof(double)) {
        if (*number < static_cast<double>(std::numeric_limits<T>::lowest())
            || *number > static_cast<double>(std::numeric_limits<T>::max())) {
            return fallback;
        }
    }

    return static_cast<T>(*number);
}

std::string JsonStringOr(const JsonObject* object, const char* key, std::string fallback = {});
bool JsonBoolOr(const JsonObject* object, const char* key, bool fallback);
const JsonArray* JsonArrayOrNull(const JsonObject* object, const char* key);
const JsonObject* JsonObjectOrNull(const JsonObject* object, const char* key);

} // namespace jsonutil
