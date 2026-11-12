// language: C++, file: inflate_probe.cpp, target: Windows 11
// Debug-only probe: includes the module's translation unit so the internal
// Inflater can be driven with a raw deflate stream and compared byte for byte
// with Python's zlib. Not part of the shipped module.
#include "../../android_backup.cpp"

#include <cstdio>
#include <string>
#include <vector>

namespace olho::android {

// Lives in the module's namespace so the internal Reader/CompressedReader types
// resolve exactly as they do inside android_backup.cpp.
class MemoryReader : public Reader {
public:
    explicit MemoryReader(std::vector<std::uint8_t> data) : data_(std::move(data)) {}
    bool read(std::uint8_t* dst, std::size_t cap, std::size_t& got, std::string&) override {
        got = 0;
        while (got < cap && pos_ < data_.size()) dst[got++] = data_[pos_++];
        return true;
    }

private:
    std::vector<std::uint8_t> data_;
    std::size_t pos_ = 0;
};

}  // namespace olho::android

namespace {

bool load(const char* path, std::vector<std::uint8_t>& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    std::uint8_t buffer[65536];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        out.insert(out.end(), buffer, buffer + got);
    }
    std::fclose(file);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::printf("uso: inflate_probe <zlib.bin> <saida.bin> <bytes-por-chamada>\n");
        return 2;
    }
    std::vector<std::uint8_t> compressed;
    if (!load(argv[1], compressed)) {
        std::printf("nao consegui ler %s\n", argv[1]);
        return 2;
    }
    const std::size_t chunk = static_cast<std::size_t>(std::atoi(argv[3]));

    // Direct inflater drive, so a container bug and a deflate bug are separable.
    {
        std::FILE* direct_out = std::fopen((std::string(argv[2]) + ".direct").c_str(), "wb");
        olho::android::Inflater inflater;
        inflater.reset();
        if (compressed.size() > 2) inflater.feed(compressed.data() + 2, compressed.size() - 2);
        std::string error;
        std::uint64_t direct = 0;
        for (int step = 0; step < 20; ++step) {
            const olho::android::Inflater::Status status = inflater.run(error);
            std::printf("inflater passo %d: status=%d out_len=%zu erro='%s'\n", step,
                        static_cast<int>(status), inflater.out_len(), error.c_str());
            if (direct_out && inflater.out_len() > 0) {
                std::fwrite(inflater.out_ptr(), 1, inflater.out_len(), direct_out);
            }
            direct += inflater.out_len();
            inflater.consume(inflater.out_len());
            if (status == olho::android::Inflater::Status::done) break;
            if (status == olho::android::Inflater::Status::failed) break;
            if (status == olho::android::Inflater::Status::need_input) {
                inflater.set_input_eof(true);
            }
        }
        if (direct_out) std::fclose(direct_out);
        std::printf("inflater direto: %llu bytes\n", static_cast<unsigned long long>(direct));
    }

    olho::android::CompressedReader reader;
    reader.init(std::make_unique<olho::android::MemoryReader>(compressed));

    std::FILE* out = std::fopen(argv[2], "wb");
    if (!out) return 2;
    std::vector<std::uint8_t> buffer(chunk == 0 ? 64 : chunk);
    std::uint64_t total = 0;
    for (;;) {
        std::size_t got = 0;
        std::string error;
        if (!reader.read(buffer.data(), buffer.size(), got, error)) {
            std::printf("FALHOU: %s (apos %llu bytes)\n", error.c_str(),
                        static_cast<unsigned long long>(total));
            std::fclose(out);
            return 1;
        }
        if (got == 0) break;
        std::fwrite(buffer.data(), 1, got, out);
        total += got;
    }
    std::fclose(out);
    std::printf("ok: %llu bytes, container=%s, integridade=%d\n",
                static_cast<unsigned long long>(total), reader.container_name(),
                static_cast<int>(reader.integrity_ok()));
    return 0;
}
