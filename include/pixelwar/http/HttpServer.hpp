#pragma once

#include "pixelwar/http/Router.hpp"
#include "pixelwar/utils/ThreadPool.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace pixelwar::http {

class HttpServer {
public:
    HttpServer(std::string host, std::uint16_t port, std::size_t workerCount, std::size_t maxBodyBytes, Router& router);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void run();
    void stop();

private:
    class SocketRuntime;

    void handleClient(std::intptr_t clientSocket, std::string remoteAddress);
    bool parseRequest(const std::string& raw, HttpRequest& out) const;
    static void closeSocket(std::intptr_t socketHandle);
    static bool sendAll(std::intptr_t socketHandle, const std::string& bytes);

    std::string host_;
    std::uint16_t port_;
    std::size_t maxBodyBytes_;
    Router& router_;
    utils::ThreadPool workers_;
    std::unique_ptr<SocketRuntime> socketRuntime_;
    std::atomic_bool running_{false};
    std::intptr_t listenSocket_{-1};
};

} // namespace pixelwar::http

