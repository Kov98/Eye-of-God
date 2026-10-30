// Standalone lease/fencing regression test for cloud/service/job_queue.cpp.
#include "cloud/service/job_queue.hpp"
#include "cloud/enterprise/secret_store.hpp"
#include <nlohmann/json.hpp>

#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (condition) {
        std::cout << "ok: " << message << "\n";
    } else {
        std::cout << "FAIL: " << message << "\n";
        ++failures;
    }
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("olho_lease_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    deep::cloud::JobQueue queue(root.string());
    deep::cloud::QueuedJob job;
    job.job_id = "lease-job";
    job.account = "analyst@example.test";
    job.tenant_id = "tenant";
    job.legal_basis = "warrant";
    job.vault_key = "vault/test";
    job.max_pages = 1;
    std::string error;
    check(queue.enqueue(job, error), "enqueue protects vault key");
    auto stored = queue.get(job.job_id);
    check(stored.has_value(), "job readable");
    check(stored && stored->vault_key == "vault/test", "vault key round-trips via DPAPI");
    check(queue.claim(job.job_id, "worker-a", 60, error), "first claim");
    stored = queue.get(job.job_id);
    check(stored && stored->lease_generation == 1, "claim increments generation");
    check(queue.renew_lease(job.job_id, 1, 120, error), "renew current lease");
    check(!queue.finish(job.job_id, "completed", "", error, 99), "stale finish is fenced");
    check(queue.finish(job.job_id, "completed", "", error, 1), "finish with current generation");
    stored = queue.get(job.job_id);
    check(stored && stored->status == "completed", "completed status");

    deep::cloud::QueuedJob cancelled = job;
    cancelled.job_id = "cancel-job";
    check(queue.enqueue(cancelled, error), "enqueue cancel job");
    check(queue.claim(cancelled.job_id, "worker-b", 60, error), "claim cancel job");
    check(queue.cancel(cancelled.job_id, error), "cancel job");
    check(queue.cancel_requested(cancelled.job_id), "cancel marker exists");
    check(!queue.renew_lease(cancelled.job_id, 1, 60, error), "cancelled lease cannot renew");

    deep::cloud::QueuedJob paused = job;
    paused.job_id = "pause-job";
    check(queue.enqueue(paused, error), "enqueue pause job");
    check(queue.claim(paused.job_id, "worker-c", 60, error), "claim pause job");
    check(queue.pause(paused.job_id, error), "pause running job");
    auto paused_state = queue.get(paused.job_id);
    check(paused_state && paused_state->status == "paused", "paused status");
    check(queue.cancel_requested(paused.job_id), "pause marker exists");
    check(queue.resume(paused.job_id, error), "resume paused job");
    paused_state = queue.get(paused.job_id);
    check(paused_state && paused_state->status == "queued", "resumed status queued");
    check(!queue.cancel_requested(paused.job_id), "pause marker removed");
    deep::cloud::QueuedJob expired = job;
    expired.job_id = "expired-job";
    check(queue.enqueue(expired, error), "enqueue expired job");
    check(queue.claim(expired.job_id, "worker-x", 30, error), "claim expired job");
    {
        const auto path = root / "expired-job.json";
        std::ifstream input(path.string());
        nlohmann::json document;
        input >> document;
        document["lease_until_epoch"] = 1;
        std::ofstream output(path.string(), std::ios::trunc);
        output << document.dump(2);
    }
    check(queue.recover_expired(error), "recover expired leases");
    auto expired_state = queue.get(expired.job_id);
    check(expired_state && expired_state->status == "queued" &&
              expired_state->lease_generation == 1,
          "expired lease requeued");

    deep::cloud::QueuedJob exhausted = job;
    exhausted.job_id = "exhausted-job";
    exhausted.max_attempts = 1;
    check(queue.enqueue(exhausted, error), "enqueue exhausted job");
    check(queue.claim(exhausted.job_id, "worker-y", 30, error), "claim exhausted job");
    {
        const auto path = root / "exhausted-job.json";
        std::ifstream input(path.string());
        nlohmann::json document;
        input >> document;
        document["lease_until_epoch"] = 1;
        std::ofstream output(path.string(), std::ios::trunc);
        output << document.dump(2);
    }
    check(queue.recover_expired(error), "recover exhausted lease");
    auto exhausted_state = queue.get(exhausted.job_id);
    check(exhausted_state && exhausted_state->status == "failed",
          "expired lease at max attempts fails");
    deep::cloud::SecretStore secrets((root / "secrets").string());
    std::string ref;
    check(secrets.create("vault/test", ref, error), "secret store creates ref");
    deep::cloud::QueuedJob ref_job = job;
    ref_job.job_id = "ref-job";
    ref_job.vault_key.clear();
    ref_job.vault_ref = ref;
    check(queue.enqueue(ref_job, error), "enqueue by ref");
    auto ref_state = queue.get(ref_job.job_id);
    check(ref_state && ref_state->vault_ref == ref, "queue stores vault ref");
    std::string resolved;
    check(secrets.resolve(ref, resolved, error) && resolved == "vault/test",
          "secret store resolves ref");
    std::ifstream queue_file((root / "ref-job.json").string());
    std::string queue_text((std::istreambuf_iterator<char>(queue_file)),
                         std::istreambuf_iterator<char>());
    check(queue_text.find("\"vault_key\"") == std::string::npos,
          "queue JSON has no vault_key field");
    deep::cloud::QueuedJob event_job = job;
    event_job.job_id = "event-job";
    check(queue.enqueue(event_job, error), "enqueue event job");
    check(queue.claim(event_job.job_id, "worker-e", 60, error), "claim event job");
    HANDLE cancel_event = CreateEventW(nullptr, TRUE, FALSE,
        deep::cloud::cancel_event_name(event_job.job_id).c_str());
    check(cancel_event != nullptr, "create named cancel event");
    if (cancel_event) ResetEvent(cancel_event);
    check(queue.cancel(event_job.job_id, error), "cancel event job");
    const DWORD event_wait = cancel_event
        ? WaitForSingleObject(cancel_event, 1000) : WAIT_FAILED;
    check(event_wait == WAIT_OBJECT_0, "cancel signals named event");
    if (cancel_event) CloseHandle(cancel_event);
    std::string orphan_ref;
    check(secrets.create("orphan", orphan_ref, error), "create orphan ref");
    int pruned = 0;
    check(secrets.prune({ref}, 0, pruned, error), "prune refs");
    check(pruned == 1, "prune removes orphan");
    std::string still_there;
    check(secrets.resolve(ref, still_there, error) && still_there == "vault/test",
          "prune keeps active ref");
    std::string orphan_value;
    check(!secrets.resolve(orphan_ref, orphan_value, error), "orphan ref removed");
    queue_file.close();
    std::filesystem::remove_all(root);
    if (failures == 0) {
        std::cout << "job queue lease tests: PASS\n";
        return 0;
    }
    std::cout << "job queue lease tests: FAIL (" << failures << ")\n";
    return 1;
}
