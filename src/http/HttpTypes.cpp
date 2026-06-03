#include "pixelwar/http/HttpTypes.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <system_error>

namespace pixelwar::http {

namespace {

std::string reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 302: return "Found";
        case 303: return "See Other";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "HTTP";
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

void addDefaultHeaders(HttpResponse& response) {
    response.headers.emplace("Server", "PixelWarRemake");
    response.headers.emplace("Connection", "close");
    response.headers.emplace("Access-Control-Allow-Origin", "*");
    response.headers.emplace("Access-Control-Allow-Headers", "Content-Type, Authorization");
    response.headers.emplace("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response.headers.emplace("Cache-Control", "no-store");
}

} // namespace

std::optional<std::string> HttpRequest::header(const std::string& name) const {
    const auto it = headers.find(toLower(name));
    if (it == headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> HttpRequest::queryValue(const std::string& name) const {
    const auto it = query.find(name);
    if (it == query.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string HttpResponse::toBytes() const {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << reasonPhrase(status) << "\r\n";

    auto headersCopy = headers;
    headersCopy["Content-Length"] = std::to_string(body.size());

    for (const auto& [name, value] : headersCopy) {
        out << name << ": " << value << "\r\n";
    }

    out << "\r\n";
    out << body;
    return out.str();
}

HttpResponse HttpResponse::json(int status, std::string body) {
    HttpResponse response;
    response.status = status;
    response.body = std::move(body);
    response.headers["Content-Type"] = "application/json; charset=utf-8";
    addDefaultHeaders(response);
    return response;
}

HttpResponse HttpResponse::text(int status, std::string body) {
    HttpResponse response;
    response.status = status;
    response.body = std::move(body);
    response.headers["Content-Type"] = "text/plain; charset=utf-8";
    addDefaultHeaders(response);
    return response;
}

HttpResponse HttpResponse::empty(int status) {
    HttpResponse response;
    response.status = status;
    addDefaultHeaders(response);
    return response;
}

std::string urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
            continue;
        }
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hexValue(value[i + 1]);
            const int lo = hexValue(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

std::unordered_map<std::string, std::string> parseQueryString(const std::string& value) {
    std::unordered_map<std::string, std::string> query;
    std::size_t start = 0;

    while (start <= value.size()) {
        const std::size_t end = value.find('&', start);
        const std::string part = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            const std::size_t equal = part.find('=');
            if (equal == std::string::npos) {
                query[urlDecode(part)] = "";
            } else {
                query[urlDecode(part.substr(0, equal))] = urlDecode(part.substr(equal + 1));
            }
        }

        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return query;
}

std::string toLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace pixelwar::http
