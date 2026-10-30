// Test fixture: write a DPAPI test token, create a vault_ref, enqueue one job.
#include "cloud/core/dpapi_token_vault.hpp"
#include "cloud/enterprise/secret_store.hpp"
#include "cloud/service/job_queue.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: worker_integration_setup <case> <queue> <vault-key>\n";
        return 2;
    }
    const std::string vault_key = argv[3];
    deep::cloud::DpapiTokenVault vault;
    deep::cloud::OAuthToken token;
    token.access_token = "integration-token";
    token.refresh_token = "integration-refresh";
    token.expires_in = 3600;
    token.reset_expiry();
    std::string error;
    if (!vault.save(vault_key, token)) {
        std::cerr << "could not save token\n";
        return 3;
    }
    deep::cloud::SecretStore secrets;
    std::string ref;
    if (!secrets.create(vault_key, ref, error)) {
        std::cerr << "secret: " << error << "\n";
        return 4;
    }
    deep::cloud::JobQueue queue(argv[2]);
    deep::cloud::QueuedJob job;
    job.job_id = "integration-job";
    job.account = "analyst@example.test";
    job.tenant_id = "tenant";
    job.legal_basis = "integration-test";
    job.vault_ref = ref;
    job.max_pages = 2;
    job.download_attachments = false;
    if (!queue.enqueue(job, error)) {
        std::cerr << "queue: " << error << "\n";
        return 5;
    }
    std::cout << "ref=" << ref << "\n";
    return 0;
}
