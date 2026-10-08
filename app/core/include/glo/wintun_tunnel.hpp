#pragma once

#include "glo/protocol.hpp"
#include "glo/preflight_policy.hpp"
#include "glo/route_scope.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>

namespace glo {

struct WintunFlowIdentity {
    std::uint64_t route_epoch{0};
    std::uint32_t flow_id{0};
    std::uint32_t remote_ipv4_host{0};
    std::uint16_t remote_port{0};
    std::uint16_t local_port{0};
};

enum class WintunEventType {
    FirstForwarded,
    NonGameTrafficDropped,
    StoppedUnexpectedly,
};

struct WintunEvent {
    WintunEventType type{WintunEventType::StoppedUnexpectedly};
    WintunFlowIdentity flow{};
    std::uint8_t ip_protocol{0};
};

// Aggregated for the currently installed exact /32 route; no payloads or tokens.
struct WintunRouteDiagnostics {
    std::uint64_t epoch{0}, rx_total{0}, rx_game_host{0}, rx_game_udp{0};
    std::uint64_t forwarded{0}, send_failed{0}, bad_ip{0}, other_host{0};
    std::uint64_t non_udp{0}, wrong_port{0}, stale_epoch{0}, malformed_udp{0}, oversize{0};
};

class WintunTunnel {
public:
    using SendControlFn = std::function<bool(const protocol::Packet&)>;
    using SendDataFn = std::function<bool(std::uint32_t flow_id, std::uint64_t sequence,
                                          std::uint32_t remote_ipv4_host, std::uint16_t remote_port,
                                          std::uint16_t local_port, std::span<const std::uint8_t> payload)>;
    using EventFn = std::function<void(WintunEvent)>;

    WintunTunnel();
    ~WintunTunnel();
    WintunTunnel(const WintunTunnel&) = delete;
    WintunTunnel& operator=(const WintunTunnel&) = delete;

    // Bring up the adapter/session once and keep it active until Disconnect.
    // arm() installs no routes.
    bool arm(const std::string& dll_path,
             std::uint64_t session_id,
             SendControlFn control_sender,
             SendDataFn data_sender,
             EventFn event_fn,
             std::string& error);

    // v0.3.15 active client API: install exactly one verified /32 endpoint or clear it.
    // The old live-flow guard / generic route list / polling-health helpers were
    // removed because the preflight route worker is the only routing authority.
    bool set_endpoint(const PreflightEndpoint& endpoint, std::string& error);
    void clear_routes();
    // Diagnostic counters are active only for the verification window.
    void set_diagnostics_enabled(bool enabled) noexcept;
    WintunRouteDiagnostics route_diagnostics() const noexcept;

    void stop();
    bool running() const noexcept { return running_.load(); }
    bool route_active() const noexcept { return route_active_.load(); }

    // Age of the most recent successful DATA_C2S forward for the currently
    // routed gameplay host. UINT64_MAX means no matching forwarded packet has
    // been observed since the route was installed. This is activity evidence,
    // not a timer-driven health probe.
    std::uint64_t forwarded_idle_ms(std::uint32_t remote_ipv4_host) const noexcept;

    // True means the reply matched a live local flow and was injected into Wintun.
    // When supplied, injected_flow receives the exact route epoch / flow tuple
    // that accepted the reverse packet.
    bool inject_reply(std::uint32_t flow_id, std::span<const std::uint8_t> udp_payload,
                      WintunFlowIdentity* injected_flow = nullptr);

private:
    struct Impl;
    Impl* impl_{nullptr};
    std::atomic_bool running_{false};
    std::atomic_bool route_active_{false};
};

}  // namespace glo
