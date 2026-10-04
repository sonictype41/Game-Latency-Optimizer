#pragma once

#include "glo/game_detector.hpp"
#include "glo/game_profile.hpp"
#include "glo/route_policy.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace glo {

enum class ConnectionState {
    Disconnected,
    Connecting,
    Monitoring,
    Direct,
    Relayed,
    Degraded,
};

enum class RoutingPolicy { RelayPreferred, DirectOnly };

enum class DirectReason {
    None,
    CapacityFull,
    Maintenance,
    RelayUnavailable,
    RelayDegraded,
    LocalFallback,
    DirectSelected,
};

enum class UiPhase {
    Disconnected,
    ConnectedWaitingForGame,
    ConnectedStandby,
    Gameplay,
};

enum class ActiveRoute { Direct, Relay };

enum class ClientErrorCode {
    WorkerStartFailed,
    WinsockInitFailed,
    RelayResolveFailed,
    RelaySocketFailed,
    RelaySocketConfigFailed,
    RelayConnectFailed,
    RelayHandshakeSendFailed,
    RelayHandshakeTimeout,
    WintunInitFailed,
    EndpointGateInitFailed,
    SessionAdmissionFailed,
    RelayKeyMissing,
    RelayKeyInvalid,
    SessionExpired,
};

struct ClientError {
    ClientErrorCode code{ClientErrorCode::RelayHandshakeTimeout};
    std::uint64_t generation{0};
    std::string title;
    std::string message;
    bool operator==(const ClientError&) const = default;
};

constexpr UiPhase classify_ui_phase(bool connected, bool game_running, bool gameplay_active) noexcept {
    if (!connected) return UiPhase::Disconnected;
    if (!game_running) return UiPhase::ConnectedWaitingForGame;
    return gameplay_active ? UiPhase::Gameplay : UiPhase::ConnectedStandby;
}

struct ClientOptions {
    // v0.12 consumes a complete portable session config. Account, entitlement,
    // scheduler and issuer semantics live outside the client.
    std::string relay_host;
    std::uint16_t relay_port{43170};
    std::string relay_public_key;
    std::vector<std::uint8_t> session_grant;
    std::string timeout_message{"Session expired."};
    std::string wintun_path{"wintun.dll"}; // internal install layout, never config data
    GameId game_id{default_game()};
    std::string game_exe_path; // privileged helper resolves this itself; not config data
    RoutingPolicy routing_policy{RoutingPolicy::RelayPreferred};
    unsigned force_handover_grace_ms{1800};
    bool dbg_log{false};
};

// UI-facing snapshot intentionally contains only product state plus telemetry.
// Endpoint-gate state, candidate state, flow IDs and verification details live in
// the event-driven route worker and optional dbg_log.txt, not in the gamer UI.
struct ClientSnapshot {
    ConnectionState state{ConnectionState::Disconnected};
    UiPhase phase{UiPhase::Disconnected};
    ActiveRoute active_route{ActiveRoute::Direct};
    DirectReason direct_reason{DirectReason::None};
    PathStats direct_path;
    PathStats relay_path;
    PathStats relay_link;
    std::uint64_t session_id{0};
    GameId game_id{default_game()};
    std::string game_name{"Roblox"};
    bool game_running{false};
    bool gameplay_active{false};
    std::uint32_t game_pid{0};
    std::size_t game_udp_flows{0};
    std::string gameplay_endpoint;  // diagnostic only; the default UI intentionally hides it
    std::uint32_t gameplay_host_ipv4{0};
    // Gamer-facing quality telemetry. These fields are deliberately optional:
    // GLO shows "--" instead of inventing a number when the active path cannot
    // be measured safely. Routing never depends on these values.
    std::optional<double> gameplay_ping_ms;
    std::optional<double> gameplay_loss_pct;
    std::uint32_t session_remaining_seconds{0};
    std::optional<ClientError> error;
    std::string message{"Disconnected"};
};

class ClientCore {
public:
    using UpdateCallback = std::function<void(const ClientSnapshot&)>;

    ClientCore();
    ~ClientCore();
    ClientCore(const ClientCore&) = delete;
    ClientCore& operator=(const ClientCore&) = delete;

    bool connect_async(const ClientOptions& options, UpdateCallback cb);
    void disconnect();
    void set_debug_logging(bool enabled) noexcept;
    ClientSnapshot snapshot() const;

private:
    void worker(ClientOptions options);
    void publish(ConnectionState state, const std::string& message);
    void publish_connect_error(ClientErrorCode code, const std::string& title, const std::string& message);
    void set_game_state(const GameState& game);

    mutable std::mutex mutex_;
    ClientSnapshot snapshot_;
    std::optional<ClientSnapshot> last_published_;
    UpdateCallback callback_;
    std::thread worker_;
    std::atomic_bool stop_{false};
    std::atomic_bool debug_log_requested_{false};
    std::uint64_t next_error_generation_{1};
};

const char* state_name(ConnectionState state) noexcept;
const char* phase_name(UiPhase phase) noexcept;

constexpr const char* direct_reason_name(DirectReason reason) noexcept {
    switch (reason) {
        case DirectReason::None: return "none";
        case DirectReason::CapacityFull: return "capacity_full";
        case DirectReason::Maintenance: return "maintenance";
        case DirectReason::RelayUnavailable: return "relay_unavailable";
        case DirectReason::RelayDegraded: return "relay_degraded";
        case DirectReason::LocalFallback: return "local_fallback";
        case DirectReason::DirectSelected: return "direct_selected";
    }
    return "none";
}

}  // namespace glo
