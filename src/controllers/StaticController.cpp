#include "pixelwar/controllers/StaticController.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace pixelwar::controllers {

namespace {

using pixelwar::http::HttpRequest;
using pixelwar::http::HttpResponse;

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string contentTypeFor(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".js") {
        return "application/javascript; charset=utf-8";
    }
    if (extension == ".svg") {
        return "image/svg+xml; charset=utf-8";
    }
    if (extension == ".webmanifest") {
        return "application/manifest+json; charset=utf-8";
    }
    return "text/plain; charset=utf-8";
}

HttpResponse serveFile(const std::filesystem::path& publicDir, const std::filesystem::path& relativePath) {
    const auto fullPath = publicDir / relativePath;
    const auto body = readTextFile(fullPath);
    if (body.empty()) {
        return HttpResponse::json(404, R"({"error":"static_file_not_found"})");
    }

    auto response = HttpResponse::text(200, body);
    response.headers["Content-Type"] = contentTypeFor(fullPath);
    response.headers["Cache-Control"] = "no-store";
    return response;
}

} // namespace

void registerStaticRoutes(http::Router& router, std::filesystem::path publicDir) {
    const std::unordered_map<std::string, std::filesystem::path> files = {
        {"/", "index.html"},
        {"/index.html", "index.html"},
        {"/gestion", "admin.html"},
        {"/gestion/", "admin.html"},
        {"/admin.html", "admin.html"},
        {"/admin.js", "admin.js"},
        {"/styles.css", "styles.css"},
        {"/app.js", "app.js"},
        {"/favicon.svg", "favicon.svg"},
        {"/site.webmanifest", "site.webmanifest"}
    };

    for (const auto& [route, file] : files) {
        router.add("GET", route, [publicDir, file](const HttpRequest&) {
            return serveFile(publicDir, file);
        });
    }
}

} // namespace pixelwar::controllers
