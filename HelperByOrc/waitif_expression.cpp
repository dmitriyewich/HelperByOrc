#include "waitif_expression.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace waitif {
namespace {

constexpr std::size_t kMaxSourceBytes = 4096;
constexpr std::size_t kMaxNodes = 256;
constexpr std::size_t kMaxArguments = 2;
constexpr std::size_t kMaxDepth = 64;

std::string NormalizeAscii(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : static_cast<char>(ch);
    });
    return result;
}

enum class TokenKind {
    End,
    Identifier,
    Number,
    String,
    True,
    False,
    Not,
    And,
    Or,
    LeftParen,
    RightParen,
    Comma,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Plus,
    Minus,
};

struct Token {
    TokenKind kind{TokenKind::End};
    std::string text{};
    double number{0.0};
    std::size_t offset{0};
};

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    bool Tokenize(std::vector<Token>& output, std::string& error) {
        while (true) {
            SkipWhitespace();
            if (position_ >= source_.size()) {
                output.push_back(Token{TokenKind::End, {}, 0.0, position_});
                return true;
            }

            Token token;
            token.offset = position_;
            const char ch = source_[position_];
            if (IsIdentifierStart(ch)) {
                ReadIdentifier(token);
            } else if (IsNumberStart()) {
                if (!ReadNumber(token, error)) {
                    return false;
                }
            } else if (ch == '\'' || ch == '"') {
                if (!ReadString(token, error)) {
                    return false;
                }
            } else if (!ReadOperator(token, error)) {
                return false;
            }
            output.push_back(std::move(token));
        }
    }

private:
    static bool IsIdentifierStart(char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
    }

    static bool IsIdentifierPart(char ch) {
        return IsIdentifierStart(ch) || (ch >= '0' && ch <= '9');
    }

    void SkipWhitespace() {
        while (position_ < source_.size()) {
            const char ch = source_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
                break;
            }
            ++position_;
        }
    }

    bool IsNumberStart() const {
        const char ch = source_[position_];
        if (ch >= '0' && ch <= '9') {
            return true;
        }
        return ch == '.' && position_ + 1 < source_.size()
            && source_[position_ + 1] >= '0' && source_[position_ + 1] <= '9';
    }

    void ReadIdentifier(Token& token) {
        const std::size_t begin = position_++;
        while (position_ < source_.size() && IsIdentifierPart(source_[position_])) {
            ++position_;
        }

        token.text = NormalizeAscii(source_.substr(begin, position_ - begin));
        if (token.text == "true") {
            token.kind = TokenKind::True;
        } else if (token.text == "false") {
            token.kind = TokenKind::False;
        } else if (token.text == "not") {
            token.kind = TokenKind::Not;
        } else if (token.text == "and") {
            token.kind = TokenKind::And;
        } else if (token.text == "or") {
            token.kind = TokenKind::Or;
        } else {
            token.kind = TokenKind::Identifier;
        }
    }

    bool ReadNumber(Token& token, std::string& error) {
        const std::size_t begin = position_;
        if (source_[position_] == '0' && position_ + 1 < source_.size()
            && (source_[position_ + 1] == 'x' || source_[position_ + 1] == 'X')) {
            position_ += 2;
            const std::size_t digitsBegin = position_;
            while (position_ < source_.size()) {
                const char ch = source_[position_];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
                    break;
                }
                ++position_;
            }
            if (digitsBegin == position_) {
                error = FormatError(begin, "expected hexadecimal digits");
                return false;
            }

            unsigned long long parsed = 0;
            const auto result = std::from_chars(
                source_.data() + digitsBegin,
                source_.data() + position_,
                parsed,
                16);
            if (result.ec != std::errc{} || parsed > (1ULL << 53)) {
                error = FormatError(begin, "hexadecimal number is out of range");
                return false;
            }
            token.kind = TokenKind::Number;
            token.number = static_cast<double>(parsed);
            token.text.assign(source_.substr(begin, position_ - begin));
            return true;
        }

        bool hasDigits = false;
        while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
            hasDigits = true;
            ++position_;
        }
        if (position_ < source_.size() && source_[position_] == '.') {
            ++position_;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
                hasDigits = true;
                ++position_;
            }
        }
        if (!hasDigits) {
            error = FormatError(begin, "invalid number");
            return false;
        }
        if (position_ < source_.size() && (source_[position_] == 'e' || source_[position_] == 'E')) {
            const std::size_t exponentOffset = position_++;
            if (position_ < source_.size() && (source_[position_] == '+' || source_[position_] == '-')) {
                ++position_;
            }
            const std::size_t exponentDigits = position_;
            while (position_ < source_.size() && source_[position_] >= '0' && source_[position_] <= '9') {
                ++position_;
            }
            if (exponentDigits == position_) {
                error = FormatError(exponentOffset, "invalid exponent");
                return false;
            }
        }

        const auto result = std::from_chars(
            source_.data() + begin,
            source_.data() + position_,
            token.number,
            std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != source_.data() + position_ || !std::isfinite(token.number)) {
            error = FormatError(begin, "number is out of range");
            return false;
        }
        token.text.assign(source_.substr(begin, position_ - begin));
        token.kind = TokenKind::Number;
        return true;
    }

    bool ReadString(Token& token, std::string& error) {
        const char quote = source_[position_++];
        token.kind = TokenKind::String;
        while (position_ < source_.size()) {
            const char ch = source_[position_++];
            if (ch == quote) {
                return true;
            }
            if (ch != '\\') {
                token.text.push_back(ch);
                continue;
            }
            if (position_ >= source_.size()) {
                break;
            }
            const char escaped = source_[position_++];
            switch (escaped) {
            case '\\':
            case '\'':
            case '"':
                token.text.push_back(escaped);
                break;
            case 'n':
                token.text.push_back('\n');
                break;
            case 'r':
                token.text.push_back('\r');
                break;
            case 't':
                token.text.push_back('\t');
                break;
            default:
                error = FormatError(position_ - 2, "unsupported string escape");
                return false;
            }
        }
        error = FormatError(token.offset, "unterminated string");
        return false;
    }

    bool ReadOperator(Token& token, std::string& error) {
        const char ch = source_[position_++];
        const auto consume = [this](char expected) {
            if (position_ < source_.size() && source_[position_] == expected) {
                ++position_;
                return true;
            }
            return false;
        };

        switch (ch) {
        case '(':
            token.kind = TokenKind::LeftParen;
            return true;
        case ')':
            token.kind = TokenKind::RightParen;
            return true;
        case ',':
            token.kind = TokenKind::Comma;
            return true;
        case '+':
            token.kind = TokenKind::Plus;
            return true;
        case '-':
            token.kind = TokenKind::Minus;
            return true;
        case '!':
            token.kind = consume('=') ? TokenKind::NotEqual : TokenKind::Not;
            return true;
        case '~':
            if (consume('=')) {
                token.kind = TokenKind::NotEqual;
                return true;
            }
            break;
        case '=':
            consume('=');
            token.kind = TokenKind::Equal;
            return true;
        case '<':
            token.kind = consume('=') ? TokenKind::LessEqual : TokenKind::Less;
            return true;
        case '>':
            token.kind = consume('=') ? TokenKind::GreaterEqual : TokenKind::Greater;
            return true;
        case '&':
            if (consume('&')) {
                token.kind = TokenKind::And;
                return true;
            }
            break;
        case '|':
            if (consume('|')) {
                token.kind = TokenKind::Or;
                return true;
            }
            break;
        default:
            break;
        }

        error = FormatError(token.offset, "unexpected character");
        return false;
    }

    static std::string FormatError(std::size_t offset, std::string_view message) {
        return "at byte " + std::to_string(offset) + ": " + std::string(message);
    }

    std::string_view source_;
    std::size_t position_{0};
};

enum class NodeKind {
    Literal,
    Identifier,
    Function,
    Not,
    Negate,
    And,
    Or,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

struct Node {
    NodeKind kind{NodeKind::Literal};
    Value literal{};
    std::string name{};
    std::vector<std::size_t> arguments{};
    std::size_t left{0};
    std::size_t right{0};
};

class Parser {
public:
    explicit Parser(const std::vector<Token>& tokens) : tokens_(tokens) {}

    bool Parse(std::vector<Node>& nodes, std::size_t& root, std::string& error) {
        nodes_ = &nodes;
        error_ = &error;
        if (!ParseOr(root, 0)) {
            return false;
        }
        if (Current().kind != TokenKind::End) {
            return Fail(Current(), "unexpected token after expression");
        }
        return true;
    }

private:
    bool ParseOr(std::size_t& output, std::size_t depth) {
        if (!ParseAnd(output, depth + 1)) {
            return false;
        }
        while (Match(TokenKind::Or)) {
            std::size_t right = 0;
            if (!ParseAnd(right, depth + 1) || !AddBinary(NodeKind::Or, output, right, output)) {
                return false;
            }
        }
        return true;
    }

    bool ParseAnd(std::size_t& output, std::size_t depth) {
        if (!ParseComparison(output, depth + 1)) {
            return false;
        }
        while (Match(TokenKind::And)) {
            std::size_t right = 0;
            if (!ParseComparison(right, depth + 1) || !AddBinary(NodeKind::And, output, right, output)) {
                return false;
            }
        }
        return true;
    }

    bool ParseComparison(std::size_t& output, std::size_t depth) {
        if (!ParseUnary(output, depth + 1)) {
            return false;
        }

        NodeKind kind{};
        bool comparison = true;
        switch (Current().kind) {
        case TokenKind::Equal:
            kind = NodeKind::Equal;
            break;
        case TokenKind::NotEqual:
            kind = NodeKind::NotEqual;
            break;
        case TokenKind::Less:
            kind = NodeKind::Less;
            break;
        case TokenKind::LessEqual:
            kind = NodeKind::LessEqual;
            break;
        case TokenKind::Greater:
            kind = NodeKind::Greater;
            break;
        case TokenKind::GreaterEqual:
            kind = NodeKind::GreaterEqual;
            break;
        default:
            comparison = false;
            break;
        }
        if (!comparison) {
            return true;
        }

        ++position_;
        std::size_t right = 0;
        if (!ParseUnary(right, depth + 1)) {
            return false;
        }
        return AddBinary(kind, output, right, output);
    }

    bool ParseUnary(std::size_t& output, std::size_t depth) {
        if (depth > kMaxDepth) {
            return Fail(Current(), "expression nesting is too deep");
        }
        if (Match(TokenKind::Not)) {
            std::size_t operand = 0;
            return ParseUnary(operand, depth + 1) && AddUnary(NodeKind::Not, operand, output);
        }
        if (Match(TokenKind::Minus)) {
            std::size_t operand = 0;
            return ParseUnary(operand, depth + 1) && AddUnary(NodeKind::Negate, operand, output);
        }
        if (Match(TokenKind::Plus)) {
            return ParseUnary(output, depth + 1);
        }
        return ParsePrimary(output, depth + 1);
    }

    bool ParsePrimary(std::size_t& output, std::size_t depth) {
        if (depth > kMaxDepth) {
            return Fail(Current(), "expression nesting is too deep");
        }

        const Token token = Current();
        if (Match(TokenKind::True)) {
            return AddLiteral(Value::Boolean(true), output);
        }
        if (Match(TokenKind::False)) {
            return AddLiteral(Value::Boolean(false), output);
        }
        if (Match(TokenKind::Number)) {
            return AddLiteral(Value::Number(token.number), output);
        }
        if (Match(TokenKind::String)) {
            return AddLiteral(Value::String(token.text), output);
        }
        if (Match(TokenKind::LeftParen)) {
            if (!ParseOr(output, depth + 1)) {
                return false;
            }
            if (!Match(TokenKind::RightParen)) {
                return Fail(Current(), "expected ')'");
            }
            return true;
        }
        if (!Match(TokenKind::Identifier)) {
            return Fail(Current(), "expected a value, identifier or function");
        }

        if (!Match(TokenKind::LeftParen)) {
            Node node;
            node.kind = NodeKind::Identifier;
            node.name = token.text;
            return AddNode(std::move(node), output);
        }

        Node node;
        node.kind = NodeKind::Function;
        node.name = token.text;
        if (!Match(TokenKind::RightParen)) {
            while (true) {
                if (node.arguments.size() >= kMaxArguments) {
                    return Fail(Current(), "too many function arguments");
                }
                std::size_t argument = 0;
                if (!ParseOr(argument, depth + 1)) {
                    return false;
                }
                node.arguments.push_back(argument);
                if (Match(TokenKind::RightParen)) {
                    break;
                }
                if (!Match(TokenKind::Comma)) {
                    return Fail(Current(), "expected ',' or ')'");
                }
            }
        }
        return AddNode(std::move(node), output);
    }

    bool AddLiteral(Value value, std::size_t& output) {
        Node node;
        node.kind = NodeKind::Literal;
        node.literal = std::move(value);
        return AddNode(std::move(node), output);
    }

    bool AddUnary(NodeKind kind, std::size_t operand, std::size_t& output) {
        Node node;
        node.kind = kind;
        node.left = operand;
        return AddNode(std::move(node), output);
    }

    bool AddBinary(NodeKind kind, std::size_t left, std::size_t right, std::size_t& output) {
        Node node;
        node.kind = kind;
        node.left = left;
        node.right = right;
        return AddNode(std::move(node), output);
    }

    bool AddNode(Node node, std::size_t& output) {
        if (nodes_->size() >= kMaxNodes) {
            return Fail(Current(), "expression is too complex");
        }
        output = nodes_->size();
        nodes_->push_back(std::move(node));
        return true;
    }

    bool Match(TokenKind kind) {
        if (Current().kind != kind) {
            return false;
        }
        ++position_;
        return true;
    }

    const Token& Current() const {
        return tokens_[std::min(position_, tokens_.size() - 1)];
    }

    bool Fail(const Token& token, std::string_view message) {
        *error_ = "at byte " + std::to_string(token.offset) + ": " + std::string(message);
        return false;
    }

    const std::vector<Token>& tokens_;
    std::vector<Node>* nodes_{nullptr};
    std::string* error_{nullptr};
    std::size_t position_{0};
};

struct EvaluatedValue {
    ResolveStatus status{ResolveStatus::Unavailable};
    Value value{};
    std::string error{};
};

bool IsTruthy(const Value& value) {
    switch (value.kind) {
    case ValueKind::Boolean:
        return value.boolean;
    case ValueKind::Number:
        return value.number != 0.0;
    case ValueKind::String:
        return !value.string.empty();
    }
    return false;
}

EvaluatedValue FromResolveResult(ResolveResult result) {
    return EvaluatedValue{result.status, std::move(result.value), std::move(result.error)};
}

EvaluatedValue Resolved(Value value) {
    return EvaluatedValue{ResolveStatus::Resolved, std::move(value), {}};
}

EvaluatedValue EvaluationError(std::string error) {
    return EvaluatedValue{ResolveStatus::InvalidArguments, {}, std::move(error)};
}

bool ValuesEqual(const Value& left, const Value& right) {
    if (left.kind != right.kind) {
        return false;
    }
    switch (left.kind) {
    case ValueKind::Boolean:
        return left.boolean == right.boolean;
    case ValueKind::Number:
        return left.number == right.number;
    case ValueKind::String:
        return left.string == right.string;
    }
    return false;
}

} // namespace

struct CompiledExpression::Impl {
    std::vector<Node> nodes{};
    std::size_t root{0};

    EvaluatedValue EvaluateNode(std::size_t index, Resolver& resolver, std::size_t depth) const {
        if (depth > kMaxNodes || index >= nodes.size()) {
            return EvaluationError("compiled expression is invalid");
        }
        const Node& node = nodes[index];
        switch (node.kind) {
        case NodeKind::Literal:
            return Resolved(node.literal);
        case NodeKind::Identifier:
            return FromResolveResult(resolver.ResolveIdentifier(node.name));
        case NodeKind::Function: {
            std::array<Value, kMaxArguments> argumentStorage{};
            std::size_t argumentCount = 0;
            for (const std::size_t argumentIndex : node.arguments) {
                EvaluatedValue argument = EvaluateNode(argumentIndex, resolver, depth + 1);
                if (argument.status != ResolveStatus::Resolved) {
                    return argument;
                }
                argumentStorage[argumentCount++] = std::move(argument.value);
            }
            return FromResolveResult(
                resolver.ResolveFunction(node.name, std::span(argumentStorage.data(), argumentCount)));
        }
        case NodeKind::Not: {
            EvaluatedValue operand = EvaluateNode(node.left, resolver, depth + 1);
            if (operand.status != ResolveStatus::Resolved) {
                return operand;
            }
            return Resolved(Value::Boolean(!IsTruthy(operand.value)));
        }
        case NodeKind::Negate: {
            EvaluatedValue operand = EvaluateNode(node.left, resolver, depth + 1);
            if (operand.status != ResolveStatus::Resolved) {
                return operand;
            }
            if (operand.value.kind != ValueKind::Number) {
                return EvaluationError("unary '-' requires a number");
            }
            return Resolved(Value::Number(-operand.value.number));
        }
        case NodeKind::And:
        case NodeKind::Or: {
            EvaluatedValue left = EvaluateNode(node.left, resolver, depth + 1);
            if (left.status != ResolveStatus::Resolved && left.status != ResolveStatus::Unavailable) {
                return left;
            }

            const bool isAnd = node.kind == NodeKind::And;
            if (left.status == ResolveStatus::Resolved) {
                const bool leftTruthy = IsTruthy(left.value);
                if ((isAnd && !leftTruthy) || (!isAnd && leftTruthy)) {
                    return Resolved(Value::Boolean(leftTruthy));
                }
            }

            EvaluatedValue right = EvaluateNode(node.right, resolver, depth + 1);
            if (right.status != ResolveStatus::Resolved) {
                return left.status == ResolveStatus::Unavailable && right.status == ResolveStatus::Unavailable
                    ? left
                    : right;
            }
            const bool rightTruthy = IsTruthy(right.value);
            if (left.status == ResolveStatus::Unavailable) {
                if ((isAnd && !rightTruthy) || (!isAnd && rightTruthy)) {
                    return Resolved(Value::Boolean(rightTruthy));
                }
                return left;
            }
            return Resolved(Value::Boolean(rightTruthy));
        }
        case NodeKind::Equal:
        case NodeKind::NotEqual:
        case NodeKind::Less:
        case NodeKind::LessEqual:
        case NodeKind::Greater:
        case NodeKind::GreaterEqual: {
            EvaluatedValue left = EvaluateNode(node.left, resolver, depth + 1);
            if (left.status != ResolveStatus::Resolved) {
                return left;
            }
            EvaluatedValue right = EvaluateNode(node.right, resolver, depth + 1);
            if (right.status != ResolveStatus::Resolved) {
                return right;
            }

            if (node.kind == NodeKind::Equal || node.kind == NodeKind::NotEqual) {
                const bool equal = ValuesEqual(left.value, right.value);
                return Resolved(Value::Boolean(node.kind == NodeKind::Equal ? equal : !equal));
            }
            if (left.value.kind != ValueKind::Number || right.value.kind != ValueKind::Number) {
                return EvaluationError("ordered comparison requires two numbers");
            }
            bool result = false;
            switch (node.kind) {
            case NodeKind::Less:
                result = left.value.number < right.value.number;
                break;
            case NodeKind::LessEqual:
                result = left.value.number <= right.value.number;
                break;
            case NodeKind::Greater:
                result = left.value.number > right.value.number;
                break;
            case NodeKind::GreaterEqual:
                result = left.value.number >= right.value.number;
                break;
            default:
                break;
            }
            return Resolved(Value::Boolean(result));
        }
        }
        return EvaluationError("unsupported expression node");
    }
};

Value Value::Boolean(bool value) {
    Value result;
    result.kind = ValueKind::Boolean;
    result.boolean = value;
    return result;
}

Value Value::Number(double value) {
    Value result;
    result.kind = ValueKind::Number;
    result.number = value;
    return result;
}

Value Value::String(std::string value) {
    Value result;
    result.kind = ValueKind::String;
    result.string = std::move(value);
    return result;
}

ResolveResult ResolveResult::Resolved(Value value) {
    ResolveResult result;
    result.status = ResolveStatus::Resolved;
    result.value = std::move(value);
    return result;
}

ResolveResult ResolveResult::Unavailable() {
    ResolveResult result;
    result.status = ResolveStatus::Unavailable;
    return result;
}

ResolveResult ResolveResult::Unknown(std::string error) {
    ResolveResult result;
    result.status = ResolveStatus::Unknown;
    result.error = std::move(error);
    return result;
}

ResolveResult ResolveResult::InvalidArguments(std::string error) {
    ResolveResult result;
    result.status = ResolveStatus::InvalidArguments;
    result.error = std::move(error);
    return result;
}

CompiledExpression::CompiledExpression(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}

std::optional<CompiledExpression> CompiledExpression::Compile(std::string_view source, std::string& error) {
    error.clear();
    if (source.empty()) {
        error = "expression is empty";
        return std::nullopt;
    }
    if (source.size() > kMaxSourceBytes) {
        error = "expression exceeds 4096 bytes";
        return std::nullopt;
    }

    std::vector<Token> tokens;
    tokens.reserve(std::min<std::size_t>(source.size() / 2 + 2, kMaxNodes * 2));
    Lexer lexer(source);
    if (!lexer.Tokenize(tokens, error)) {
        return std::nullopt;
    }

    auto impl = std::make_shared<Impl>();
    impl->nodes.reserve(std::min<std::size_t>(tokens.size(), kMaxNodes));
    Parser parser(tokens);
    if (!parser.Parse(impl->nodes, impl->root, error)) {
        return std::nullopt;
    }
    return CompiledExpression(std::move(impl));
}

EvaluationResult CompiledExpression::Evaluate(Resolver& resolver) const {
    if (!impl_ || impl_->nodes.empty()) {
        return EvaluationResult{EvaluationStatus::Error, "expression is not compiled"};
    }

    EvaluatedValue result = impl_->EvaluateNode(impl_->root, resolver, 0);
    switch (result.status) {
    case ResolveStatus::Resolved:
        return EvaluationResult{
            IsTruthy(result.value) ? EvaluationStatus::True : EvaluationStatus::False,
            {}};
    case ResolveStatus::Unavailable:
        return EvaluationResult{EvaluationStatus::Unavailable, {}};
    case ResolveStatus::Unknown:
    case ResolveStatus::InvalidArguments:
        return EvaluationResult{
            EvaluationStatus::Error,
            result.error.empty() ? "expression resolver failed" : std::move(result.error)};
    }
    return EvaluationResult{EvaluationStatus::Error, "expression resolver returned an invalid status"};
}

} // namespace waitif
