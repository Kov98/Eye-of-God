#include "winhttp_transport.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace deep::cloud {
namespace {

// Streaming chunk size. Every response body is read with WinHttpReadData in
// fixed 64 KiB pieces so a large `$value` download never has to be resident.
constexpr DWORD kReadChunkBytes = 64 * 1024;

std::wstring widen(std::string_view value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::string last_error_text(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string text = size ? narrow(std::wstring_view(buffer, size)) : std::string();
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' '))
        text.pop_back();
    return text.empty() ? ("WinHTTP error " + std::to_string(code)) : text;
}

HttpResponse failure(DWORD code) {
    HttpResponse response;
    response.transport_error = true;
    response.error = last_error_text(code);
    return response;
}

struct TargetUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

// Splits an absolute URL and enforces the TLS-by-default policy. The error text
// never contains the URL itself (which may carry a query token for other
// callers), only the reason.
bool parse_target(const std::string& url, bool allow_insecure_http, TargetUrl& target,
                  std::string& error) {
    if (url.empty()) {
        error = "empty URL";
        return false;
    }
    const std::wstring wide_url = widen(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
        error = last_error_text(GetLastError());
        return false;
    }
    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if (!secure && components.nScheme != INTERNET_SCHEME_HTTP) {
        error = "only http/https URLs are supported";
        return false;
    }
    if (!secure && !allow_insecure_http) {
        error = "TLS required; HTTP disabled outside explicit mock mode";
        return false;
    }
    target.secure = secure;
    target.port = components.nPort;
    target.host.assign(components.lpszHostName, components.dwHostNameLength);
    target.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        target.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (target.path.empty()) target.path = L"/";
    return true;
}

bool build_headers(const std::vector<std::pair<std::string, std::string>>& headers,
                   std::wstring& output, std::string& error) {
    for (const auto& [name, value] : headers) {
        if (name.find('\r') != std::string::npos ||
            name.find('\n') != std::string::npos ||
            value.find('\r') != std::string::npos ||
            value.find('\n') != std::string::npos) {
            error = "header contains CR/LF";
            return false;
        }
        output += widen(name);
        output += L": ";
        output += widen(value);
        output += L"\r\n";
    }
    return true;
}

std::vector<std::pair<std::string, std::string>> query_headers(HINTERNET handle) {
    std::vector<std::pair<std::string, std::string>> headers;
    DWORD size = 0;
    WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF,
        WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (size == 0) return headers;
    std::wstring raw(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_RAW_HEADERS_CRLF,
            WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &size,
            WINHTTP_NO_HEADER_INDEX)) {
        return headers;
    }
    const std::string lines = narrow(std::wstring_view(raw.data(), size / sizeof(wchar_t)));
    std::size_t begin = lines.find("\r\n");
    if (begin == std::string::npos) begin = 0;
    else begin += 2;
    while (begin < lines.size()) {
        const std::size_t end = lines.find("\r\n", begin);
        const std::string line = lines.substr(begin,
            end == std::string::npos ? std::string::npos : end - begin);
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos && colon + 1 < line.size()) {
            const std::size_t value_begin = colon + 1 + (line[colon + 1] == ' ' ? 1 : 0);
            headers.emplace_back(line.substr(0, colon), line.substr(value_begin));
        }
        if (end == std::string::npos) break;
        begin = end + 2;
    }
    return headers;
}

struct HttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~HttpHandles() {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

} // namespace

WinHttpTransport::WinHttpTransport(std::wstring user_agent,
                                           bool allow_insecure_http)
    : user_agent_(std::move(user_agent)),
      allow_insecure_http_(allow_insecure_http) {}

HttpResponse WinHttpTransport::send(const HttpRequest& request) {
    HttpResponse response;
    TargetUrl target;
    std::string error;
    if (!parse_target(request.url, allow_insecure_http_, target, error)) {
        response.transport_error = true;
        response.error = error;
        return response;
    }
    std::wstring headers;
    if (!build_headers(request.headers, headers, error)) {
        response.transport_error = true;
        response.error = error;
        return response;
    }

    HttpHandles handles;
    handles.session = WinHttpOpen(user_agent_.empty() ? L"OlhoDeDeus-Cloud/0.1" :
        user_agent_.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!handles.session) return failure(GetLastError());
    WinHttpSetTimeouts(handles.session, 10000, 10000, 15000, 30000);

    handles.connection = WinHttpConnect(handles.session, target.host.c_str(),
        target.port, 0);
    if (!handles.connection) return failure(GetLastError());

    const std::wstring method = widen(request.method.empty() ? "GET" : request.method);
    handles.request = WinHttpOpenRequest(handles.connection, method.c_str(),
        target.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, target.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!handles.request) return failure(GetLastError());

    DWORD redirects = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(handles.request, WINHTTP_OPTION_DISABLE_FEATURE, &redirects,
        sizeof(redirects));

    void* body = request.body.empty() ? nullptr : const_cast<char*>(request.body.data());
    const DWORD body_size = static_cast<DWORD>(request.body.size());
    if (!WinHttpSendRequest(handles.request,
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
            body, body_size, body_size, 0)) {
        return failure(GetLastError());
    }
    if (!WinHttpReceiveResponse(handles.request, nullptr)) {
        return failure(GetLastError());
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(handles.request, WINHTTP_QUERY_STATUS_CODE |
            WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        return failure(GetLastError());
    }
    response.status = static_cast<int>(status);
    response.headers = query_headers(handles.request);

    DWORD available = 0;
    do {
        available = 0;
        if (!WinHttpQueryDataAvailable(handles.request, &available)) break;
        if (available == 0) break;
        std::vector<char> chunk(available);
        DWORD read = 0;
        if (!WinHttpReadData(handles.request, chunk.data(), available, &read)) break;
        response.body.append(chunk.data(), read);
    } while (available > 0);

    return response;
}

HttpDownloadResult WinHttpTransport::download(const HttpDownloadRequest& request) {
    HttpDownloadResult result;
    TargetUrl target;
    std::string error;
    if (!parse_target(request.url, allow_insecure_http_, target, error)) {
        result.transport_error = true;
        result.error = error;
        return result;
    }
    std::wstring headers;
    if (!build_headers(request.headers, headers, error)) {
        result.transport_error = true;
        result.error = error;
        return result;
    }
    if (request.destination_path.empty()) {
        result.error = "download destination path is empty";
        return result;
    }

    HttpHandles handles;
    handles.session = WinHttpOpen(user_agent_.empty() ? L"OlhoDeDeus-Cloud/0.1" :
        user_agent_.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!handles.session) {
        result.transport_error = true;
        result.error = last_error_text(GetLastError());
        return result;
    }
    WinHttpSetTimeouts(handles.session, 10000, 10000, 15000, 60000);

    handles.connection = WinHttpConnect(handles.session, target.host.c_str(),
        target.port, 0);
    if (!handles.connection) {
        result.transport_error = true;
        result.error = last_error_text(GetLastError());
        return result;
    }

    const std::wstring method = widen(request.method.empty() ? "GET" : request.method);
    handles.request = WinHttpOpenRequest(handles.connection, method.c_str(),
        target.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, target.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!handles.request) {
        result.transport_error = true;
        result.error = last_error_text(GetLastError());
        return result;
    }

    // Redirects are never followed here: the bearer token must not be replayed
    // to whatever host a 3xx points at. The caller validates and re-issues the
    // request without credentials when the target host is allowlisted.
    DWORD redirects = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(handles.request, WINHTTP_OPTION_DISABLE_FEATURE, &redirects,
        sizeof(redirects));

    if (!WinHttpSendRequest(handles.request,
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
            nullptr, 0, 0, 0)) {
        result.transport_error = true;
        result.error = last_error_text(GetLastError());
        return result;
    }
    if (!WinHttpReceiveResponse(handles.request, nullptr)) {
        result.transport_error = true;
        result.error = last_error_text(GetLastError());
        return result;
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(handles.request, WINHTTP_QUERY_STATUS_CODE |
            WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
            &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        result.transport_error = true;
        result.error = last_error_text(GetLastError());
        return result;
    }
    result.status = static_cast<int>(status);
    result.headers = query_headers(handles.request);
    if (result.status >= 300 && result.status < 400) {
        result.redirected = true;
        result.location = find_header_value(result.headers, "Location");
        result.error = "HTTP " + std::to_string(result.status);
        return result;
    }
    if (result.status < 200 || result.status >= 300) {
        result.error = "HTTP " + std::to_string(result.status);
        return result;
    }

    // Reject an oversized payload from the declared length before creating any
    // file, then keep enforcing the same limit for every written chunk.
    std::uint64_t declared = 0;
    const std::string content_length = find_header_value(result.headers, "Content-Length");
    if (request.max_bytes > 0 && !content_length.empty() &&
        parse_decimal_u64(content_length, declared) && declared > request.max_bytes) {
        result.limit_exceeded = true;
        result.error = "download exceeds the configured max_bytes limit";
        return result;
    }

    DownloadSink sink;
    if (!sink.open(request.destination_path, request.max_bytes, error)) {
        result.error = error;
        return result;
    }

    std::vector<char> buffer(kReadChunkBytes);
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(handles.request, &available)) {
            sink.discard();
            result.transport_error = true;
            result.error = "WinHTTP download read failed: " +
                last_error_text(GetLastError());
            return result;
        }
        if (available == 0) break;
        const DWORD to_read = static_cast<DWORD>(
            std::min<std::size_t>(available, buffer.size()));
        DWORD read = 0;
        if (!WinHttpReadData(handles.request, buffer.data(), to_read, &read)) {
            sink.discard();
            result.transport_error = true;
            result.error = "WinHTTP download read failed: " +
                last_error_text(GetLastError());
            return result;
        }
        if (read == 0) break;
        if (!sink.write(buffer.data(), read, error)) {
            const bool limit_hit = sink.limit_exceeded();
            sink.discard();
            result.limit_exceeded = limit_hit;
            result.transport_error = !limit_hit;
            result.error = error;
            return result;
        }
    }

    if (!sink.commit(error)) {
        result.error = error;
        return result;
    }
    result.bytes = sink.bytes();
    result.sha256 = sink.sha256();
    result.ok = true;
    return result;
}

} // namespace deep::cloud
