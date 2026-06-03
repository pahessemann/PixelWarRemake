#pragma once

#include "pixelwar/http/HttpTypes.hpp"

#include <functional>
#include <string>
#include <unordered_map>

namespace pixelwar::http {

class Router {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    void add(std::string method, std::string path, Handler handler);
    [[nodiscard]] HttpResponse dispatch(const HttpRequest& request) const;

private:
    static std::string key(const std::string& method, const std::string& path);

    std::unordered_map<std::string, Handler> routes_;
};

} // namespace pixelwar::http

