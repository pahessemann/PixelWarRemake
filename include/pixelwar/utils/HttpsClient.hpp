#pragma once

#include <string>
#include <unordered_map>

namespace pixelwar::utils {

struct HttpsResponse {
    int status = 0;
    std::string body;
    std::string error;
};

HttpsResponse httpsRequest(
    const std::string& method,
    const std::string& host,
    const std::string& path,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
);

} // namespace pixelwar::utils
