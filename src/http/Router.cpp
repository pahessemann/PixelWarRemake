#include "pixelwar/http/Router.hpp"

#include <utility>

namespace pixelwar::http {

void Router::add(std::string method, std::string path, Handler handler) {
    routes_[key(toLower(std::move(method)), std::move(path))] = std::move(handler);
}

HttpResponse Router::dispatch(const HttpRequest& request) const {
    if (request.method == "OPTIONS") {
        return HttpResponse::empty(204);
    }

    const auto it = routes_.find(key(toLower(request.method), request.path));
    if (it == routes_.end()) {
        return HttpResponse::json(404, R"({"error":"not_found"})");
    }

    try {
        return it->second(request);
    } catch (...) {
        return HttpResponse::json(500, R"({"error":"internal_error"})");
    }
}

std::string Router::key(const std::string& method, const std::string& path) {
    return method + " " + path;
}

} // namespace pixelwar::http

