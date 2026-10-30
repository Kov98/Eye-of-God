#include "cloud/core/winhttp_transport.hpp"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: https_download_test <url> <destination>\n";
        return 2;
    }
    const std::filesystem::path destination(argv[2]);
    std::error_code error;
    std::filesystem::remove(destination, error);
    std::filesystem::remove(destination.string() + ".part", error);

    deep::cloud::WinHttpTransport transport(L"OlhoDeDeus-HTTPS-Test/0.1", false);
    deep::cloud::HttpDownloadRequest request;
    request.url = argv[1];
    request.destination_path = destination.string();
    request.max_bytes = 2 * 1024 * 1024;
    const auto full = transport.download(request);
    if (!full.ok || full.bytes != 1024 * 1024 || full.sha256.size() != 64) {
        std::cerr << "download failed: status=" << full.status
                  << " bytes=" << full.bytes << " error=" << full.error << "\n";
        return 3;
    }
    if (!std::filesystem::exists(destination) ||
        std::filesystem::exists(destination.string() + ".part")) {
        std::cerr << "file/.part state invalid\n";
        return 4;
    }
    std::cout << "sha256=" << full.sha256 << "\n";

    deep::cloud::HttpDownloadRequest limited = request;
    limited.destination_path = destination.string() + ".limited";
    limited.max_bytes = 1024;
    const auto limited_result = transport.download(limited);
    if (limited_result.ok || !limited_result.limit_exceeded) {
        std::cerr << "limit was not enforced\n";
        return 5;
    }
    if (std::filesystem::exists(limited.destination_path) ||
        std::filesystem::exists(limited.destination_path + ".part")) {
        std::cerr << "limit left a file\n";
        return 6;
    }
    std::cout << "https download: PASS\n";
    return 0;
}
