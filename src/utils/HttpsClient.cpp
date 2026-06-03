#include "pixelwar/utils/HttpsClient.hpp"

#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace pixelwar::utils {

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

struct WinHttpHandle {
    HINTERNET handle = nullptr;

    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET value) : handle(value) {}
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    ~WinHttpHandle() {
        reset();
    }

    void reset() {
        if (handle) {
            WinHttpCloseHandle(handle);
            handle = nullptr;
        }
    }

    explicit operator bool() const {
        return handle != nullptr;
    }
};
#endif

} // namespace

HttpsResponse httpsRequest(
    const std::string& method,
    const std::string& host,
    const std::string& path,
    const std::unordered_map<std::string, std::string>& headers,
    const std::string& body
) {
#ifndef _WIN32
    (void)method;
    (void)host;
    (void)path;
    (void)headers;
    (void)body;
    return HttpsResponse{0, {}, "https_client_unavailable"};
#else
    HttpsResponse response;

    WinHttpHandle session(WinHttpOpen(
        L"PixelWarRemake/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));
    if (!session) {
        response.error = "winhttp_open_failed";
        return response;
    }

    WinHttpHandle connection(WinHttpConnect(
        session.handle,
        widen(host).c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    ));
    if (!connection) {
        response.error = "winhttp_connect_failed";
        return response;
    }

    WinHttpHandle request(WinHttpOpenRequest(
        connection.handle,
        widen(method).c_str(),
        widen(path).c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    ));
    if (!request) {
        response.error = "winhttp_request_failed";
        return response;
    }

    for (const auto& [name, value] : headers) {
        const std::wstring header = widen(name + ": " + value);
        WinHttpAddRequestHeaders(
            request.handle,
            header.c_str(),
            static_cast<DWORD>(header.size()),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE
        );
    }

    LPVOID bodyData = body.empty() ? WINHTTP_NO_REQUEST_DATA : static_cast<LPVOID>(const_cast<char*>(body.data()));
    const DWORD bodySize = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(
            request.handle,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            bodyData,
            bodySize,
            bodySize,
            0
        )) {
        response.error = "winhttp_send_failed";
        return response;
    }

    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        response.error = "winhttp_receive_failed";
        return response;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (WinHttpQueryHeaders(
            request.handle,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX
        )) {
        response.status = static_cast<int>(status);
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            response.error = "winhttp_query_data_failed";
            return response;
        }
        if (available == 0) {
            break;
        }

        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, buffer.data(), available, &read)) {
            response.error = "winhttp_read_failed";
            return response;
        }
        response.body.append(buffer.data(), buffer.data() + read);
    }

    return response;
#endif
}

} // namespace pixelwar::utils
