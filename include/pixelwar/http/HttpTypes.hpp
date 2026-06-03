#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace pixelwar::http {

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::unordered_map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string remoteAddress;

    [[nodiscard]] std::optional<std::string> header(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> queryValue(const std::string& name) const;
};

struct HttpResponse {
    int status = 200;
    std::unordered_map<std::string, std::string> headers;
    std::string body;

    [[nodiscard]] std::string toBytes() const;

    static HttpResponse json(int status, std::string body);
    static HttpResponse text(int status, std::string body);
    static HttpResponse empty(int status);
};

std::string urlDecode(const std::string& value);
std::unordered_map<std::string, std::string> parseQueryString(const std::string& value);
std::string toLower(std::string value);

} // namespace pixelwar::http

