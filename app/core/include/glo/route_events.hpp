#pragma once

#include "glo/preflight_policy.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace glo {

enum class RouteControlState {
    Idle,
    WaitingForGame,
    RelayVerifying,
    RelayLocked,
    DirectLocked,
};

// UI/telemetry activity is based on recent successfully forwarded game UDP,
// not merely on pre-installed system-wide host routes. The relay must first
// have confirmed a reverse flow. 25 seconds matches the gameplay idle policy.
inline constexpr std::uint64_t kProfileGameplayActivityIdleMs = 25'000;
inline bool profile_relay_activity(RouteControlState state, std::uint32_t host,
                                   std::uint64_t forwarded_idle_ms) noexcept {
    return state == RouteControlState::RelayLocked && host != 0 &&
           forwarded_idle_ms < kProfileGameplayActivityIdleMs;
}

enum class RouteEventType {
    ProcessImageResolved,
    FirstForwarded,
    RelayReverseConfirmed,
    TunnelNonGameTrafficDropped,
    TunnelStopped,
    RelayUnavailable,
    RelayRejected,
    Disconnect,
};

struct RouteFlowIdentity {
    std::uint64_t route_epoch{0};
    std::uint32_t flow_id{0};
    std::uint32_t remote_ipv4_host{0};
    std::uint16_t remote_port{0};
    std::uint16_t local_port{0};

    [[nodiscard]] bool valid() const noexcept {
        return route_epoch != 0 && flow_id != 0 && remote_ipv4_host != 0 && remote_port != 0 && local_port != 0;
    }
};

inline bool same_route_flow(const RouteFlowIdentity& a, const RouteFlowIdentity& b) noexcept {
    return a.route_epoch == b.route_epoch && a.flow_id == b.flow_id &&
           a.remote_ipv4_host == b.remote_ipv4_host && a.remote_port == b.remote_port &&
           a.local_port == b.local_port;
}

inline bool same_endpoint_tuple(const RouteFlowIdentity& flow, const PreflightEndpoint& ep) noexcept {
    return flow.remote_ipv4_host == ep.remote_ipv4_host && flow.remote_port == ep.remote_port &&
           flow.local_port == ep.local_port;
}

// Verification evidence for one exact-route attempt. Roblox may have several
// same-host UDP tuples active at once, so proof must not be pinned to whichever
// tuple happened to traverse Wintun first.
class ForwardedFlowSet {
public:
    void clear() noexcept { flows_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return flows_.size(); }

    bool remember(const RouteFlowIdentity& flow) {
        if (!flow.valid()) return false;
        const bool first = flows_.find(flow.flow_id) == flows_.end();
        flows_[flow.flow_id] = flow;
        return first;
    }

    [[nodiscard]] bool matches(const RouteFlowIdentity& reverse) const noexcept {
        if (!reverse.valid()) return false;
        const auto it = flows_.find(reverse.flow_id);
        return it != flows_.end() && same_route_flow(it->second, reverse);
    }

    [[nodiscard]] bool contains(std::uint32_t flow_id) const noexcept {
        return flow_id != 0 && flows_.find(flow_id) != flows_.end();
    }

private:
    std::unordered_map<std::uint32_t, RouteFlowIdentity> flows_;
};

struct RouteEvent {
    RouteEventType type{RouteEventType::Disconnect};
    std::uint64_t generation{0};
    std::uint64_t route_cycle{0};
    std::optional<PreflightEndpoint> endpoint;
    std::string detail;
    std::wstring image_path;
    std::uint32_t process_id{0};
    std::optional<RouteFlowIdentity> flow;
    std::uint8_t ip_protocol{0};
    std::uint16_t relay_reject_reason{0};
    std::uint32_t relay_flow_id{0};
};

// Single-consumer queue for routing-control events. Producers are the Wintun packet thread and relay receive path.
class RouteEventQueue {
public:
    void push(RouteEvent event) {
        {
            std::scoped_lock lock(mu_);
            if (closed_) return;
            queue_.push_back(std::move(event));
        }
        cv_.notify_one();
    }

    bool wait_pop(RouteEvent& out) {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    template <class Clock, class Duration>
    bool wait_pop_until(RouteEvent& out, const std::chrono::time_point<Clock, Duration>& deadline) {
        std::unique_lock lock(mu_);
        cv_.wait_until(lock, deadline, [&] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        queue_.pop_front();
        return true;
    }

    void close() noexcept {
        {
            std::scoped_lock lock(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<RouteEvent> queue_;
    bool closed_{false};
};

// Small portable state guard used by the routing worker and tests. Route locks
// are terminal for one gameplay cycle, but a later game/server cycle may return
// to WaitingForGame without reconnecting the whole GLO control session.
class RouteControlModel {
public:
    void reset(std::uint64_t generation, RouteControlState initial) noexcept {
        generation_ = generation;
        state_ = initial;
    }

    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] RouteControlState state() const noexcept { return state_; }
    [[nodiscard]] bool locked() const noexcept {
        return state_ == RouteControlState::RelayLocked || state_ == RouteControlState::DirectLocked;
    }

    bool verifying(std::uint64_t generation) noexcept {
        if (generation != generation_ || state_ != RouteControlState::WaitingForGame) return false;
        state_ = RouteControlState::RelayVerifying;
        return true;
    }
    bool lock_relay(std::uint64_t generation) noexcept {
        if (generation != generation_ || state_ != RouteControlState::RelayVerifying) return false;
        state_ = RouteControlState::RelayLocked;
        return true;
    }
    bool lock_direct(std::uint64_t generation) noexcept {
        if (generation != generation_ || state_ == RouteControlState::DirectLocked) return false;
        state_ = RouteControlState::DirectLocked;
        return true;
    }
    bool next_gameplay_cycle(std::uint64_t generation) noexcept {
        if (generation != generation_ || state_ == RouteControlState::Idle) return false;
        state_ = RouteControlState::WaitingForGame;
        return true;
    }

private:
    std::uint64_t generation_{0};
    RouteControlState state_{RouteControlState::Idle};
};

}  // namespace glo
