#include "glo/wintun_tunnel.hpp"

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace glo {
namespace {

using WINTUN_ADAPTER_HANDLE = void*;
using WINTUN_SESSION_HANDLE = void*;
using WINTUN_CREATE_ADAPTER_FUNC = WINTUN_ADAPTER_HANDLE (WINAPI*)(LPCWSTR, LPCWSTR, const GUID*);
using WINTUN_OPEN_ADAPTER_FUNC = WINTUN_ADAPTER_HANDLE (WINAPI*)(LPCWSTR);
using WINTUN_CLOSE_ADAPTER_FUNC = void (WINAPI*)(WINTUN_ADAPTER_HANDLE);
using WINTUN_GET_ADAPTER_LUID_FUNC = void (WINAPI*)(WINTUN_ADAPTER_HANDLE, NET_LUID*);
using WINTUN_START_SESSION_FUNC = WINTUN_SESSION_HANDLE (WINAPI*)(WINTUN_ADAPTER_HANDLE, DWORD);
using WINTUN_END_SESSION_FUNC = void (WINAPI*)(WINTUN_SESSION_HANDLE);
using WINTUN_GET_READ_WAIT_EVENT_FUNC = HANDLE (WINAPI*)(WINTUN_SESSION_HANDLE);
using WINTUN_RECEIVE_PACKET_FUNC = BYTE* (WINAPI*)(WINTUN_SESSION_HANDLE, DWORD*);
using WINTUN_RELEASE_RECEIVE_PACKET_FUNC = void (WINAPI*)(WINTUN_SESSION_HANDLE, const BYTE*);
using WINTUN_ALLOCATE_SEND_PACKET_FUNC = BYTE* (WINAPI*)(WINTUN_SESSION_HANDLE, DWORD);
using WINTUN_SEND_PACKET_FUNC = void (WINAPI*)(WINTUN_SESSION_HANDLE, const BYTE*);

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::wstring resolve_dll_path(const std::string& requested) {
    std::filesystem::path p = widen(requested.empty() ? "wintun.dll" : requested);
    if (p.is_absolute()) return p.wstring();
    std::array<wchar_t, 32768> exe{};
    const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (n == 0 || n >= exe.size()) return p.wstring();
    return (std::filesystem::path(std::wstring(exe.data(), n)).parent_path() / p).wstring();
}

std::string narrow_path(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string winerr(const char* what, DWORD e = GetLastError()) {
    std::ostringstream s;
    s << what << " (Win32=" << e << ')';
    return s.str();
}

std::uint16_t ip_checksum(const std::uint8_t* data, std::size_t len) {
    std::uint32_t sum = 0;
    while (len >= 2) {
        sum += (static_cast<std::uint16_t>(data[0]) << 8u) | data[1];
        data += 2;
        len -= 2;
    }
    if (len) sum += static_cast<std::uint16_t>(data[0]) << 8u;
    while (sum >> 16u) sum = (sum & 0xffffu) + (sum >> 16u);
    return static_cast<std::uint16_t>(~sum);
}

void put16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 8u);
    p[1] = static_cast<std::uint8_t>(v);
}
void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24u);
    p[1] = static_cast<std::uint8_t>(v >> 16u);
    p[2] = static_cast<std::uint8_t>(v >> 8u);
    p[3] = static_cast<std::uint8_t>(v);
}
std::uint16_t get16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8u) | p[1]);
}
std::uint32_t get32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24u) | (std::uint32_t(p[1]) << 16u) |
           (std::uint32_t(p[2]) << 8u) | p[3];
}

constexpr std::uint32_t kVirtualIpHost = (100u << 24u) | (64u << 16u) | (254u << 8u) | 2u;

bool same_routes(const std::vector<Ipv4Cidr>& lhs, const std::vector<Ipv4Cidr>& rhs) {
    return lhs == rhs;
}

std::uint64_t steady_now_ms() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

}  // namespace

struct WintunTunnel::Impl {
    HMODULE dll{};
    WINTUN_CREATE_ADAPTER_FUNC create_adapter{};
    WINTUN_OPEN_ADAPTER_FUNC open_adapter{};
    WINTUN_CLOSE_ADAPTER_FUNC close_adapter{};
    WINTUN_GET_ADAPTER_LUID_FUNC get_luid{};
    WINTUN_START_SESSION_FUNC start_session{};
    WINTUN_END_SESSION_FUNC end_session{};
    WINTUN_GET_READ_WAIT_EVENT_FUNC get_read_event{};
    WINTUN_RECEIVE_PACKET_FUNC receive_packet{};
    WINTUN_RELEASE_RECEIVE_PACKET_FUNC release_receive{};
    WINTUN_ALLOCATE_SEND_PACKET_FUNC allocate_send{};
    WINTUN_SEND_PACKET_FUNC send_packet{};

    WINTUN_ADAPTER_HANDLE adapter{};
    WINTUN_SESSION_HANDLE session{};
    NET_LUID luid{};
    MIB_UNICASTIPADDRESS_ROW address_row{};
    MIB_IPINTERFACE_ROW interface_row{};
    ULONG original_mtu{0};
    bool mtu_changed{false};
    bool address_added{false};

    std::mutex routes_mu;
    std::vector<Ipv4Cidr> routes;
    std::vector<MIB_IPFORWARD_ROW2> route_rows;
    std::uint64_t route_generation{1};
    std::atomic<std::uint16_t> game_port_min{49152},game_port_max{65535};

    std::uint64_t session_id{};
    SendControlFn control_sender;
    SendDataFn data_sender;
    EventFn event_fn;
    std::thread thread;
    std::atomic_bool stop{false};
    std::atomic<std::uint64_t> non_game_notified_generation{0};
    std::atomic<std::uint32_t> last_forwarded_host{0};
    std::atomic<std::uint64_t> last_forwarded_at_ms{0};

    // Per-route aggregate counters. The route-control thread publishes the epoch
    // after the Windows host route is verified. In-flight stale packets are ignored.
    std::atomic<std::uint64_t> diag_epoch{0};
    std::atomic_bool diag_enabled{false};
    std::atomic<std::uint64_t> d_rx_total{0}, d_rx_game_host{0}, d_rx_game_udp{0};
    std::atomic<std::uint64_t> d_forwarded{0}, d_send_failed{0}, d_bad_ip{0}, d_other_host{0};
    std::atomic<std::uint64_t> d_non_udp{0}, d_wrong_port{0}, d_stale_epoch{0};
    std::atomic<std::uint64_t> d_malformed_udp{0}, d_oversize{0};
    void count(std::uint64_t epoch, std::atomic<std::uint64_t>& metric) noexcept {
        // No counter writes outside verification or when Debug is off.
        if (diag_enabled.load(std::memory_order_relaxed) && epoch != 0 &&
            diag_epoch.load(std::memory_order_relaxed) == epoch)
            metric.fetch_add(1, std::memory_order_relaxed);
    }
    void reset_diag(std::uint64_t epoch) noexcept {
        diag_epoch.store(0, std::memory_order_release);
        d_rx_total=0; d_rx_game_host=0; d_rx_game_udp=0;
        d_forwarded=0; d_send_failed=0; d_bad_ip=0; d_other_host=0;
        d_non_udp=0; d_wrong_port=0; d_stale_epoch=0; d_malformed_udp=0; d_oversize=0;
        diag_epoch.store(epoch, std::memory_order_release);
    }
    WintunRouteDiagnostics diag() const noexcept {
        WintunRouteDiagnostics d;
        d.epoch=diag_epoch.load(std::memory_order_acquire);
        d.rx_total=d_rx_total.load(); d.rx_game_host=d_rx_game_host.load(); d.rx_game_udp=d_rx_game_udp.load();
        d.forwarded=d_forwarded.load(); d.send_failed=d_send_failed.load(); d.bad_ip=d_bad_ip.load();
        d.other_host=d_other_host.load(); d.non_udp=d_non_udp.load(); d.wrong_port=d_wrong_port.load();
        d.stale_epoch=d_stale_epoch.load(); d.malformed_udp=d_malformed_udp.load(); d.oversize=d_oversize.load();
        return d;
    }


    struct LocalFlow {
        std::uint16_t port{};
        std::uint32_t local_ip_host{};
        std::uint32_t remote_ip_host{};
        std::uint16_t remote_port{};
        std::uint64_t route_epoch{};
    };
    struct FlowTuple {
        std::uint16_t local_port{};
        std::uint32_t local_ip_host{};
        std::uint32_t remote_ip_host{};
        std::uint16_t remote_port{};
        bool operator==(const FlowTuple& o) const noexcept {
            return local_port == o.local_port && local_ip_host == o.local_ip_host &&
                   remote_ip_host == o.remote_ip_host && remote_port == o.remote_port;
        }
    };
    struct FlowTupleHash {
        std::size_t operator()(const FlowTuple& k) const noexcept {
            std::uint64_t x = (static_cast<std::uint64_t>(k.local_ip_host) << 32u) ^ k.remote_ip_host;
            x ^= (static_cast<std::uint64_t>(k.local_port) << 48u) ^
                 (static_cast<std::uint64_t>(k.remote_port) << 16u);
            x ^= x >> 33u; x *= 0xff51afd7ed558ccdULL;
            x ^= x >> 33u;
            return static_cast<std::size_t>(x);
        }
    };
    std::mutex flows_mu;
    std::unordered_map<FlowTuple, std::uint32_t, FlowTupleHash> tuple_to_flow;
    std::unordered_map<std::uint32_t, LocalFlow> flow_local;
    std::unordered_set<std::uint32_t> forward_notified_flows;
    std::uint32_t next_flow{1};
    std::uint64_t next_data_sequence{1};

    bool load(const std::string& path, std::string& error) {
        const auto wpath = resolve_dll_path(path);
        dll = LoadLibraryExW(wpath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) {
            const DWORD code = GetLastError();
            error = "Could not load wintun.dll: " + narrow_path(wpath) + " (Win32=" + std::to_string(code) + ")";
            return false;
        }
#define LOAD_API(field, name) \
    field = reinterpret_cast<decltype(field)>(GetProcAddress(dll, name)); \
    if (!field) { error = std::string("Missing Wintun API: ") + name; return false; }
        LOAD_API(create_adapter, "WintunCreateAdapter");
        LOAD_API(open_adapter, "WintunOpenAdapter");
        LOAD_API(close_adapter, "WintunCloseAdapter");
        LOAD_API(get_luid, "WintunGetAdapterLUID");
        LOAD_API(start_session, "WintunStartSession");
        LOAD_API(end_session, "WintunEndSession");
        LOAD_API(get_read_event, "WintunGetReadWaitEvent");
        LOAD_API(receive_packet, "WintunReceivePacket");
        LOAD_API(release_receive, "WintunReleaseReceivePacket");
        LOAD_API(allocate_send, "WintunAllocateSendPacket");
        LOAD_API(send_packet, "WintunSendPacket");
#undef LOAD_API
        return true;
    }

    void close_all_flows(bool reset_sequence) {
        std::vector<std::uint32_t> ids;
        {
            std::scoped_lock lock(flows_mu);
            ids.reserve(flow_local.size());
            for (const auto& [id, _] : flow_local) ids.push_back(id);
            tuple_to_flow.clear();
            flow_local.clear();
            forward_notified_flows.clear();
            if (reset_sequence) next_flow = 1;
        }

        // FLOW_CLOSE makes route flaps explicit to the relay. Flow IDs remain
        // monotonic for the lifetime of one relay session, so a stale server
        // flow cannot alias a freshly created local tuple.
        if (control_sender && session_id != 0) {
            for (const auto id : ids) {
                protocol::Packet close;
                close.type = protocol::PacketType::FlowClose;
                close.session_id = session_id;
                close.flow_id = id;
                control_sender(close);
            }
        }
    }

    void remove_routes_locked(bool reset_flow_sequence = false) {
        // Invalidate packet-thread snapshots before mutating route state. A
        // packet captured under an old /32 must never tear down or feed a newer
        // route generation.
        ++route_generation;
        for (auto it = route_rows.rbegin(); it != route_rows.rend(); ++it) {
            DeleteIpForwardEntry2(&*it);
        }
        route_rows.clear();
        routes.clear();
        last_forwarded_host.store(0, std::memory_order_release);
        last_forwarded_at_ms.store(0, std::memory_order_release);
        close_all_flows(reset_flow_sequence);
    }

    bool configure_base(std::string& error) {
        adapter = open_adapter(L"GLO");
        if (!adapter) adapter = create_adapter(L"GLO", L"GLO", nullptr);
        if (!adapter) {
            error = winerr("WintunCreateAdapter failed");
            return false;
        }
        get_luid(adapter, &luid);

        // Recover from an unclean exit. Delete only routes created as manual
        // GLO routes on this adapter, leaving Windows' connected/address routes alone.
        PMIB_IPFORWARD_TABLE2 table = nullptr;
        if (GetIpForwardTable2(AF_INET, &table) == NO_ERROR && table) {
            for (ULONG i = 0; i < table->NumEntries; ++i) {
                const auto& r = table->Table[i];
                if (r.InterfaceLuid.Value == luid.Value &&
                    r.Protocol == static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT)) {
                    DeleteIpForwardEntry2(&r);
                }
            }
            FreeMibTable(table);
        }

        InitializeIpInterfaceEntry(&interface_row);
        interface_row.Family = AF_INET;
        interface_row.InterfaceLuid = luid;
        DWORD ifrc = GetIpInterfaceEntry(&interface_row);
        if (ifrc == NO_ERROR) {
            original_mtu = interface_row.NlMtu;
            if (interface_row.NlMtu != protocol::kTunnelMtu) {
                interface_row.NlMtu = static_cast<ULONG>(protocol::kTunnelMtu);
                ifrc = SetIpInterfaceEntry(&interface_row);
                if (ifrc == NO_ERROR) mtu_changed = true;
            }
        }

        InitializeUnicastIpAddressEntry(&address_row);
        address_row.InterfaceLuid = luid;
        address_row.Address.Ipv4.sin_family = AF_INET;
        address_row.Address.Ipv4.sin_addr.S_un.S_addr = htonl(kVirtualIpHost);
        address_row.OnLinkPrefixLength = 24;
        address_row.PrefixOrigin = IpPrefixOriginManual;
        address_row.SuffixOrigin = IpSuffixOriginManual;
        address_row.DadState = IpDadStatePreferred;
        address_row.ValidLifetime = 0xffffffffu;
        address_row.PreferredLifetime = 0xffffffffu;
        DWORD rc = CreateUnicastIpAddressEntry(&address_row);
        if (rc == NO_ERROR) address_added = true;
        else if (rc != ERROR_OBJECT_ALREADY_EXISTS) {
            error = winerr("CreateUnicastIpAddressEntry failed", rc);
            return false;
        }

        session = start_session(adapter, 0x400000);
        if (!session) {
            error = winerr("WintunStartSession failed");
            return false;
        }
        return true;
    }

    bool install_routes(const std::vector<Ipv4Cidr>& requested, std::string& error) {
        std::scoped_lock lock(routes_mu);
        if (!session || !adapter) {
            error = "Wintun is not active";
            return false;
        }
        if (requested.empty() || requested.size()>32) {
            error = "Profile must contain 1 to 32 exact /32 routes";
            return false;
        }
        if (same_routes(routes, requested) && route_rows.size() == requested.size()) {
            return true;
        }

        remove_routes_locked();
        for (const auto& cidr : requested) {
            if (cidr.prefix_length != 32) {
                error = "Profile safety policy permits /32 host routes only";
                remove_routes_locked();
                return false;
            }

            MIB_IPFORWARD_ROW2 row{};
            InitializeIpForwardEntry(&row);
            row.InterfaceLuid = luid;
            row.DestinationPrefix.Prefix.Ipv4.sin_family = AF_INET;
            const auto normalized = cidr.network_host & cidr_mask(cidr.prefix_length);
            row.DestinationPrefix.Prefix.Ipv4.sin_addr.S_un.S_addr = htonl(normalized);
            row.DestinationPrefix.PrefixLength = cidr.prefix_length;
            row.NextHop.Ipv4.sin_family = AF_INET;
            row.NextHop.Ipv4.sin_addr.S_un.S_addr = 0;
            row.Metric = 1;
            row.Protocol = static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT);

            DWORD rc = CreateIpForwardEntry2(&row);
            // Never overwrite a route we do not own; cleanup must not delete another application's route.
            if (rc != NO_ERROR) {
                error = "Could not install " + cidr_text(cidr) + ": " + winerr("Create/SetIpForwardEntry2 failed", rc);
                remove_routes_locked();
                return false;
            }
            route_rows.push_back(row);
            routes.push_back(Ipv4Cidr{normalized, cidr.prefix_length});
        }
        return true;
    }

    bool route_selected_for_host(std::uint32_t ip_host, std::string& detail) const {
        NET_IFINDEX glo_index = 0;
        const DWORD irc = ConvertInterfaceLuidToIndex(&luid, &glo_index);
        if (irc != NO_ERROR || glo_index == 0) {
            detail = winerr("ConvertInterfaceLuidToIndex failed", irc);
            return false;
        }

        sockaddr_in destination{};
        destination.sin_family = AF_INET;
        destination.sin_addr.S_un.S_addr = htonl(ip_host);
        auto mutable_destination = destination;
        ULONG selected = 0;
        const DWORD rc = GetBestInterfaceEx(reinterpret_cast<sockaddr*>(&mutable_destination), &selected);
        if (rc != NO_ERROR) {
            detail = winerr("GetBestInterfaceEx failed", rc);
            return false;
        }
        if (selected != glo_index) {
            detail = "Windows selected interface index " + std::to_string(selected) +
                     " instead of GLO index " + std::to_string(glo_index);
            return false;
        }
        detail = "Windows route lookup selected GLO interface index " + std::to_string(glo_index);
        return true;
    }

    void clear_routes() {
        std::scoped_lock lock(routes_mu);
        remove_routes_locked();
    }

    struct RouteView {
        std::vector<Ipv4Cidr> routes;
        std::uint64_t generation{};
    };

    RouteView routes_snapshot() {
        std::scoped_lock lock(routes_mu);
        return RouteView{routes, route_generation};
    }

    bool route_generation_current(std::uint64_t generation) {
        std::scoped_lock lock(routes_mu);
        return generation == route_generation;
    }

    std::uint32_t flow_for_tuple(std::uint16_t local_port,
                                 std::uint32_t local_ip_host,
                                 const PreflightEndpoint& ep,
                                 std::uint64_t route_epoch) {
        std::scoped_lock lock(flows_mu);
        const FlowTuple key{local_port, local_ip_host, ep.remote_ipv4_host, ep.remote_port};
        auto it = tuple_to_flow.find(key);
        if (it != tuple_to_flow.end()) {
            flow_local[it->second] = LocalFlow{local_port, local_ip_host, ep.remote_ipv4_host, ep.remote_port, route_epoch};
            return it->second;
        }

        std::uint32_t id = next_flow++;
        while (id == 0 || flow_local.contains(id)) id = next_flow++;
        tuple_to_flow[key] = id;
        flow_local[id] = LocalFlow{local_port, local_ip_host, ep.remote_ipv4_host, ep.remote_port, route_epoch};
        return id;
    }

    void run() {
        HANDLE event = get_read_event(session);
        while (!stop.load()) {
            DWORD size = 0;
            BYTE* packet = receive_packet(session, &size);
            if (!packet) {
                const DWORD e = GetLastError();
                if (e == ERROR_NO_MORE_ITEMS) {
                    WaitForSingleObject(event, 100);
                    continue;
                }
                if (e == ERROR_HANDLE_EOF) break;
                Sleep(10);
                continue;
            }

            const auto active_route = routes_snapshot();
            const auto& active_routes = active_route.routes;
            if (!active_routes.empty()) count(active_route.generation, d_rx_total);
            if (!active_routes.empty() && size < 20) count(active_route.generation, d_bad_ip);
            if (!active_routes.empty() && size >= 20) {
                const auto* b = reinterpret_cast<const std::uint8_t*>(packet);
                const std::uint8_t ver = b[0] >> 4u;
                const std::size_t ihl = static_cast<std::size_t>(b[0] & 0x0fu) * 4u;
                if (ver == 4 && ihl >= 20 && size >= ihl) {
                    const auto src = get32(b + 12);
                    const auto dst = get32(b + 16);
                    bool captured = false;
                    for (const auto& cidr : active_routes) {
                        if (cidr_contains(cidr, dst)) {
                            captured = true;
                            break;
                        }
                    }
                    if (captured) {
                        count(active_route.generation, d_rx_game_host);
                        if (b[9] != IPPROTO_UDP || size < ihl + 8) {
                            if (b[9] != IPPROTO_UDP) count(active_route.generation, d_non_udp);
                            else count(active_route.generation, d_malformed_udp);
                            // A /32 route is protocol-agnostic, so ICMP/TCP destined for
                            // the selected game host can enter Wintun too. Those packets
                            // are not game tunnel traffic. Drop them locally, keep the
                            // already-verified gameplay route intact, and notify control
                            // at most once per route epoch for diagnostics.
                            auto seen = non_game_notified_generation.load(std::memory_order_relaxed);
                            if (seen != active_route.generation &&
                                non_game_notified_generation.compare_exchange_strong(
                                    seen, active_route.generation, std::memory_order_acq_rel) && event_fn) {
                                WintunEvent ev;
                                ev.type = WintunEventType::NonGameTrafficDropped;
                                ev.flow.route_epoch = active_route.generation;
                                ev.flow.remote_ipv4_host = dst;
                                ev.ip_protocol = b[9];
                                event_fn(ev);
                            }
                        } else {
                            const auto* u = b + ihl;
                            const auto src_port = get16(u);
                            const auto dst_port = get16(u + 2);

                            // The host route is broader than the Roblox gameplay-port
                            // profile. Do not turn unrelated UDP on the same IP into
                            // tunnel traffic. Gameplay may move among high ports on the
                            // same host, so every profile-valid port remains accepted.
                            if (!(dst_port>=game_port_min.load(std::memory_order_relaxed) && dst_port<=game_port_max.load(std::memory_order_relaxed))) {
                                count(active_route.generation, d_wrong_port);
                                auto seen = non_game_notified_generation.load(std::memory_order_relaxed);
                                if (seen != active_route.generation &&
                                    non_game_notified_generation.compare_exchange_strong(
                                        seen, active_route.generation, std::memory_order_acq_rel) && event_fn) {
                                    WintunEvent ev;
                                    ev.type = WintunEventType::NonGameTrafficDropped;
                                    ev.flow.route_epoch = active_route.generation;
                                    ev.flow.remote_ipv4_host = dst;
                                    ev.flow.remote_port = dst_port;
                                    ev.ip_protocol = IPPROTO_UDP;
                                    event_fn(ev);
                                }
                            } else {
                                // If the control plane changed routes after this packet
                                // was dequeued, drop the stale packet rather than
                                // forwarding it under a new epoch.
                                if (!route_generation_current(active_route.generation)) {
                                    count(active_route.generation, d_stale_epoch);
                                    release_receive(session, packet);
                                    continue;
                                }
                                const auto udp_len = get16(u + 4);
                                if (udp_len >= 8 && ihl + udp_len <= size) {
                                    count(active_route.generation, d_rx_game_udp);
                                    const std::size_t payload_len = udp_len - 8;
                                    if (payload_len <= protocol::kMaxInnerUdpPayload &&
                                        payload_len + protocol::kFlowMetaSize <= protocol::kMaxPayload) {
                                        const PreflightEndpoint ep{dst, dst_port, src_port, 17};
                                        auto sequence = next_data_sequence++;
                                        if (sequence == 0) sequence = next_data_sequence++;
                                        const auto flow_id = flow_for_tuple(src_port, src, ep, active_route.generation);
                                        const auto payload = std::span<const std::uint8_t>(u + 8, payload_len);
                                        if (data_sender && data_sender(flow_id, sequence, dst, dst_port, src_port, payload)) {
                                            count(active_route.generation, d_forwarded);
                                            last_forwarded_host.store(ep.remote_ipv4_host, std::memory_order_release);
                                            last_forwarded_at_ms.store(steady_now_ms(), std::memory_order_release);
                                            bool first_for_flow = false;
                                            {
                                                std::scoped_lock lock(flows_mu);
                                                first_for_flow = forward_notified_flows.insert(flow_id).second;
                                            }
                                            if (first_for_flow && event_fn) {
                                                WintunEvent ev;
                                                ev.type = WintunEventType::FirstForwarded;
                                                ev.flow = WintunFlowIdentity{active_route.generation, flow_id,
                                                                            ep.remote_ipv4_host, ep.remote_port, src_port};
                                                event_fn(ev);
                                            }
                                        } else {
                                            count(active_route.generation, d_send_failed);
                                        }
                                    } else {
                                        count(active_route.generation, d_oversize);
                                    }
                                } else {
                                    count(active_route.generation, d_malformed_udp);
                                }
                            }
                        }
                    } else {
                        count(active_route.generation, d_other_host);
                    }
                } else {
                    count(active_route.generation, d_bad_ip);
                }
            }
            release_receive(session, packet);
        }
    }

    bool inject(std::uint32_t flow_id, std::span<const std::uint8_t> payload,
                WintunFlowIdentity* injected_flow) {
        if (!session || payload.size() > protocol::kMaxInnerUdpPayload) return false;
        LocalFlow local{};
        {
            std::scoped_lock lock(flows_mu);
            const auto it = flow_local.find(flow_id);
            if (it == flow_local.end()) return false;
            local = it->second;
        }

        const std::size_t total = 20 + 8 + payload.size();
        BYTE* raw = allocate_send(session, static_cast<DWORD>(total));
        if (!raw) return false;
        auto* b = reinterpret_cast<std::uint8_t*>(raw);
        std::memset(b, 0, total);
        b[0] = 0x45;
        put16(b + 2, static_cast<std::uint16_t>(total));
        static std::atomic_uint16_t ipid{1};
        put16(b + 4, ipid.fetch_add(1));
        b[8] = 64;
        b[9] = IPPROTO_UDP;
        put32(b + 12, local.remote_ip_host);
        put32(b + 16, local.local_ip_host ? local.local_ip_host : kVirtualIpHost);
        put16(b + 10, ip_checksum(b, 20));

        auto* u = b + 20;
        put16(u, local.remote_port);
        put16(u + 2, local.port);
        put16(u + 4, static_cast<std::uint16_t>(8 + payload.size()));
        put16(u + 6, 0);
        if (!payload.empty()) std::memcpy(u + 8, payload.data(), payload.size());
        send_packet(session, raw);
        if (injected_flow) {
            *injected_flow = WintunFlowIdentity{local.route_epoch, flow_id,
                                               local.remote_ip_host, local.remote_port, local.port};
        }
        return true;
    }

    void cleanup() {
        stop = true;
        if (thread.joinable()) thread.join();
        {
            std::scoped_lock lock(routes_mu);
            remove_routes_locked(true);
        }
        if (session) {
            end_session(session);
            session = nullptr;
        }
        if (address_added) {
            DeleteUnicastIpAddressEntry(&address_row);
            address_added = false;
        }
        if (mtu_changed && original_mtu) {
            interface_row.NlMtu = original_mtu;
            SetIpInterfaceEntry(&interface_row);
            mtu_changed = false;
        }
        if (adapter) {
            close_adapter(adapter);
            adapter = nullptr;
        }
        if (dll) {
            FreeLibrary(dll);
            dll = nullptr;
        }
        control_sender = {};
        data_sender = {};
        event_fn = {};
        session_id = 0;
    }
};

WintunTunnel::WintunTunnel() : impl_(new Impl()) {}
WintunTunnel::~WintunTunnel() {
    stop();
    delete impl_;
}

bool WintunTunnel::arm(const std::string& dll_path,
                       std::uint64_t session_id,
                       SendControlFn control_sender,
                       SendDataFn data_sender,
                       EventFn event_fn,
                       std::string& error) {
    stop();
    impl_->session_id = session_id;
    impl_->control_sender = std::move(control_sender);
    impl_->data_sender = std::move(data_sender);
    impl_->event_fn = std::move(event_fn);
    impl_->stop = false;
    impl_->non_game_notified_generation = 0;
    impl_->last_forwarded_host = 0;
    impl_->last_forwarded_at_ms = 0;
    impl_->next_data_sequence = 1;
    if (!impl_->load(dll_path, error) || !impl_->configure_base(error)) {
        impl_->cleanup();
        return false;
    }
    running_ = true;
    route_active_ = false;
    impl_->thread = std::thread([this] {
        impl_->run();
        const bool unexpected = !impl_->stop.load(std::memory_order_acquire);
        running_ = false;
        route_active_ = false;
        if (unexpected && impl_->event_fn) {
            WintunEvent ev;
            ev.type = WintunEventType::StoppedUnexpectedly;
            impl_->event_fn(ev);
        }
    });
    return true;
}

WintunRouteDiagnostics WintunTunnel::route_diagnostics() const noexcept {
    return impl_ ? impl_->diag() : WintunRouteDiagnostics{};
}

void WintunTunnel::set_diagnostics_enabled(bool enabled) noexcept {
    if (!impl_) return;
    if (!enabled) impl_->diag_epoch.store(0, std::memory_order_release);
    impl_->diag_enabled.store(enabled, std::memory_order_release);
}

void WintunTunnel::clear_routes() {
    if (!impl_) return;
    impl_->clear_routes();
    route_active_ = false;
}

bool WintunTunnel::set_profile_routes(const std::vector<std::uint32_t>& hosts,
                                      std::uint16_t port_min,std::uint16_t port_max,std::string& error) {
    if (!running_ || !impl_) { error="Wintun is not active";return false; }
    if(hosts.empty()||hosts.size()>32||!port_min||port_min>port_max){error="Invalid profile route set";return false;}
    std::vector<Ipv4Cidr> exact;exact.reserve(hosts.size());
    for(auto ip:hosts) exact.push_back(host_route(ip));
    impl_->game_port_min.store(port_min);impl_->game_port_max.store(port_max);
    if(!impl_->install_routes(exact,error)){route_active_=false;return false;}
    for(auto ip:hosts){std::string verify;if(!impl_->route_selected_for_host(ip,verify)){
        impl_->clear_routes();route_active_=false;error="Windows did not select Wintun for "+ipv4_text(ip)+": "+verify;return false;
    }}
    if(impl_->diag_enabled.load())impl_->reset_diag(impl_->routes_snapshot().generation);
    route_active_=true;return true;
}

void WintunTunnel::stop() {
    if (!impl_) return;
    impl_->cleanup();
    running_ = false;
    route_active_ = false;
}

bool WintunTunnel::inject_reply(std::uint32_t flow_id,
                                std::span<const std::uint8_t> udp_payload,
                                WintunFlowIdentity* injected_flow) {
    return running_ && impl_ && impl_->inject(flow_id, udp_payload, injected_flow);
}

std::uint64_t WintunTunnel::forwarded_idle_ms(std::uint32_t remote_ipv4_host) const noexcept {
    if (!impl_ || remote_ipv4_host == 0) return std::numeric_limits<std::uint64_t>::max();
    if (impl_->last_forwarded_host.load(std::memory_order_acquire) != remote_ipv4_host)
        return std::numeric_limits<std::uint64_t>::max();
    const auto at = impl_->last_forwarded_at_ms.load(std::memory_order_acquire);
    if (at == 0) return std::numeric_limits<std::uint64_t>::max();
    const auto now = steady_now_ms();
    return now >= at ? now - at : 0;
}

}  // namespace glo
