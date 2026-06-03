#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace pixelwar::utils::json {

struct Value {
    enum class Type {
        String,
        Number,
        Boolean,
        Null
    };

    Type type = Type::Null;
    std::string text;
};

using Object = std::unordered_map<std::string, Value>;

std::optional<Object> parseObject(const std::string& input);
std::optional<std::string> getString(const Object& object, const std::string& key);
std::optional<std::int64_t> getInt(const Object& object, const std::string& key);
std::optional<bool> getBool(const Object& object, const std::string& key);
std::string escape(const std::string& input);

} // namespace pixelwar::utils::json

