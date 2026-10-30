#include "job_queue.hpp"

#include "third_party/nlohmann/json.hpp"

#include <windows.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace deep::cloud {
namespace {

std::int64_t now_epoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
    gmtime_s(&value, &now);
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &value);
    return text;
}

class QueueLock {
public:
    explicit QueueLock(const std::string& root, std::string& error) : error_(error) {
        try {
            const std::filesystem::path path = std::filesystem::path(root) / ".lock";
            std::filesystem::create_directories(path.parent_path());
            handle_ = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (handle_ == INVALID_HANDLE_VALUE) {
                error_ = "cannot open queue lock";
                return;
            }
            OVERLAPPED overlap{};
            if (!LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &overlap)) {
                error_ = "cannot acquire queue lock";
                CloseHandle(handle_);
                handle_ = INVALID_HANDLE_VALUE;
                return;
            }
            locked_ = true;
        } catch (const std::exception&) {
            error_ = "cannot acquire queue lock";
        }
    }
    ~QueueLock() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            if (locked_) {
                OVERLAPPED overlap{};
                UnlockFileEx(handle_, 0, 1, 0, &overlap);
            }
            CloseHandle(handle_);
        }
    }
    QueueLock(const QueueLock&) = delete;
    QueueLock& operator=(const QueueLock&) = delete;
    bool ok() const { return locked_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool locked_ = false;
    std::string& error_;
};
bool write_json(const std::filesystem::path& path, const nlohmann::json& document,
                std::string& error) {
    try {
        const auto parent = path.parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        const std::filesystem::path temporary = path.string() + ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "cannot write job queue entry";
            return false;
        }
        output << document.dump(2);
        output.close();
        if (!output) {
            error = "cannot write job queue entry";
            return false;
        }
        if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(temporary.wstring().c_str());
            error = "cannot commit job queue entry";
            return false;
        }
        return true;
    } catch (const std::exception&) {
        error = "cannot write job queue entry";
        return false;
    }
}

std::string base64_encode(const std::vector<BYTE>& data) {
    static const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int value = 0;
    int bits = -6;
    for (BYTE byte : data) {
        value = (value << 8) | byte;
        bits += 8;
        while (bits >= 0) {
            out.push_back(alphabet[(value >> bits) & 0x3f]);
            bits -= 6;
        }
    }
    if (bits > -6) out.push_back(alphabet[((value << 8) >> (bits + 8)) & 0x3f]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string protect_secret(const std::string& plain, bool& ok) {
    ok = false;
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    const_cast<BYTE*>(reinterpret_cast<const BYTE*>(plain.data()))};
    DATA_BLOB entropy{0, nullptr};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"Olho de Deus job vault", &entropy, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    std::vector<BYTE> bytes(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    ok = true;
    return base64_encode(bytes);
}

std::string unprotect_secret(const std::string& encoded) {
    if (encoded.empty()) return {};
    std::vector<BYTE> decoded;
    int value = 0;
    int bits = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        int index = -1;
        if (c >= 'A' && c <= 'Z') index = c - 'A';
        else if (c >= 'a' && c <= 'z') index = c - 'a' + 26;
        else if (c >= '0' && c <= '9') index = c - '0' + 52;
        else if (c == '+') index = 62;
        else if (c == '/') index = 63;
        if (index < 0) return {};
        value = (value << 6) + index;
        bits += 6;
        if (bits >= 0) {
            decoded.push_back(static_cast<BYTE>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    if (decoded.empty()) return {};
    DATA_BLOB input{static_cast<DWORD>(decoded.size()), decoded.data()};
    DATA_BLOB entropy{0, nullptr};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) return {};
    std::string plain(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return plain;
}
nlohmann::json serialize(const QueuedJob& job) {
    return {
        {"job_id", job.job_id},
        {"account", job.account},
        {"tenant_id", job.tenant_id},
        {"legal_basis", job.legal_basis},
        {"vault_ref", job.vault_ref},
        {"vault_key_protected", job.vault_key_protected},
        {"since", job.since},
        {"until", job.until},
        {"max_pages", job.max_pages},
        {"priority", job.priority},
        {"download_attachments", job.download_attachments},
        {"attempts", job.attempts},
        {"lease_generation", job.lease_generation},
        {"max_attempts", job.max_attempts},
        {"lease_until_epoch", job.lease_until_epoch},
        {"next_attempt_epoch", job.next_attempt_epoch},
        {"worker_id", job.worker_id},
        {"status", job.status},
        {"requested_by", job.requested_by},
        {"created_at_utc", job.created_at_utc},
        {"started_at_utc", job.started_at_utc},
        {"finished_at_utc", job.finished_at_utc},
        {"error", job.error}
    };
}

QueuedJob deserialize(const nlohmann::json& document) {
    QueuedJob job;
    job.job_id = document.value("job_id", "");
    job.account = document.value("account", "");
    job.tenant_id = document.value("tenant_id", "");
    job.legal_basis = document.value("legal_basis", "");
    job.vault_key = document.value("vault_key", "");
    job.vault_ref = document.value("vault_ref", "");
    job.vault_key_protected = document.value("vault_key_protected", "");
    if (!job.vault_key_protected.empty())
        job.vault_key = unprotect_secret(job.vault_key_protected);
    job.since = document.value("since", "");
    job.until = document.value("until", "");
    job.max_pages = document.value("max_pages", 50);
    job.priority = document.value("priority", 0);
    job.download_attachments = document.value("download_attachments", false);
    job.attempts = document.value("attempts", 0);
    job.lease_generation = document.value("lease_generation", 0);
    job.max_attempts = document.value("max_attempts", 3);
    job.lease_until_epoch = document.value("lease_until_epoch", 0);
    job.next_attempt_epoch = document.value("next_attempt_epoch", 0);
    job.worker_id = document.value("worker_id", "");
    job.status = document.value("status", "queued");
    job.requested_by = document.value("requested_by", "");
    job.created_at_utc = document.value("created_at_utc", "");
    job.started_at_utc = document.value("started_at_utc", "");
    job.finished_at_utc = document.value("finished_at_utc", "");
    job.error = document.value("error", "");
    return job;
}

} // namespace

std::wstring cancel_event_name(const std::string& job_id) {
    std::wstring name = L"Local\\OlhoDeDeus.cancel.";
    for (unsigned char c : job_id) name.push_back(static_cast<wchar_t>(c));
    return name;
}

namespace {

bool signal_cancel_event(const std::string& job_id) {
    const std::wstring name = cancel_event_name(job_id);
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    if (!event) return false;
    const bool signaled = SetEvent(event) != FALSE;
    CloseHandle(event);
    return signaled;
}

void reset_cancel_event(const std::string& job_id) {
    const std::wstring name = cancel_event_name(job_id);
    HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!event) return;
    ResetEvent(event);
    CloseHandle(event);
}

} // namespace
bool valid_job_id(const std::string& job_id) {
    if (job_id.empty() || job_id.size() > 128) return false;
    for (unsigned char c : job_id) {
        if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

JobQueue::JobQueue(std::string root) : root_(std::move(root)) {}

std::string JobQueue::path_for(const std::string& job_id) const {
    return (std::filesystem::path(root_) / (job_id + ".json")).string();
}

std::string JobQueue::cancel_path_for(const std::string& job_id) const {
    return (std::filesystem::path(root_) / (job_id + ".cancel")).string();
}

bool JobQueue::cancel_requested(const std::string& job_id) const {
    std::error_code error;
    return std::filesystem::exists(cancel_path_for(job_id), error);
}

bool JobQueue::enqueue(const QueuedJob& job, std::string& error) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    if (!valid_job_id(job.job_id) || job.account.empty() || job.tenant_id.empty() ||
        job.legal_basis.empty() ||
        (job.vault_key.empty() && job.vault_ref.empty())) {
        error = "job_id, account, tenant_id, legal_basis and vault_key are required";
        return false;
    }
    if (job.max_pages < 1 || job.max_pages > 500) {
        error = "max_pages must be between 1 and 500";
        return false;
    }
    if (job.priority < -100 || job.priority > 100) {
        error = "priority must be between -100 and 100";
        return false;
    }
    if (std::filesystem::exists(path_for(job.job_id))) {
        error = "job id already exists";
        return false;
    }
    QueuedJob stored = job;
    stored.vault_key_protected.clear();
    if (!stored.vault_key.empty()) {
        bool protected_ok = false;
        stored.vault_key_protected = protect_secret(stored.vault_key, protected_ok);
        if (!protected_ok) {
            error = "could not protect vault key";
            return false;
        }
    }
    if (stored.vault_ref.empty() && stored.vault_key.empty()) {
        error = "vault reference or key is required";
        return false;
    }
    stored.status = "queued";
    stored.created_at_utc = utc_now();
    stored.started_at_utc.clear();
    stored.finished_at_utc.clear();
    stored.error.clear();
    return write_json(path_for(job.job_id), serialize(stored), error);
}

std::vector<QueuedJob> JobQueue::list() const {
    std::vector<QueuedJob> jobs;
    std::error_code error;
    if (!std::filesystem::exists(root_, error)) return jobs;
    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
        if (error || !entry.is_regular_file() ||
            entry.path().extension() != ".json") continue;
        try {
            std::ifstream input(entry.path(), std::ios::binary);
            const auto document = nlohmann::json::parse(
                std::string((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>()));
            if (document.is_object()) jobs.push_back(deserialize(document));
        } catch (const std::exception&) {
            continue;
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const QueuedJob& left, const QueuedJob& right) {
        if (left.priority != right.priority) return left.priority > right.priority;
        return left.created_at_utc > right.created_at_utc;
    });
    return jobs;
}

QueueMetrics JobQueue::metrics() const {
    QueueMetrics metrics;
    const auto now = now_epoch();
    for (const auto& job : list()) {
        ++metrics.total;
        if (job.status == "queued") ++metrics.queued;
        else if (job.status == "running") {
            ++metrics.running;
            if (job.lease_until_epoch != 0 && job.lease_until_epoch <= now)
                ++metrics.expired_leases;
        } else if (job.status == "completed") ++metrics.completed;
        else if (job.status == "failed") ++metrics.failed;
        else if (job.status == "cancelled") ++metrics.cancelled;
    }
    return metrics;
}
std::optional<QueuedJob> JobQueue::get(const std::string& job_id) const {
    if (!valid_job_id(job_id)) return std::nullopt;
    std::ifstream input(path_for(job_id), std::ios::binary);
    if (!input) return std::nullopt;
    try {
        const auto document = nlohmann::json::parse(
            std::string((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>()));
        if (!document.is_object()) return std::nullopt;
        return deserialize(document);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}


bool JobQueue::claim(const std::string& job_id, const std::string& worker_id,
                     int lease_seconds, std::string& error) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (job->status != "queued") {
        error = "job is not queued";
        return false;
    }
    const auto now = now_epoch();
    if (job->next_attempt_epoch > now) {
        error = "job is in retry backoff";
        return false;
    }
    job->status = "running";
    job->started_at_utc = utc_now();
    job->worker_id = worker_id;
    job->lease_until_epoch = now + std::max(30, lease_seconds);
    ++job->attempts;
    ++job->lease_generation;
    reset_cancel_event(job_id);
    std::error_code marker_error;
    std::filesystem::remove(cancel_path_for(job_id), marker_error);
    return write_json(path_for(job_id), serialize(*job), error);
}

bool JobQueue::renew_lease(const std::string& job_id, int generation,
                           int lease_seconds, std::string& error) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (job->status != "running") {
        error = "job is not running";
        return false;
    }
    if (job->lease_generation != generation) {
        error = "stale lease generation";
        return false;
    }
    job->lease_until_epoch = now_epoch() + std::max(30, lease_seconds);
    return write_json(path_for(job_id), serialize(*job), error);
}
int JobQueue::recover_expired(std::string& error) {
    error.clear();
    QueueLock lock(root_, error);
    if (!lock.ok()) return 0;
    const auto now = now_epoch();
    int recovered = 0;
    for (auto& job : list()) {
        if (job.status != "running" || job.lease_until_epoch == 0 ||
            job.lease_until_epoch > now) continue;
        job.lease_until_epoch = 0;
        job.worker_id.clear();
        if (job.attempts < job.max_attempts) {
            job.status = "queued";
            job.next_attempt_epoch = now;
            job.error = "worker lease expired; retrying";
        } else {
            job.status = "failed";
            job.finished_at_utc = utc_now();
            job.error = "worker lease expired after maximum attempts";
        }
        std::string write_error;
        if (!write_json(path_for(job.job_id), serialize(job), write_error)) {
            error = write_error;
            continue;
        }
        ++recovered;
    }
    return recovered;
}

bool JobQueue::retry(const std::string& job_id, const std::string& error_text,
                     std::string& error, int generation) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (generation != 0 && job->lease_generation != generation) {
        error = "stale lease generation";
        return false;
    }
    job->lease_until_epoch = 0;
    job->worker_id.clear();
    if (job->attempts >= job->max_attempts) {
        job->status = "failed";
        job->finished_at_utc = utc_now();
        job->error = error_text;
    } else {
        job->status = "queued";
        job->next_attempt_epoch = now_epoch() + std::min(300, job->attempts * 30);
        job->error = error_text;
    }
    return write_json(path_for(job_id), serialize(*job), error);
}
bool JobQueue::finish(const std::string& job_id, const std::string& status,
                      const std::string& error_text, std::string& error,
                      int generation) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    if (status != "completed" && status != "failed" && status != "cancelled") {
        error = "invalid terminal status";
        return false;
    }
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (job->status == "completed" || job->status == "failed" ||
        job->status == "cancelled") {
        error = "job is already terminal";
        return false;
    }
    if (generation != 0 && job->lease_generation != generation) {
        error = "stale lease generation";
        return false;
    }
    job->status = status;
    job->finished_at_utc = utc_now();
    job->lease_until_epoch = 0;
    job->worker_id.clear();
    job->error = error_text;
    return write_json(path_for(job_id), serialize(*job), error);
}
bool JobQueue::pause(const std::string& job_id, std::string& error) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (job->status != "queued" && job->status != "running") {
        error = "job cannot be paused in its current state";
        return false;
    }
    const bool was_running = job->status == "running";
    job->status = "paused";
    job->lease_until_epoch = 0;
    job->worker_id.clear();
    job->next_attempt_epoch = 0;
    if (!write_json(path_for(job_id), serialize(*job), error)) return false;
    if (was_running) {
        signal_cancel_event(job_id);
        std::ofstream marker(cancel_path_for(job_id), std::ios::binary | std::ios::trunc);
        marker << "paused";
    }
    return true;
}

bool JobQueue::resume(const std::string& job_id, std::string& error) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (job->status != "paused") {
        error = "job is not paused";
        return false;
    }
    job->status = "queued";
    job->lease_until_epoch = 0;
    job->worker_id.clear();
    job->next_attempt_epoch = 0;
    job->error.clear();
    if (!write_json(path_for(job_id), serialize(*job), error)) return false;
    std::error_code marker_error;
    std::filesystem::remove(cancel_path_for(job_id), marker_error);
    return true;
}
bool JobQueue::cancel(const std::string& job_id, std::string& error) {
    QueueLock lock(root_, error);
    if (!lock.ok()) return false;
    auto job = get(job_id);
    if (!job) {
        error = "job not found";
        return false;
    }
    if (job->status != "queued" && job->status != "running") {
        error = "job cannot be cancelled in its current state";
        return false;
    }
    job->status = "cancelled";
    job->finished_at_utc = utc_now();
    job->lease_until_epoch = 0;
    job->worker_id.clear();
    if (!write_json(path_for(job_id), serialize(*job), error)) return false;
    signal_cancel_event(job_id);
    std::ofstream marker(cancel_path_for(job_id), std::ios::binary | std::ios::trunc);
    marker << "cancelled";
    return static_cast<bool>(marker);
}

} // namespace deep::cloud
