#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deep::cloud {

struct QueueMetrics {
    int total = 0;
    int queued = 0;
    int running = 0;
    int completed = 0;
    int failed = 0;
    int cancelled = 0;
    int expired_leases = 0;
};

struct QueuedJob {
    std::string job_id;
    std::string account;
    std::string tenant_id;
    std::string legal_basis;
    std::string vault_key;
    std::string vault_ref;
    std::string vault_key_protected;
    std::string since;
    std::string until;
    int max_pages = 50;
    int priority = 0;
    bool download_attachments = false;
    int attempts = 0;
    int lease_generation = 0;
    int max_attempts = 3;
    std::int64_t lease_until_epoch = 0;
    std::int64_t next_attempt_epoch = 0;
    std::string worker_id;
    std::string status = "queued";
    std::string requested_by;
    std::string created_at_utc;
    std::string started_at_utc;
    std::string finished_at_utc;
    std::string error;
};

// Durable queue of requested cloud collections. The API only enqueues jobs;
// execution remains an explicit worker/CLI operation.
class JobQueue {
public:
    explicit JobQueue(std::string root);

    bool enqueue(const QueuedJob& job, std::string& error);
    std::vector<QueuedJob> list() const;
    QueueMetrics metrics() const;
    std::optional<QueuedJob> get(const std::string& job_id) const;
    bool claim(const std::string& job_id, const std::string& worker_id,
                int lease_seconds, std::string& error);
    bool renew_lease(const std::string& job_id, int generation,
                     int lease_seconds, std::string& error);
    bool retry(const std::string& job_id, const std::string& error_text,
               std::string& error, int generation = 0);
    int recover_expired(std::string& error);
    bool finish(const std::string& job_id, const std::string& status,
                const std::string& error_text, std::string& error,
                int generation = 0);
    bool cancel(const std::string& job_id, std::string& error);
    bool pause(const std::string& job_id, std::string& error);
    bool resume(const std::string& job_id, std::string& error);
    bool cancel_requested(const std::string& job_id) const;

private:
    std::string path_for(const std::string& job_id) const;
    std::string cancel_path_for(const std::string& job_id) const;
    std::string root_;
};

bool valid_job_id(const std::string& job_id);
std::wstring cancel_event_name(const std::string& job_id);

} // namespace deep::cloud
