#include "pixelwar/utils/Json.hpp"

#include <cctype>
#include <charconv>
#include <system_error>

namespace pixelwar::utils::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& input) : input_(input) {}

    std::optional<Object> parseObject() {
        skipWhitespace();
        if (!consume('{')) {
            return std::nullopt;
        }

        Object object;
        skipWhitespace();
        if (consume('}')) {
            return object;
        }

        while (pos_ < input_.size()) {
            auto key = parseString();
            if (!key) {
                return std::nullopt;
            }

            skipWhitespace();
            if (!consume(':')) {
                return std::nullopt;
            }

            auto value = parseValue();
            if (!value) {
                return std::nullopt;
            }
            object.emplace(std::move(*key), std::move(*value));

            skipWhitespace();
            if (consume('}')) {
                skipWhitespace();
                return pos_ == input_.size() ? std::optional<Object>(std::move(object)) : std::nullopt;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

private:
    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (pos_ < input_.size() && input_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<std::string> parseString() {
        skipWhitespace();
        if (pos_ >= input_.size() || input_[pos_] != '"') {
            return std::nullopt;
        }
        ++pos_;

        std::string out;
        while (pos_ < input_.size()) {
            char c = input_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }

            if (pos_ >= input_.size()) {
                return std::nullopt;
            }
            const char escaped = input_[pos_++];
            switch (escaped) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default:
                    return std::nullopt;
            }
        }

        return std::nullopt;
    }

    std::optional<Value> parseValue() {
        skipWhitespace();
        if (pos_ >= input_.size()) {
            return std::nullopt;
        }

        if (input_[pos_] == '"') {
            auto str = parseString();
            if (!str) {
                return std::nullopt;
            }
            return Value{Value::Type::String, *str};
        }

        if (input_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value{Value::Type::Boolean, "true"};
        }
        if (input_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value{Value::Type::Boolean, "false"};
        }
        if (input_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value{Value::Type::Null, ""};
        }

        const std::size_t start = pos_;
        if (input_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
        if (start != pos_) {
            return Value{Value::Type::Number, input_.substr(start, pos_ - start)};
        }

        return std::nullopt;
    }

    const std::string& input_;
    std::size_t pos_ = 0;
};

} // namespace

std::optional<Object> parseObject(const std::string& input) {
    return Parser(input).parseObject();
}

std::optional<std::string> getString(const Object& object, const std::string& key) {
    const auto it = object.find(key);
    if (it == object.end() || it->second.type != Value::Type::String) {
        return std::nullopt;
    }
    return it->second.text;
}

std::optional<std::int64_t> getInt(const Object& object, const std::string& key) {
    const auto it = object.find(key);
    if (it == object.end() || it->second.type != Value::Type::Number) {
        return std::nullopt;
    }

    std::int64_t value = 0;
    const auto* begin = it->second.text.data();
    const auto* end = begin + it->second.text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> getBool(const Object& object, const std::string& key) {
    const auto it = object.find(key);
    if (it == object.end() || it->second.type != Value::Type::Boolean) {
        return std::nullopt;
    }
    return it->second.text == "true";
}

std::string escape(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                const auto byte = static_cast<unsigned char>(c);
                if (byte < 0x20) {
                    out += "\\u00";
                    constexpr char hex[] = "0123456789abcdef";
                    out.push_back(hex[(byte >> 4) & 0xF]);
                    out.push_back(hex[byte & 0xF]);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

} // namespace pixelwar::utils::json
