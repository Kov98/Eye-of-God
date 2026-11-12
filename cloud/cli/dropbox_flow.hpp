// language: C++, file: cloud/cli/dropbox_flow.hpp
// Dropbox half of the collection flow, with the transport already built.
//
// WHY THIS SEAM EXISTS
//   The vault read, the WinHTTP transport and the token refresh live in
//   collect_flow.cpp and need a real machine. Everything that decides the
//   outcome - argument validation, config mapping, exit codes - lives here and
//   takes the client by reference, so it is proven offline against the mock
//   transport instead of against the Dropbox service.
#pragma once

#include "cloud/cli/collect_flow.hpp"
#include "cloud/connectors/dropbox/dropbox_collector.hpp"
#include "cloud/connectors/dropbox/dropbox_http.hpp"

#include <functional>

namespace deep::cloud {

struct DropboxFlowResult {
    // Process-style code: 0 complete, 2 bad arguments, 5 failed,
    // 6 partial or cancelled. Never 0 unless the collection really finished.
    int exit_code = 5;
    CollectFlowOutcome outcome;
};

DropboxFlowResult run_dropbox_collect(olho::cloud::dropbox::client& api,
                                      const CollectFlowOptions& options,
                                      const std::function<bool()>& cancelled);

}  // namespace deep::cloud
