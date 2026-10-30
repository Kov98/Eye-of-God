#include "oauth_callback.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace deep::cloud {
namespace {

std::string url_decode(std::string_view value) {
    std::string out;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size() &&
                   std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
            const auto hex = [](char c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return c - 'A' + 10;
            };
            out.push_back(static_cast<char>(hex(value[i + 1]) * 16 + hex(value[i + 2])));
            i += 2;
        } else {
            out.push_back(value[i]);
        }
    }
    return out;
}

std::string query_value(std::string_view query, std::string_view key) {
    std::size_t begin = 0;
    while (begin <= query.size()) {
        const std::size_t end = query.find('&', begin);
        const std::string_view pair = query.substr(begin,
            end == std::string_view::npos ? std::string_view::npos : end - begin);
        const std::size_t equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == key) {
            return url_decode(pair.substr(equals + 1));
        }
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return {};
}

void send_all(SOCKET socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int result = send(socket, data.data() + sent,
            static_cast<int>(data.size() - sent), 0);
        if (result <= 0) return;
        sent += static_cast<std::size_t>(result);
    }
}

} // namespace

OAuthCallbackServer::OAuthCallbackServer() {
    WSADATA data{};
    WSAStartup(MAKEWORD(2, 2), &data);
}

OAuthCallbackServer::~OAuthCallbackServer() {
    stop();
    WSACleanup();
}

bool OAuthCallbackServer::start(std::string& error) {
    if (started_) return true;
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        error = "socket() failed: " + std::to_string(WSAGetLastError());
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR) {
        error = "bind() failed: " + std::to_string(WSAGetLastError());
        closesocket(listener);
        return false;
    }
    int length = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) ==
        SOCKET_ERROR) {
        error = "getsockname() failed: " + std::to_string(WSAGetLastError());
        closesocket(listener);
        return false;
    }
    if (listen(listener, 1) == SOCKET_ERROR) {
        error = "listen() failed: " + std::to_string(WSAGetLastError());
        closesocket(listener);
        return false;
    }
    socket_ = static_cast<std::uintptr_t>(listener);
    port_ = ntohs(address.sin_port);
    started_ = true;
    return true;
}

void OAuthCallbackServer::stop() {
    if (started_) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = static_cast<std::uintptr_t>(~0ull);
        port_ = 0;
        started_ = false;
    }
}

std::string OAuthCallbackServer::redirect_uri() const {
    if (!started_ || port_ == 0) return {};
    return "http://127.0.0.1:" + std::to_string(port_) + "/callback";
}

CallbackResult OAuthCallbackServer::wait(std::chrono::seconds timeout) {
    CallbackResult result;
    if (!started_) {
        result.error = "callback server not started";
        return result;
    }
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(static_cast<SOCKET>(socket_), &read_set);
    timeval tv{};
    tv.tv_sec = static_cast<long>(timeout.count());
    tv.tv_usec = 0;
    const int ready = select(0, &read_set, nullptr, nullptr, &tv);
    if (ready == 0) {
        result.timed_out = true;
        result.error = "OAuth callback timed out";
        return result;
    }
    if (ready == SOCKET_ERROR) {
        result.error = "select() failed: " + std::to_string(WSAGetLastError());
        return result;
    }
    sockaddr_in client{};
    int client_length = sizeof(client);
    SOCKET accepted = accept(static_cast<SOCKET>(socket_),
        reinterpret_cast<sockaddr*>(&client), &client_length);
    if (accepted == INVALID_SOCKET) {
        result.error = "accept() failed: " + std::to_string(WSAGetLastError());
        return result;
    }

    std::string request;
    char buffer[2048];
    while (request.size() < 16384) {
        const int received = recv(accepted, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        request.append(buffer, static_cast<std::size_t>(received));
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    const std::size_t first_space = request.find(' ');
    const std::size_t second_space = first_space == std::string::npos
        ? std::string::npos : request.find(' ', first_space + 1);
    if (first_space == std::string::npos || second_space == std::string::npos ||
        request.substr(0, first_space) != "GET") {
        result.error = "invalid OAuth callback request";
    } else {
        const std::string target = request.substr(first_space + 1,
            second_space - first_space - 1);
        const std::size_t question = target.find('?');
        const std::string_view query = question == std::string::npos
            ? std::string_view{} : std::string_view(target).substr(question + 1);
        result.code = query_value(query, "code");
        result.state = query_value(query, "state");
        result.error = query_value(query, "error");
        const std::string description = query_value(query, "error_description");
        if (!result.error.empty() && !description.empty()) result.error += ": " + description;
        if (result.code.empty() && result.error.empty())
            result.error = "callback has no code";
    }

    const std::string body =
        "<!doctype html><html><body style=\"font-family:Segoe UI;background:#121212;"
        "color:#ddd;padding:40px\"><h2>Olho de Deus</h2>"
        "<p>Autenticacao concluida. Voce pode fechar esta janela.</p></body></html>";
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
             << "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n"
             << body;
    send_all(accepted, response.str());
    closesocket(accepted);
    return result;
}

} // namespace deep::cloud
