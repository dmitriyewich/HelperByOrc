#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace waitif {

enum class ValueKind {
    Boolean,
    Number,
    String,
};

struct Value {
    ValueKind kind{ValueKind::Boolean};
    bool boolean{false};
    double number{0.0};
    std::string string{};

    static Value Boolean(bool value);
    static Value Number(double value);
    static Value String(std::string value);
};

enum class ResolveStatus {
    Resolved,
    Unavailable,
    Unknown,
    InvalidArguments,
};

struct ResolveResult {
    ResolveStatus status{ResolveStatus::Unavailable};
    Value value{};
    std::string error{};

    static ResolveResult Resolved(Value value);
    static ResolveResult Unavailable();
    static ResolveResult Unknown(std::string error);
    static ResolveResult InvalidArguments(std::string error);
};

class Resolver {
public:
    virtual ~Resolver() = default;

    virtual ResolveResult ResolveIdentifier(std::string_view normalizedName) = 0;
    virtual ResolveResult ResolveFunction(
        std::string_view normalizedName,
        std::span<const Value> arguments) = 0;
};

enum class EvaluationStatus {
    True,
    False,
    Unavailable,
    Error,
};

struct EvaluationResult {
    EvaluationStatus status{EvaluationStatus::Error};
    std::string error{};
};

class CompiledExpression {
public:
    static std::optional<CompiledExpression> Compile(std::string_view source, std::string& error);

    EvaluationResult Evaluate(Resolver& resolver) const;

private:
    struct Impl;

    explicit CompiledExpression(std::shared_ptr<const Impl> impl);

    std::shared_ptr<const Impl> impl_{};
};

} // namespace waitif
