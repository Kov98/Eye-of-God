// Minimal WinHTTP smoke client for the synthetic Graph mock.
// Usage: cloud_transport_smoke.exe http://127.0.0.1:8765/v1.0/me

#include "cloud/core/winhttp_transport.hpp"

#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: cloud_transport_smoke <url>\n";
        return 2;
    }
    deep::cloud::WinHttpTransport transport(L"OlhoDeDeus-Cloud-Smoke/0.1", true);
    deep::cloud::HttpRequest request;
    request.method = "GET";
    request.url = argv[1];
    request.headers.push_back({"Authorization", "Bearer test-token"});
    request.headers.push_back({"Accept", "application/json"});
    const auto response = transport.send(request);
    std::cout << "status=" << response.status
              << " body_bytes=" << response.body.size()
              << " transport_error=" << (response.transport_error ? 1 : 0) << "\n";
    if (response.transport_error) {
        std::cerr << "error: " << response.error << "\n";
        return 1;
    }
    return response.ok() ? 0 : 1;
}
