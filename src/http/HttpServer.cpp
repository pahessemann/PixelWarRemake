#include "pixelwar/http/HttpServer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pixelwar::http {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

NativeSocket toNative(std::intptr_t value) {
    return static_cast<NativeSocket>(value);
}

std::intptr_t fromNative(NativeSocket value) {
    return static_cast<std::intptr_t>(value);
}

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        ++start;
    }
    return value.substr(start);
}

std::size_t parseContentLength(const HttpRequest& request) {
    const auto header = request.header("content-length");
    if (!header) {
        return 0;
    }

    std::size_t length = 0;
    const auto* begin = header->data();
    const auto* end = begin + header->size();
    const auto result = std::from_chars(begin, end, length);
    if (result.ec != std::errc{} || result.ptr != end) {
        return 0;
    }
    return length;
}

std::optional<std::size_t> parseContentLengthFromHeaderBlock(const std::string& headerBlock) {
    std::istringstream stream(headerBlock);
    std::string line;
    if (!std::getline(stream, line)) {
        return std::nullopt;
    }

    while (std::getline(stream, line)) {
        line = trim(line);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        if (toLower(line.substr(0, colon)) != "content-length") {
            continue;
        }

        const std::string valueText = trim(line.substr(colon + 1));
        std::size_t value = 0;
        const auto* begin = valueText.data();
        const auto* end = begin + valueText.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) {
            return std::nullopt;
        }
        return value;
    }

    return 0;
}

} // namespace

class HttpServer::SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }

    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

HttpServer::HttpServer(std::string host, std::uint16_t port, std::size_t workerCount, std::size_t maxBodyBytes, Router& router)
    : host_(std::move(host)),
      port_(port),
      maxBodyBytes_(maxBodyBytes),
      router_(router),
      workers_(workerCount),
      socketRuntime_(std::make_unique<SocketRuntime>()) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::run() {
    running_ = true;

    NativeSocket listenSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == kInvalidSocket) {
        throw std::runtime_error("socket creation failed");
    }
    listenSocket_ = fromNative(listenSocket);

    int reuse = 1;
#ifdef _WIN32
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (host_ == "0.0.0.0" || host_.empty()) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
        closeSocket(listenSocket_);
        throw std::runtime_error("invalid IPv4 host: " + host_);
    }

    if (::bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        closeSocket(listenSocket_);
        throw std::runtime_error("bind failed");
    }

    if (::listen(listenSocket, SOMAXCONN) != 0) {
        closeSocket(listenSocket_);
        throw std::runtime_error("listen failed");
    }

    std::cout << "PixelWarRemake listening on http://" << host_ << ':' << port_ << '\n';

    while (running_) {
        sockaddr_in remote{};
#ifdef _WIN32
        int remoteLen = sizeof(remote);
#else
        socklen_t remoteLen = sizeof(remote);
#endif
        NativeSocket client = ::accept(listenSocket, reinterpret_cast<sockaddr*>(&remote), &remoteLen);
        if (client == kInvalidSocket) {
            if (running_) {
                continue;
            }
            break;
        }

        std::array<char, INET_ADDRSTRLEN> remoteBuffer{};
#ifdef _WIN32
        const char* remoteAddress = ::inet_ntop(AF_INET, &remote.sin_addr, remoteBuffer.data(), remoteBuffer.size());
#else
        const char* remoteAddress = ::inet_ntop(AF_INET, &remote.sin_addr, remoteBuffer.data(), static_cast<socklen_t>(remoteBuffer.size()));
#endif
        std::string remoteString = remoteAddress != nullptr ? remoteAddress : "unknown";

        try {
            workers_.enqueue([this, clientHandle = fromNative(client), remoteString] {
                handleClient(clientHandle, remoteString);
            });
        } catch (...) {
            closeSocket(fromNative(client));
        }
    }
}

void HttpServer::stop() {
    running_ = false;
    if (listenSocket_ != -1) {
        closeSocket(listenSocket_);
        listenSocket_ = -1;
    }
}

void HttpServer::handleClient(std::intptr_t clientSocket, std::string remoteAddress) {
    std::string raw;
    raw.reserve(4096);
    std::array<char, 4096> buffer{};
    std::size_t expectedBody = 0;
    bool headersParsed = false;

    while (raw.size() <= maxBodyBytes_ + 16384) {
#ifdef _WIN32
        const int received = ::recv(toNative(clientSocket), buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const ssize_t received = ::recv(toNative(clientSocket), buffer.data(), buffer.size(), 0);
#endif
        if (received <= 0) {
            closeSocket(clientSocket);
            return;
        }

        raw.append(buffer.data(), static_cast<std::size_t>(received));
        const std::size_t headerEnd = raw.find("\r\n\r\n");
        if (!headersParsed && headerEnd != std::string::npos) {
            const auto contentLength = parseContentLengthFromHeaderBlock(raw.substr(0, headerEnd));
            if (!contentLength) {
                sendAll(clientSocket, HttpResponse::json(400, R"({"error":"bad_request"})").toBytes());
                closeSocket(clientSocket);
                return;
            }
            expectedBody = *contentLength;
            if (expectedBody > maxBodyBytes_) {
                sendAll(clientSocket, HttpResponse::json(413, R"({"error":"payload_too_large"})").toBytes());
                closeSocket(clientSocket);
                return;
            }
            headersParsed = true;
        }

        if (headersParsed) {
            const std::size_t bodyStart = raw.find("\r\n\r\n") + 4;
            if (raw.size() >= bodyStart + expectedBody) {
                break;
            }
        }
    }

    HttpRequest request;
    if (!parseRequest(raw, request)) {
        sendAll(clientSocket, HttpResponse::json(400, R"({"error":"bad_request"})").toBytes());
        closeSocket(clientSocket);
        return;
    }
    request.remoteAddress = std::move(remoteAddress);

    const HttpResponse response = router_.dispatch(request);
    sendAll(clientSocket, response.toBytes());
    closeSocket(clientSocket);
}

bool HttpServer::parseRequest(const std::string& raw, HttpRequest& out) const {
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return false;
    }

    std::istringstream stream(raw.substr(0, headerEnd));
    std::string requestLine;
    if (!std::getline(stream, requestLine)) {
        return false;
    }
    requestLine = trim(requestLine);

    std::istringstream requestLineStream(requestLine);
    std::string version;
    if (!(requestLineStream >> out.method >> out.target >> version)) {
        return false;
    }
    if (version.rfind("HTTP/", 0) != 0) {
        return false;
    }

    const std::size_t queryStart = out.target.find('?');
    if (queryStart == std::string::npos) {
        out.path = urlDecode(out.target);
    } else {
        out.path = urlDecode(out.target.substr(0, queryStart));
        out.query = parseQueryString(out.target.substr(queryStart + 1));
    }

    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        out.headers[toLower(line.substr(0, colon))] = trim(line.substr(colon + 1));
    }

    const std::size_t contentLength = parseContentLength(out);
    if (contentLength > maxBodyBytes_) {
        return false;
    }
    const std::size_t bodyStart = headerEnd + 4;
    if (raw.size() < bodyStart + contentLength) {
        return false;
    }
    out.body = raw.substr(bodyStart, contentLength);
    return true;
}

void HttpServer::closeSocket(std::intptr_t socketHandle) {
    if (socketHandle == -1) {
        return;
    }
#ifdef _WIN32
    ::closesocket(toNative(socketHandle));
#else
    ::close(toNative(socketHandle));
#endif
}

bool HttpServer::sendAll(std::intptr_t socketHandle, const std::string& bytes) {
    std::size_t sentTotal = 0;
    while (sentTotal < bytes.size()) {
#ifdef _WIN32
        const int sent = ::send(toNative(socketHandle), bytes.data() + sentTotal, static_cast<int>(bytes.size() - sentTotal), 0);
#else
        const ssize_t sent = ::send(toNative(socketHandle), bytes.data() + sentTotal, bytes.size() - sentTotal, 0);
#endif
        if (sent <= 0) {
            return false;
        }
        sentTotal += static_cast<std::size_t>(sent);
    }
    return true;
}

} // namespace pixelwar::http
