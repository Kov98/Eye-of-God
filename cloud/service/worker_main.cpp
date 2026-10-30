#include "cloud/cli/collect_flow.hpp"
#include "job_queue.hpp"
#include "cloud/enterprise/secret_store.hpp"
#include "third_party/nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::int64_t heartbeat_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string heartbeat_id(std::string value) {
    for (char& c : value) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            c = '_';
    }
    if (value.empty()) value = "worker";
    return value;
}

void write_heartbeat(const std::string& case_dir, const std::string& worker_id,
                     int parallel, const std::string& status) {
    try {
        const std::string safe = heartbeat_id(worker_id);
        const std::filesystem::path root =
            std::filesystem::path(case_dir) / "cloud" / "workers";
        std::filesystem::create_directories(root);
        nlohmann::json heartbeat = {
            {"worker_id", worker_id},
            {"pid", static_cast<unsigned long>(GetCurrentProcessId())},
            {"parallel", parallel},
            {"status", status},
            {"last_seen_epoch", heartbeat_now()}
        };
        std::ofstream output(root / (safe + ".json"), std::ios::binary | std::ios::trunc);
        output << heartbeat.dump(2);
    } catch (const std::exception&) {
        // Heartbeat is diagnostic only; never fail a collection because of it.
    }
}

void usage() {
    std::cout
        << "OlhoDeDeus cloud worker\n"
        << "  --case DIR       case directory (required)\n"
        << "  --queue DIR      queue directory (default case/cloud/queue)\n"
        << "  --watch          keep polling\n"
        << "  --poll-ms N      poll interval (default 2000)\n"
        << "  --help\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string case_dir;
    std::string queue_dir;
    bool watch = false;
    bool deep_ignored = false;
    int poll_ms = 2000;
    int max_parallel = 1;
    int lease_seconds = 900;
    std::string graph_base_url;
    bool mock_http = false;
    std::string worker_id = "worker";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--case" && i + 1 < argc) case_dir = argv[++i];
        else if (arg == "--queue" && i + 1 < argc) queue_dir = argv[++i];
        else if (arg == "--deep") {
            // Kept for command-line compatibility only. The worker collects
            // in-process and never launches a child collector.
            if (i + 1 < argc) ++i;
            deep_ignored = true;
        }
        else if (arg == "--watch") watch = true;
        else if (arg == "--worker-id" && i + 1 < argc) worker_id = argv[++i];
        else if (arg == "--lease-seconds" && i + 1 < argc) lease_seconds = std::atoi(argv[++i]);
        else if (arg == "--base-url" && i + 1 < argc) graph_base_url = argv[++i];
        else if (arg == "--mock-http") mock_http = true;
        else if (arg == "--poll-ms" && i + 1 < argc) poll_ms = std::atoi(argv[++i]);
        else if (arg == "--max-parallel" && i + 1 < argc) max_parallel = std::atoi(argv[++i]);
        else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    if (mock_http) {
        if (graph_base_url.rfind("http://127.0.0.1:", 0) != 0 &&
            graph_base_url.rfind("http://localhost:", 0) != 0) {
            std::cerr << "mock-http requires a loopback --base-url\n";
            return 2;
        }
    } else if (!graph_base_url.empty() &&
               graph_base_url.rfind("https://graph.microsoft.com/", 0) != 0) {
        std::cerr << "base-url must be https://graph.microsoft.com/ unless mock-http is set\n";
        return 2;
    }
    if (case_dir.empty()) {
        usage();
        return 2;
    }
    if (deep_ignored) {
        std::cerr << "warning: --deep is deprecated and ignored; "
                     "the worker collects in-process\n";
    }
    if (queue_dir.empty()) queue_dir = case_dir + "\\cloud\\queue";
    if (poll_ms < 250) poll_ms = 250;
    if (lease_seconds < 30) lease_seconds = 30;
    if (max_parallel < 1) max_parallel = 1;
    if (max_parallel > 8) max_parallel = 8;
    if (worker_id == "worker") worker_id += "-" + std::to_string(GetCurrentProcessId());

    deep::cloud::JobQueue queue(queue_dir);
    std::cout << "cloud worker: case=" << case_dir << "\n";
    std::cout.flush();

    std::mutex log_mutex;
    auto log_line = [&](const std::string& line, bool error) {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (error) std::cerr << line << "\n";
        else std::cout << line << "\n";
        std::cout.flush();
    };

    auto process_job = [&](const deep::cloud::QueuedJob& job, const std::string& id,
                           int generation) {
        std::string vault_key = job.vault_key;
        if (vault_key.empty() && !job.vault_ref.empty()) {
            deep::cloud::SecretStore store;
            std::string resolve_error;
            if (!store.resolve(job.vault_ref, vault_key, resolve_error)) {
                std::string finish_error;
                queue.finish(job.job_id, "failed", "secret reference: " + resolve_error,
                             finish_error, generation);
                log_line("failed " + job.job_id + ": " + resolve_error, true);
                return;
            }
        }
        if (vault_key.empty()) {
            std::string finish_error;
            queue.finish(job.job_id, "failed", "job has no vault reference or key",
                         finish_error, generation);
            log_line("failed " + job.job_id + ": no vault reference or key", true);
            return;
        }
        deep::cloud::CollectFlowOptions options;
        options.case_dir = case_dir;
        options.account = job.account;
        options.tenant = job.tenant_id;
        options.vault_key = vault_key;
        options.legal_basis = job.legal_basis;
        options.job = job.job_id;
        options.since = job.since;
        options.until = job.until;
        options.max_pages = job.max_pages;
        options.download_attachments = job.download_attachments;
        if (!graph_base_url.empty()) options.base_url = graph_base_url;
        options.mock_http = mock_http;

        struct MonitorState {
            std::atomic<bool> stop{false};
            std::atomic<bool> cancelled{false};
            std::atomic<bool> job_cancelled{false};
            std::atomic<bool> lease_lost{false};
        } monitor;

        log_line("running " + job.job_id + " [" + id + "]", false);
        const std::wstring cancel_event_name =
            deep::cloud::cancel_event_name(job.job_id);
        HANDLE cancel_event = CreateEventW(nullptr, TRUE, FALSE,
            cancel_event_name.c_str());
        if (cancel_event) ResetEvent(cancel_event);
        std::thread monitor_thread([&]() {
            // Renew at one third of the lease, but wake on cancellation
            // immediately through the named event.
            const auto renew_every =
                std::chrono::seconds(std::max(2, lease_seconds / 3));
            auto next_renewal = std::chrono::steady_clock::now() + renew_every;
            while (!monitor.stop.load()) {
                if (cancel_event) {
                    const DWORD wait = WaitForSingleObject(cancel_event, 250);
                    if (wait == WAIT_OBJECT_0) {
                        monitor.cancelled.store(true);
                        break;
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(250));
                }
                if (monitor.stop.load()) break;
                const auto current = queue.get(job.job_id);
                if (!current) {
                    monitor.cancelled.store(true);
                    monitor.job_cancelled.store(true);
                    break;
                }
                if (queue.cancel_requested(job.job_id) || current->status == "cancelled") {
                    monitor.cancelled.store(true);
                    monitor.job_cancelled.store(true);
                    break;
                }
                if (current->status != "running") {
                    // Another actor finished or requeued the job; never
                    // overwrite its terminal state from this worker.
                    monitor.cancelled.store(true);
                    break;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now < next_renewal) continue;
                std::string lease_error;
                if (!queue.renew_lease(job.job_id, generation, lease_seconds,
                                       lease_error)) {
                    monitor.cancelled.store(true);
                    monitor.lease_lost.store(true);
                    break;
                }
                next_renewal = now + renew_every;
            }
        });

        deep::cloud::CollectFlowOutcome outcome;
        int exit_code = 5;
        try {
            exit_code = deep::cloud::run_collect_flow(
                options, [&monitor]() { return monitor.cancelled.load(); }, &outcome);
        } catch (const std::exception& exception) {
            outcome.error = exception.what();
        } catch (...) {
            outcome.error = "unknown collector error";
        }
        monitor.stop.store(true);
        monitor_thread.join();
        if (cancel_event) CloseHandle(cancel_event);

        if (monitor.cancelled.load() || outcome.cancelled) {
            // Cancelled (operator or stale lease): never report completed.
            const std::string reason = monitor.job_cancelled.load()
                ? "job cancelled"
                : (monitor.lease_lost.load() ? "worker lease lost" : "collection cancelled");
            log_line("stopped " + job.job_id + ": " + reason, true);
            const auto current = queue.get(job.job_id);
            if (current && current->status == "paused") {
                log_line("paused " + job.job_id, false);
                return;
            }
            if (current && current->status != "cancelled") {
                std::string retry_error;
                if (!queue.retry(job.job_id, reason, retry_error, generation))
                    log_line("cancel " + job.job_id + ": " + retry_error, true);
            }
            return;
        }
        if (exit_code == 0 && outcome.ok) {
            std::string finish_error;
            if (!queue.finish(job.job_id, "completed", "", finish_error, generation))
                log_line("completed " + job.job_id + ": " + finish_error, true);
            else
                log_line("completed " + job.job_id, false);
            return;
        }
        std::string message = outcome.error;
        if (message.empty()) message = "collector exit " + std::to_string(exit_code);
        if (outcome.partial) message = "partial: " + message;
        const auto current = queue.get(job.job_id);
        if (current && current->status == "cancelled") {
            log_line("cancelled " + job.job_id, false);
            return;
        }
        std::string retry_error;
        if (!queue.retry(job.job_id, message, retry_error, generation))
            log_line("retry " + job.job_id + ": " + retry_error, true);
        else
            log_line("retry " + job.job_id + ": " + message, true);
    };

    auto worker_loop = [&](const std::string& id) {
        write_heartbeat(case_dir, id, max_parallel, "running");
        for (;;) {
            bool worked = false;
            std::string recovery_error;
            queue.recover_expired(recovery_error);
            for (const auto& job : queue.list()) {
                if (job.status != "queued") continue;
                std::string claim_error;
                if (!queue.claim(job.job_id, id, lease_seconds, claim_error)) {
                    if (claim_error.find("backoff") == std::string::npos &&
                        claim_error.find("not queued") == std::string::npos)
                        log_line("claim " + job.job_id + ": " + claim_error, true);
                    continue;
                }
                worked = true;
                const auto claimed = queue.get(job.job_id);
                if (!claimed) continue;
                process_job(*claimed, id, claimed->lease_generation);
            }
            if (!watch) break;
            if (!worked) std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        }
        write_heartbeat(case_dir, id, max_parallel, "stopped");
    };

    if (max_parallel == 1) {
        worker_loop(worker_id);
    } else {
        std::vector<std::thread> threads;
        for (int index = 0; index < max_parallel; ++index) {
            threads.emplace_back(worker_loop, worker_id + "-" + std::to_string(index + 1));
        }
        for (auto& thread : threads) thread.join();
    }
    return 0;
}
