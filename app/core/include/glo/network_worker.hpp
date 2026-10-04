#pragma once

#include "glo/client_core.hpp"

#include <memory>
#include <optional>

namespace glo {

// Non-admin UI facade. RelayPreferred starts one elevated instance of the same
// GLO.exe on demand and proxies only the narrow network-worker contract over a
// local named pipe. Account/access tokens never cross this boundary.
class NetworkWorkerClient {
public:
    using UpdateCallback = ClientCore::UpdateCallback;

    NetworkWorkerClient();
    ~NetworkWorkerClient();
    NetworkWorkerClient(const NetworkWorkerClient&) = delete;
    NetworkWorkerClient& operator=(const NetworkWorkerClient&) = delete;

    bool connect_async(const ClientOptions& options, UpdateCallback cb);
    void disconnect();
    void shutdown();
    void set_debug_logging(bool enabled) noexcept;
    ClientSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Called before any UI initialization. Returns an exit code when
// this process was launched as the elevated network worker; otherwise nullopt.
std::optional<int> run_network_worker_if_requested();

}  // namespace glo
