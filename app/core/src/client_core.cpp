#include "glo/secure_transport.hpp"
#include <sodium.h>
#include <filesystem>
#include <fstream>
#include "glo/client_core.hpp"
#include "glo/protocol.hpp"
#include "glo/quality_metrics.hpp"
#include "glo/game_detector.hpp"
#include "glo/profile_hosts.hpp"
#include "glo/route_policy.hpp"
#include "glo/route_scope.hpp"

#include "glo/route_events.hpp"
#include "glo/client_log.hpp"
#include "glo/wintun_tunnel.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <vector>
#include <utility>

namespace glo {
namespace {
using Clock = std::chrono::steady_clock;

std::uint64_t random_u64() {
    std::uint64_t value{}; randombytes_buf(&value,sizeof(value)); return value;
}

bool packet_send(SOCKET sock, const protocol::Packet& p, std::mutex& send_mu, secure::Client& transport) {
    std::vector<std::uint8_t> bytes;
    if (!transport.seal(p, bytes)) return false;
    std::scoped_lock lock(send_mu);
    return send(sock,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),0)==static_cast<int>(bytes.size());
}

bool data_send(SOCKET sock, std::uint64_t session_id, std::uint32_t flow_id, std::uint64_t sequence,
               std::uint32_t remote_ipv4_host, std::uint16_t remote_port, std::uint16_t local_port,
               std::span<const std::uint8_t> payload, std::mutex& send_mu,
               int* failure_kind = nullptr, int* socket_error = nullptr) {
    if (failure_kind) *failure_kind = 0;
    if (socket_error) *socket_error = 0;
    std::array<std::uint8_t, protocol::kDataMaxDatagram> bytes;
    std::size_t written = 0;
    if (!protocol::encode_data_c2s(session_id, sequence, flow_id, remote_ipv4_host, remote_port, local_port,
                                   payload, bytes, written)) {
        if (failure_kind) *failure_kind = 1;  // local serialization
        return false;
    }
    std::scoped_lock lock(send_mu);
    const int n = send(sock,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(written),0);
    if (n == static_cast<int>(written)) return true;
    if (failure_kind) *failure_kind = 2;  // socket failure/short send
    if (socket_error && n == SOCKET_ERROR) *socket_error = WSAGetLastError();
    return false;
}

enum class RxKind { None, Control, Data };
struct RxPacket {
    std::array<std::uint8_t, secure::kMaxDatagram + 1> bytes{};
    protocol::Packet control{};
    protocol::DataView data{};
};
RxKind packet_recv(SOCKET sock, RxPacket& rx, secure::Client& transport) {
    const int n=recv(sock,reinterpret_cast<char*>(rx.bytes.data()),static_cast<int>(rx.bytes.size()),0);
    if(n<=0) return RxKind::None;
    const auto b=std::span<const std::uint8_t>(rx.bytes.data(),static_cast<std::size_t>(n));
    if(protocol::decode_data_view(b,rx.data)) return RxKind::Data;
    if(transport.open(b,rx.control)) return RxKind::Control;
    return RxKind::None;
}
bool same_path_stats(const PathStats& a, const PathStats& b) noexcept {
    return a.rtt_ms == b.rtt_ms && a.jitter_ms == b.jitter_ms && a.loss_pct == b.loss_pct &&
           a.sent == b.sent && a.received == b.received;
}

bool same_snapshot(const ClientSnapshot& a, const ClientSnapshot& b) noexcept {
    return a.state == b.state && a.phase == b.phase && a.active_route == b.active_route &&
           a.direct_reason == b.direct_reason &&
           same_path_stats(a.direct_path, b.direct_path) && same_path_stats(a.relay_path, b.relay_path) &&
           same_path_stats(a.relay_link, b.relay_link) &&
           a.session_id == b.session_id && a.game_id == b.game_id && a.game_name == b.game_name &&
           a.game_running == b.game_running && a.gameplay_active == b.gameplay_active && a.game_pid == b.game_pid &&
           a.game_udp_flows == b.game_udp_flows && a.gameplay_endpoint == b.gameplay_endpoint &&
           a.gameplay_host_ipv4 == b.gameplay_host_ipv4 &&
           a.gameplay_ping_ms == b.gameplay_ping_ms && a.gameplay_loss_pct == b.gameplay_loss_pct &&
           a.session_remaining_seconds == b.session_remaining_seconds &&
           a.error == b.error && a.message == b.message;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}


}  // namespace

ClientCore::ClientCore() = default;
ClientCore::~ClientCore() { disconnect(); }

bool ClientCore::connect_async(const ClientOptions& options, UpdateCallback cb) {
    disconnect();
    debug_log_requested_.store(options.dbg_log, std::memory_order_release);
    {
        std::scoped_lock lock(mutex_);
        snapshot_ = ClientSnapshot{};
        last_published_.reset();
    }
    callback_ = std::move(cb);
    stop_ = false;
    publish(ConnectionState::Connecting, "Connecting...");
    try {
        worker_ = std::thread(&ClientCore::worker, this, options);
    } catch (const std::exception& e) {
        publish_connect_error(
            ClientErrorCode::WorkerStartFailed,
            "GLO - Connection error",
            "GLO could not start its connection worker.\n\n" + std::string(e.what()) +
                "\n\nGLO was not connected. Close other applications if the system is low on resources, then try again.");
        return false;
    } catch (...) {
        publish_connect_error(
            ClientErrorCode::WorkerStartFailed,
            "GLO - Connection error",
            "GLO could not start its connection worker.\n\nGLO was not connected. Restart GLO and try again.");
        return false;
    }
    return true;
}

void ClientCore::disconnect() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    {
        std::scoped_lock lock(mutex_);
        snapshot_ = ClientSnapshot{};
        snapshot_.phase = UiPhase::Disconnected;
        snapshot_.state = ConnectionState::Disconnected;
        snapshot_.message = "Disconnected";
        snapshot_.active_route = ActiveRoute::Direct;
    }
    publish(ConnectionState::Disconnected, "Disconnected");
}


void ClientCore::set_debug_logging(bool enabled) noexcept {
    debug_log_requested_.store(enabled, std::memory_order_release);
}

ClientSnapshot ClientCore::snapshot() const {
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

void ClientCore::publish(ConnectionState state, const std::string& message) {
    UpdateCallback cb;
    ClientSnapshot cp;
    bool changed = false;
    {
        std::scoped_lock lock(mutex_);
        snapshot_.state = state;
        snapshot_.message = message;
        if (state == ConnectionState::Disconnected) snapshot_.phase = UiPhase::Disconnected;
        if (!last_published_ || !same_snapshot(snapshot_, *last_published_)) {
            cp = snapshot_;
            last_published_ = snapshot_;
            cb = callback_;
            changed = true;
        }
    }
    if (changed && cb) cb(cp);
}

void ClientCore::publish_connect_error(ClientErrorCode code, const std::string& title, const std::string& message) {
    // A Connect failure is a single terminal UI transition: Not connected + one
    // generation-tagged modal. Runtime relay/admission failures deliberately do
    // not use this path because they fail open to Direct instead.
    if (stop_.load(std::memory_order_acquire)) return;

    UpdateCallback cb;
    ClientSnapshot cp;
    bool changed = false;
    {
        std::scoped_lock lock(mutex_);
        snapshot_ = ClientSnapshot{};
        snapshot_.state = ConnectionState::Disconnected;
        snapshot_.phase = UiPhase::Disconnected;
        snapshot_.active_route = ActiveRoute::Direct;
        snapshot_.direct_reason = DirectReason::None;
        snapshot_.message = "Not connected";

        ClientError error;
        error.code = code;
        error.generation = next_error_generation_++;
        if (next_error_generation_ == 0) next_error_generation_ = 1;
        error.title = title;
        error.message = message;
        snapshot_.error = std::move(error);

        if (!last_published_ || !same_snapshot(snapshot_, *last_published_)) {
            cp = snapshot_;
            last_published_ = snapshot_;
            cb = callback_;
            changed = true;
        }
    }
    if (changed && cb) cb(cp);
}

void ClientCore::set_game_state(const GameState& game) {
    std::scoped_lock lock(mutex_);
    const UiPhase next_phase = classify_ui_phase(true, game.process_running, game.gameplay_active);
    const bool phase_changed = snapshot_.phase != next_phase;
    snapshot_.phase = next_phase;
    snapshot_.game_id = game.game_id;
    snapshot_.game_name = std::string(game_name(game.game_id));
    snapshot_.game_running = game.process_running;
    snapshot_.gameplay_active = game.gameplay_active;
    snapshot_.game_pid = game.primary_pid;
    snapshot_.game_udp_flows = game.gameplay_active ? 1u : 0u;
    snapshot_.gameplay_host_ipv4 = game.gameplay_active ? game.gameplay_host_ipv4 : 0u;
    snapshot_.gameplay_endpoint = game.gameplay_active && game.gameplay_host_ipv4 != 0
                                    ? ipv4_text(game.gameplay_host_ipv4) : std::string{};

    if (phase_changed && next_phase != UiPhase::Gameplay) {
        snapshot_.direct_path = {};
        snapshot_.relay_path = {};
        snapshot_.relay_link = {};
        snapshot_.gameplay_ping_ms.reset();
        snapshot_.gameplay_loss_pct.reset();
        snapshot_.gameplay_endpoint.clear();
    }
}

void ClientCore::worker(ClientOptions options) {
    ClientLog log;
    std::string log_error;
    auto sync_debug_log = [&]() {
        const bool requested = debug_log_requested_.load(std::memory_order_acquire);
        if (requested && !log.enabled()) {
            if (!log.enable_debug_file(log_error)) {
                OutputDebugStringA(("GLO debug log: " + log_error + "\r\n").c_str());
            } else {
                log.info("CLI004", "event=debug_logging enabled=true");
            }
        } else if (!requested && log.enabled()) {
            log.info("CLI004", "event=debug_logging enabled=false");
            log.disable();
        }
    };
    sync_debug_log();
    log.info("CLI001", std::string("event=start version=") + GLO_VERSION + " generation=pending");

    // The client has one routing policy: RelayPreferred.
    // --force-direct is a developer/baseline override and deliberately avoids
    // relay control, Wintun and profile routes entirely.
    if (options.routing_policy == RoutingPolicy::DirectOnly) {
        log.info("CLI003", "event=direct_only enabled=true");
        GameDetector direct_detector(options.game_id);
        publish(ConnectionState::Monitoring, "Connected - waiting for a game");
        while (!stop_) {
            sync_debug_log();
            const auto game = direct_detector.poll();
            set_game_state(game);
            {
                std::scoped_lock lock(mutex_);
                snapshot_.active_route = ActiveRoute::Direct;
                snapshot_.direct_reason = game.gameplay_active ? DirectReason::DirectSelected : DirectReason::None;
                snapshot_.gameplay_ping_ms.reset();
                snapshot_.gameplay_loss_pct.reset();
            }
            publish(game.gameplay_active ? ConnectionState::Direct : ConnectionState::Monitoring,
                    game.gameplay_active ? "In game - Direct" : "Connected - waiting for a game");
            for (int i = 0; i < 10 && !stop_; ++i) Sleep(50);
        }
        log.info("CLI002", "client worker stopped direct_only=true");
        return;
    }

    publish(ConnectionState::Connecting, "Securing local connection...");
    log.info("CLI005", "stage=secure_identity_begin");
    secure::Client transport;
    if (!transport.begin()) {
        publish_connect_error(ClientErrorCode::RelayKeyInvalid,"GLO - Secure connection failed","Could not create a secure handshake identity.");
        return;
    }
    log.info("CLI005", "stage=secure_identity_ready");
    if (options.relay_host.empty() || options.relay_port == 0 || options.relay_public_key.empty() || options.session_grant.empty()) {
        publish_connect_error(ClientErrorCode::SessionAdmissionFailed,"GLO - Invalid config","The loaded session config is incomplete.");
        return;
    }
    publish(ConnectionState::Connecting, "Connecting to relay...");
    log.info("CLI005", "stage=session_config_ready grant_bytes=" + std::to_string(options.session_grant.size()));
    if (!transport.set_relay_key(options.relay_public_key) || !transport.set_ticket(options.session_grant)) {
        publish_connect_error(ClientErrorCode::RelayKeyInvalid,"GLO - Invalid config","The relay public key or session grant is invalid.");
        return;
    }

    WSADATA wsa{};
    const int wsa_start = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (wsa_start != 0) {
        const std::string detail = "WSAStartup failed (WSA=" + std::to_string(wsa_start) + ")";
        log.error("NET001", detail);
        publish_connect_error(
            ClientErrorCode::WinsockInitFailed,
            "GLO - Connection error",
            "GLO could not initialize Windows networking.\n\n" + detail +
                "\n\nGLO was not connected. Restart GLO or Windows, then try again.");
        return;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    const auto port = std::to_string(options.relay_port);
    const int resolve_rc = getaddrinfo(options.relay_host.c_str(), port.c_str(), &hints, &result);
    if (resolve_rc != 0 || !result) {
        const std::string detail = "getaddrinfo failed (code=" + std::to_string(resolve_rc) + ")";
        log.error("NET002", "could not resolve relay host=" + options.relay_host + " " + detail);
        WSACleanup();
        publish_connect_error(
            ClientErrorCode::RelayResolveFailed,
            "GLO - Connection error",
            "GLO could not resolve the relay server '" + options.relay_host + "'.\n\n" + detail +
                "\n\nCheck your Internet connection and relay address, then try again.");
        return;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        const int error_code = WSAGetLastError();
        const std::string detail = "socket failed (WSA=" + std::to_string(error_code) + ")";
        freeaddrinfo(result);
        log.error("NET003", detail);
        WSACleanup();
        publish_connect_error(
            ClientErrorCode::RelaySocketFailed,
            "GLO - Connection error",
            "GLO could not create its relay socket.\n\n" + detail +
                "\n\nGLO was not connected. Check Windows networking or security software, then try again.");
        return;
    }
    if (::connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        const int error_code = WSAGetLastError();
        const std::string detail = "UDP connect failed (WSA=" + std::to_string(error_code) + ")";
        freeaddrinfo(result);
        closesocket(sock);
        log.error("NET004", detail);
        WSACleanup();
        publish_connect_error(
            ClientErrorCode::RelayConnectFailed,
            "GLO - Connection error",
            "GLO could not prepare the relay connection.\n\n" + detail +
                "\n\nGLO was not connected. Check your Internet connection, firewall, and relay configuration, then try again.");
        return;
    }
    freeaddrinfo(result);

    // Timed blocking recv keeps Disconnect responsive. Routing itself never polls
    // this timeout: Wintun and relay events are delivered into RouteEventQueue.
    DWORD timeout = 100;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR) {
        const int error_code = WSAGetLastError();
        const std::string detail = "SO_RCVTIMEO failed (WSA=" + std::to_string(error_code) + ")";
        closesocket(sock);
        log.error("NET005", detail);
        WSACleanup();
        publish_connect_error(
            ClientErrorCode::RelaySocketConfigFailed,
            "GLO - Connection error",
            "GLO could not configure its relay socket.\n\n" + detail +
                "\n\nGLO was not connected. Check Windows networking or security software, then try again.");
        return;
    }
    // Relay identity and the single-use ticket were supplied by the session config
    // before the UDP socket was opened. No account/control-plane call occurs in
    // the handshake hot path.
    std::mutex send_mu;
    auto send_packet = [&](const protocol::Packet& p) { return packet_send(sock,p,send_mu,transport); };
    const auto hello_start=Clock::now();
    const auto hdeadline=hello_start+std::chrono::seconds(8);
    auto next_send=hello_start;
    bool challenged=false,welcomeed=false,confirmed=false;
    while(!stop_ && Clock::now()<hdeadline) {
        if(Clock::now()>=next_send) {
            if(!welcomeed) {
                const auto hello=challenged?transport.auth_hello():transport.hello();
                if(hello.empty()||send(sock,reinterpret_cast<const char*>(hello.data()),static_cast<int>(hello.size()),0)==SOCKET_ERROR)break;
            } else {
                protocol::Packet finish;finish.type=protocol::PacketType::Finish;finish.session_id=transport.session_id();finish.flags=protocol::kFinishFlagGLOD1;
                if(!send_packet(finish))break;
            }
            next_send=Clock::now()+std::chrono::milliseconds(250);
        }
        std::array<std::uint8_t,secure::kMaxDatagram+1> bytes{};
        const int n=recv(sock,reinterpret_cast<char*>(bytes.data()),static_cast<int>(bytes.size()),0);
        if(n<=0)continue;
        const auto b=std::span<const std::uint8_t>(bytes.data(),static_cast<std::size_t>(n));
        if(!welcomeed) {
            if(transport.retry(b)) {challenged=true;next_send=Clock::now();continue;}
            if(transport.welcome(b)){welcomeed=true;next_send=Clock::now();}
        } else {
            protocol::Packet ack;
            if(transport.open(b,ack)&&ack.type==protocol::PacketType::FinishAck&&ack.payload.empty()&&ack.flags==protocol::kFinishFlagGLOD1&&ack.flow_id==0){confirmed=true;break;}
        }
    }
    if(!confirmed) {
        closesocket(sock);WSACleanup();
        if(stop_)return;
        log.error("CRYPTO002","authenticated handshake failed; no plaintext fallback");
        publish_connect_error(ClientErrorCode::RelayHandshakeTimeout,"GLO - Secure connection failed",
            "Could not authenticate the relay within 8 seconds. Check the relay address, public key, firewall and that both client and relay support the GLO6 protocol.");
        return;
    }
    const auto sid=transport.session_id();
    std::uint64_t generation = random_u64();
    if (generation == 0) generation = 1;
    const double handshake_rtt_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - hello_start).count();
    {
        std::scoped_lock lock(mutex_);
        snapshot_.session_id = sid;
        snapshot_.phase = UiPhase::ConnectedWaitingForGame;
        snapshot_.game_id = options.game_id;
        snapshot_.game_name = std::string(game_name(options.game_id));
        snapshot_.active_route = ActiveRoute::Direct;
        snapshot_.session_remaining_seconds = transport.remaining_seconds();
    }
    log.info("RELAY003", "event=handshake_ok session=" + std::to_string(sid) +
                           " generation=" + std::to_string(generation) +
                           " rtt_ms=" + std::to_string(handshake_rtt_ms));

    publish(ConnectionState::Connecting, "Preparing network...");
    GameDetector detector(options.game_id);
    std::vector<std::uint32_t> profile_hosts;
    std::string profile_error;
    const std::string ips=options.gameplay_ipv4.empty() ? bundled_profile_hosts(game_profile(options.game_id).key) : options.gameplay_ipv4;
    if(!parse_profile_hosts(ips,profile_hosts,profile_error)||options.port_min==0||options.port_min>options.port_max){
        closesocket(sock);WSACleanup();
        publish_connect_error(ClientErrorCode::ProfileRouteInitFailed,"GLO - Invalid game profile",profile_error.empty()?"Invalid UDP profile policy":profile_error);
        return;
    }
    WintunTunnel tunnel;
    RouteEventQueue route_events;

    std::atomic<RouteControlState> route_state{RouteControlState::WaitingForGame};
    std::atomic<std::uint32_t> routed_gameplay_host{0};
    std::atomic_bool relay_unavailable_event_sent{false};

    auto tunnel_event = [&](const WintunEvent& e) {
        RouteEvent event;
        event.generation = generation;
        switch (e.type) {
            case WintunEventType::FirstForwarded:
                event.type = RouteEventType::FirstForwarded;
                if (e.flow.flow_id != 0) {
                    event.flow = RouteFlowIdentity{e.flow.route_epoch, e.flow.flow_id,
                                                   e.flow.remote_ipv4_host, e.flow.remote_port,
                                                   e.flow.local_port};
                }
                break;
            case WintunEventType::NonGameTrafficDropped:
                event.type = RouteEventType::TunnelNonGameTrafficDropped;
                event.ip_protocol = e.ip_protocol;
                if (e.flow.route_epoch != 0) {
                    event.flow = RouteFlowIdentity{e.flow.route_epoch, 0, e.flow.remote_ipv4_host, 0, 0};
                }
                break;
            case WintunEventType::StoppedUnexpectedly:
                event.type = RouteEventType::TunnelStopped;
                break;
        }
        route_events.push(std::move(event));
    };

    std::atomic<std::uint64_t> net_send_ok{0}, net_encode_fail{0}, net_socket_fail{0};
    std::atomic<int> net_last_wsa_error{0};
    std::atomic_bool verification_diagnostics{false};
    std::string tunnel_error;
    if (!tunnel.arm(options.wintun_path, sid,
                    [&](const protocol::Packet& p) { return send_packet(p); },
                    [&](std::uint32_t flow_id, std::uint64_t sequence, std::uint32_t remote_ipv4_host,
                        std::uint16_t remote_port, std::uint16_t local_port, std::span<const std::uint8_t> payload) {
                        int failure = 0, winsock = 0;
                        const bool ok = data_send(sock, sid, flow_id, sequence, remote_ipv4_host,
                                                  remote_port, local_port, payload, send_mu, &failure, &winsock);
                        if (verification_diagnostics.load(std::memory_order_relaxed)) {
                            if (ok) net_send_ok.fetch_add(1, std::memory_order_relaxed);
                            else if (failure == 1) net_encode_fail.fetch_add(1, std::memory_order_relaxed);
                            else { net_socket_fail.fetch_add(1, std::memory_order_relaxed);
                                   if (winsock) net_last_wsa_error.store(winsock, std::memory_order_relaxed); }
                        }
                        return ok;
                    },
                    tunnel_event, tunnel_error)) {
        log.error("WINTUN001", "event=init_failed reason=" + tunnel_error);
        protocol::Packet bye;
        bye.type = protocol::PacketType::Bye;
        bye.session_id = sid;
        bye.nonce = random_u64();
        send_packet(bye);
        closesocket(sock);
        WSACleanup();
        publish_connect_error(ClientErrorCode::WintunInitFailed,
                              "GLO - Wintun error",
                              "GLO could not start its privileged network path.\n\n" + tunnel_error +
                                  "\n\nCheck that the official amd64 wintun.dll is beside GLO.exe, then try again.");
        return;
    }
    log.info("WINTUN002", "event=ready routes=0");
    publish(ConnectionState::Monitoring, "Connected - waiting for a game");

    auto set_route_ui = [&](ActiveRoute route) {
        std::scoped_lock lock(mutex_);
        snapshot_.active_route = route;
        if (route == ActiveRoute::Relay) snapshot_.direct_reason = DirectReason::None;
    };
    auto set_direct_reason = [&](DirectReason reason) {
        std::scoped_lock lock(mutex_);
        snapshot_.direct_reason = reason;
    };

    // Pre-route a bounded allowlist immediately after the authenticated session starts.
    // No WFP interception, no Roblox log tailing, no kernel callout. Route /32s are
    // system-wide; applications reaching one of these same hosts may be affected.
    std::string route_error;
    if(!tunnel.set_profile_routes(profile_hosts,options.port_min,options.port_max,route_error)){
        log.error("ROUTE004","event=profile_install_failed reason="+route_error);
        tunnel.stop();closesocket(sock);WSACleanup();
        publish_connect_error(ClientErrorCode::ProfileRouteInitFailed,"GLO - Routing failed",route_error);
        return;
    }
    log.info("ROUTE007","event=profile_routes_installed count="+std::to_string(profile_hosts.size())+
             " profile="+options.profile_id+" revision="+std::to_string(options.profile_revision));
    std::thread route_thread([&] {
        ForwardedFlowSet forwarded;
        bool direct_locked=false;
        bool relay_locked=false;
        std::optional<Clock::time_point> verify_deadline;
        while(true){
            RouteEvent ev;
            bool got=verify_deadline ? route_events.wait_pop_until(ev,*verify_deadline) : route_events.wait_pop(ev);
            if(!got){
                if(verify_deadline && Clock::now()>=*verify_deadline){
                    log.warn("ROUTE016","event=fail_open reason=relay_verification_timeout",std::chrono::seconds(2));
                    direct_locked=true;tunnel.clear_routes();routed_gameplay_host=0;
                    route_state=RouteControlState::DirectLocked;set_route_ui(ActiveRoute::Direct);
                    set_direct_reason(DirectReason::RelayUnavailable);publish(ConnectionState::Direct,"In game - Direct");
                    verify_deadline.reset();
                    continue;
                }
                break;
            }
            if(ev.type==RouteEventType::Disconnect)break;
            if(ev.generation!=0 && ev.generation!=generation)continue;
            if(direct_locked)continue;
            if(ev.type==RouteEventType::FirstForwarded && ev.flow && ev.flow->valid()){
                forwarded.remember(*ev.flow);
                routed_gameplay_host.store(ev.flow->remote_ipv4_host);
                if(!relay_locked){route_state=RouteControlState::RelayVerifying;
                    verify_deadline=Clock::now()+std::chrono::milliseconds(options.force_handover_grace_ms);}
                log.info("ROUTE008","event=forwarded_flow flow_id="+std::to_string(ev.flow->flow_id));
            }else if(ev.type==RouteEventType::RelayReverseConfirmed && ev.flow && forwarded.matches(*ev.flow)){
                if(!relay_locked){relay_locked=true;route_state=RouteControlState::RelayLocked;
                    verify_deadline.reset();set_route_ui(ActiveRoute::Relay);set_direct_reason(DirectReason::None);
                    log.info("ROUTE009","event=relay_locked flow_id="+std::to_string(ev.flow->flow_id));
                    publish(ConnectionState::Relayed,"In game - Relay");}
                routed_gameplay_host.store(ev.flow->remote_ipv4_host);
            }else if(ev.type==RouteEventType::TunnelNonGameTrafficDropped){
                log.warn("WINTUN006","event=non_game_packet_dropped protocol="+std::to_string(ev.ip_protocol),std::chrono::seconds(10));
            }else if(ev.type==RouteEventType::RelayUnavailable || ev.type==RouteEventType::TunnelStopped || ev.type==RouteEventType::RelayRejected){
                direct_locked=true;tunnel.clear_routes();routed_gameplay_host=0;route_state=RouteControlState::DirectLocked;
                set_route_ui(ActiveRoute::Direct);set_direct_reason(DirectReason::RelayUnavailable);
                log.warn("ROUTE026","event=fail_open reason=relay_unavailable",std::chrono::seconds(2));
                publish(ConnectionState::Direct,"In game - Direct");verify_deadline.reset();
            }
        }
    });

    struct LossPoint {
        Clock::time_point at{};
        std::uint64_t c2r_received{};
        std::uint64_t c2r_lost{};
        std::uint64_t s2c_received{};
        std::uint64_t s2c_lost{};
    };

    auto next_detector = Clock::now();
    auto last_ping = Clock::now();
    auto last_stats = Clock::now();
    auto last_quality_summary = Clock::now();
    std::uint64_t ping_nonce = 0;
    Clock::time_point ping_sent{};
    bool ping_for_quality = false;
    int ping_misses = 0;
    GameState current_game{};
    current_game.game_id = options.game_id;
    std::uint32_t process_image_event_pid = 0;
    std::uint32_t previous_primary_pid = 0;
    bool previous_process_running = false;
    bool quality_was_active = false;
    RttWindow relay_rtt(5);
    SequenceLossTracker s2c_loss;
    SequenceLossTracker s2c_session_loss;
    std::deque<LossPoint> loss_history;
    constexpr auto kQualityPingInterval = std::chrono::seconds(2);
    constexpr auto kIdleKeepaliveInterval = std::chrono::seconds(8);
    constexpr auto kStatsInterval = std::chrono::seconds(2);
    constexpr auto kQualitySummaryInterval = std::chrono::seconds(15);
    constexpr auto kLossWindow = std::chrono::seconds(12);

    auto clear_quality_ui = [&]() {
        std::scoped_lock lock(mutex_);
        snapshot_.relay_link = {};
        snapshot_.relay_path = {};
        snapshot_.gameplay_ping_ms.reset();
        snapshot_.gameplay_loss_pct.reset();
    };
    auto publish_quality = [&]() {
        const auto cp = snapshot();
        publish(cp.state, cp.message);
    };

    while (!stop_) {
        if (transport.expired()) {
            log.info("CRYPTO003", "secure session lifetime reached; reconnect required");
            break;
        }
        sync_debug_log();
        const auto now = Clock::now();
        {
            std::scoped_lock lock(mutex_);
            snapshot_.session_remaining_seconds = transport.remaining_seconds();
        }

        if (now >= next_detector) {
            current_game = detector.poll();
            const auto rs = route_state.load(std::memory_order_acquire);
            const auto host = routed_gameplay_host.load(std::memory_order_acquire);
            const auto idle_ms = tunnel.forwarded_idle_ms(host);
            current_game.gameplay_active = current_game.process_running && profile_relay_activity(rs, host, idle_ms);
            current_game.gameplay_host_ipv4 = current_game.gameplay_active ? host : 0;
            set_game_state(current_game);
            next_detector = now + (current_game.process_running ? std::chrono::milliseconds(500)
                                                                 : std::chrono::milliseconds(1000));

            previous_process_running=current_game.process_running;
            previous_primary_pid=current_game.primary_pid;
            if (current_game.gameplay_active) {
                set_route_ui(ActiveRoute::Relay);
                publish(ConnectionState::Relayed, "In game - Relay");
            } else if (rs == RouteControlState::DirectLocked && current_game.process_running) {
                publish(ConnectionState::Direct, "Direct (gameplay relay unavailable)");
            } else {
                publish(ConnectionState::Monitoring, "Connected - waiting for gameplay");
            }
        }

        const auto rs = route_state.load(std::memory_order_acquire);
        const auto routed_host = routed_gameplay_host.load(std::memory_order_acquire);
        const auto routed_idle_ms = tunnel.forwarded_idle_ms(routed_host);
        const bool quality_active = profile_relay_activity(rs, routed_host, routed_idle_ms);
        if (quality_active != quality_was_active) {
            quality_was_active = quality_active;
            relay_rtt.reset();
            s2c_loss.reset();
            loss_history.clear();
            ping_nonce = 0;
            ping_misses = 0;
            relay_unavailable_event_sent.store(false, std::memory_order_release);
            clear_quality_ui();
            if (quality_active) {
                last_ping = now - kQualityPingInterval;
                last_stats = now - kStatsInterval;
                last_quality_summary = now - kQualitySummaryInterval;
                log.info("TELEM001", "event=tunnel_quality_started ping_hz=0.5 loss_window_s=12");
            } else {
                last_ping = now;
                last_stats = now;
                log.info("TELEM004", "event=tunnel_quality_stopped");
            }
            publish_quality();
        }

        const bool relay_control_live = rs == RouteControlState::WaitingForGame ||
                                        rs == RouteControlState::RelayVerifying ||
                                        rs == RouteControlState::RelayLocked ||
                                        rs == RouteControlState::DirectLocked;
        const auto ping_interval = quality_active ? kQualityPingInterval : kIdleKeepaliveInterval;
        if (relay_control_live && now - last_ping >= ping_interval) {
            if (ping_nonce != 0 && ++ping_misses >= 3 && !relay_unavailable_event_sent.exchange(true)) {
                RouteEvent ev;
                ev.type = RouteEventType::RelayUnavailable;
                ev.generation = generation;
                ev.detail = "heartbeat timeout";
                route_events.push(std::move(ev));
            }
            ping_nonce = random_u64();
            if (ping_nonce == 0) ping_nonce = 1;
            ping_sent = now;
            ping_for_quality = quality_active;
            protocol::Packet ping;
            ping.type = protocol::PacketType::Ping;
            ping.session_id = sid;
            ping.nonce = ping_nonce;
            send_packet(ping);
            last_ping = now;
        }
        if (quality_active && now - last_stats >= kStatsInterval) {
            protocol::Packet req;
            req.type = protocol::PacketType::StatsRequest;
            req.session_id = sid;
            req.nonce = random_u64();
            {
                const auto q = snapshot();
                protocol::QualityReport report{};
                if (q.gameplay_ping_ms && std::isfinite(*q.gameplay_ping_ms) && *q.gameplay_ping_ms > 0.0) {
                    const auto micros = std::llround(std::clamp(*q.gameplay_ping_ms, 0.0, 10000.0) * 1000.0);
                    report.relay_rtt_us = static_cast<std::uint32_t>(std::clamp<long long>(micros, 0, 0xffffffffLL));
                }
                report.s2c_seq_received = s2c_session_loss.finalized_received();
                report.s2c_seq_lost = s2c_session_loss.finalized_lost();
                const auto payload = protocol::encode_quality_report(report);
                req.payload.assign(payload.begin(), payload.end());
            }
            send_packet(req);
            last_stats = now;
        }

        RxPacket rx;
        const auto rx_kind = packet_recv(sock, rx, transport);
        if (rx_kind == RxKind::Data) {
            const auto& d = rx.data;
            if (d.direction == protocol::DataDirection::S2C && d.session_id == sid) {
                if (d.sequence != 0) s2c_session_loss.observe(d.sequence);
                if (quality_active && d.sequence != 0) s2c_loss.observe(d.sequence);
                WintunFlowIdentity injected{};
                if (tunnel.inject_reply(d.flow_id, d.payload, &injected) &&
                    route_state.load(std::memory_order_acquire) == RouteControlState::RelayVerifying) {
                    RouteEvent reverse;
                    reverse.type = RouteEventType::RelayReverseConfirmed;
                    reverse.generation = generation;
                    reverse.flow = RouteFlowIdentity{injected.route_epoch, injected.flow_id,
                                                     injected.remote_ipv4_host, injected.remote_port,
                                                     injected.local_port};
                    route_events.push(std::move(reverse));
                }
            }
        } else if (rx_kind == RxKind::Control) {
            const auto& p = rx.control;
            if (p.type == protocol::PacketType::Pong && p.session_id == sid && p.nonce == ping_nonce) {
                const bool sample = ping_for_quality && quality_active;
                const double rtt_ms = std::chrono::duration<double, std::milli>(Clock::now() - ping_sent).count();
                ping_nonce = 0;
                ping_misses = 0;
                relay_unavailable_event_sent.store(false, std::memory_order_release);
                if (sample && rtt_ms > 0.0 && rtt_ms < 10000.0) {
                    relay_rtt.add(rtt_ms);
                    if (const auto median = relay_rtt.median()) {
                        std::scoped_lock lock(mutex_);
                        snapshot_.gameplay_ping_ms = *median;
                        snapshot_.relay_link.rtt_ms = *median;
                        snapshot_.relay_link.sent = 1;
                        snapshot_.relay_link.received = 1;
                        snapshot_.relay_path = snapshot_.relay_link;
                    }
                    publish_quality();
                }
            } else if (p.type == protocol::PacketType::Error && p.session_id == sid) {
                const auto reject = protocol::relay_reject_reason(p.flags);
                if (reject != protocol::RelayRejectReason::None) {
                    RouteEvent ev;
                    ev.type = RouteEventType::RelayRejected;
                    ev.generation = generation;
                    ev.relay_reject_reason = static_cast<std::uint16_t>(reject);
                    ev.relay_flow_id = p.flow_id;
                    route_events.push(std::move(ev));
                }
            } else if (p.type == protocol::PacketType::StatsResponse && p.session_id == sid) {
                protocol::DataCounters c{};
                if (protocol::decode_data_counters(p.payload, c) && quality_active &&
                    p.payload.size() == protocol::kDataCountersSize) {
                    loss_history.push_back(LossPoint{Clock::now(), c.c2r_seq_received, c.c2r_seq_lost,
                                                     s2c_loss.finalized_received(), s2c_loss.finalized_lost()});
                    while (loss_history.size() > 2 && loss_history.back().at - loss_history[1].at > kLossWindow)
                        loss_history.pop_front();
                    if (loss_history.size() >= 2) {
                        const auto& first = loss_history.front();
                        const auto& last = loss_history.back();
                        if (last.c2r_received >= first.c2r_received && last.c2r_lost >= first.c2r_lost &&
                            last.s2c_received >= first.s2c_received && last.s2c_lost >= first.s2c_lost) {
                            const std::uint64_t received = (last.c2r_received-first.c2r_received) +
                                                           (last.s2c_received-first.s2c_received);
                            const std::uint64_t lost = (last.c2r_lost-first.c2r_lost) +
                                                       (last.s2c_lost-first.s2c_lost);
                            std::scoped_lock lock(mutex_);
                            snapshot_.gameplay_loss_pct = received + lost
                                ? std::optional<double>(100.0 * static_cast<double>(lost) / static_cast<double>(received + lost))
                                : std::nullopt;
                        }
                        publish_quality();
                    }
                }
            }
        }

        if (quality_active && now - last_quality_summary >= kQualitySummaryInterval) {
            const auto q = snapshot();
            const std::string rtt = q.gameplay_ping_ms ? std::to_string(*q.gameplay_ping_ms) : "na";
            const std::string loss = q.gameplay_loss_pct ? std::to_string(*q.gameplay_loss_pct) : "na";
            log.info("QUAL001", "game=" + std::string(game_name(q.game_id)) +
                                " route=relay rtt_ms=" + rtt + " loss_pct=" + loss);
            last_quality_summary = now;
        }
    }

    RouteEvent disconnect;
    disconnect.type = RouteEventType::Disconnect;
    disconnect.generation = generation;
    route_events.push(std::move(disconnect));
    route_events.close();
    if (route_thread.joinable()) route_thread.join();
    tunnel.stop();

    protocol::Packet bye;
    bye.type = protocol::PacketType::Bye;
    bye.session_id = sid;
    bye.nonce = random_u64();
    // Carry one final generic OSS quality snapshot on graceful shutdown so the
    // relay can finalize a session aggregate even if the last periodic stats
    // interval did not fire. Old relays safely ignore the Bye payload.
    {
        protocol::QualityReport report{};
        const auto q = snapshot();
        if (q.gameplay_ping_ms && std::isfinite(*q.gameplay_ping_ms) && *q.gameplay_ping_ms > 0.0) {
            const auto micros = std::llround(std::clamp(*q.gameplay_ping_ms, 0.0, 10000.0) * 1000.0);
            report.relay_rtt_us = static_cast<std::uint32_t>(std::clamp<long long>(micros, 0, 0xffffffffLL));
        }
        report.s2c_seq_received = s2c_session_loss.finalized_received();
        report.s2c_seq_lost = s2c_session_loss.finalized_lost();
        const auto payload = protocol::encode_quality_report(report);
        bye.payload.assign(payload.begin(), payload.end());
    }
    send_packet(bye);
    closesocket(sock);
    WSACleanup();
    log.info("CLI002", "client worker stopped");
    if (!stop_) {
        publish_connect_error(ClientErrorCode::SessionExpired, "GLO - Session expired",
                              options.timeout_message.empty() ? "The secure session has expired." : options.timeout_message);
    }
}

const char* state_name(ConnectionState s) noexcept {
    switch (s) {
        case ConnectionState::Disconnected: return "Disconnected";
        case ConnectionState::Connecting: return "Connecting";
        case ConnectionState::Monitoring: return "Monitoring";
        case ConnectionState::Direct: return "Direct";
        case ConnectionState::Relayed: return "Relayed";
        case ConnectionState::Degraded: return "Degraded";
    }
    return "Unknown";
}

const char* phase_name(UiPhase phase) noexcept {
    switch (phase) {
        case UiPhase::Disconnected: return "Disconnected";
        case UiPhase::ConnectedWaitingForGame: return "ConnectedWaitingForGame";
        case UiPhase::ConnectedStandby: return "ConnectedStandby";
        case UiPhase::Gameplay: return "Gameplay";
    }
    return "Unknown";
}

}  // namespace glo
