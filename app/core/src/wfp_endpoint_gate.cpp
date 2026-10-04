#include "glo/endpoint_gate.hpp"

#include "glo/route_scope.hpp"

#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>

#ifndef FWPM_SESSION_FLAG_DYNAMIC
#define FWPM_SESSION_FLAG_DYNAMIC 0x00000001u
#endif
#ifndef FWPM_NET_EVENT_FLAG_IP_PROTOCOL_SET
#define FWPM_NET_EVENT_FLAG_IP_PROTOCOL_SET 0x00000001u
#define FWPM_NET_EVENT_FLAG_LOCAL_ADDR_SET 0x00000002u
#define FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET 0x00000004u
#define FWPM_NET_EVENT_FLAG_LOCAL_PORT_SET 0x00000008u
#define FWPM_NET_EVENT_FLAG_REMOTE_PORT_SET 0x00000010u
#define FWPM_NET_EVENT_FLAG_APP_ID_SET 0x00000020u
#define FWPM_NET_EVENT_FLAG_USER_ID_SET 0x00000040u
#define FWPM_NET_EVENT_FLAG_SCOPE_ID_SET 0x00000080u
#define FWPM_NET_EVENT_FLAG_IP_VERSION_SET 0x00000100u
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

namespace glo {
namespace {

constexpr GUID kLayerAleAuthConnectV4{
    0xc38d57d1, 0x05a7, 0x4c33, {0x90, 0x4f, 0x7f, 0xbc, 0xee, 0xe6, 0x0e, 0x82}};
constexpr GUID kConditionAleAppId{
    0xd78e1e87, 0x8644, 0x4ea5, {0x94, 0x37, 0xd8, 0x09, 0xec, 0xef, 0xc9, 0x71}};
constexpr GUID kConditionIpProtocol{
    0x3971ef2b, 0x623e, 0x4f9a, {0x8c, 0xb1, 0x6e, 0x79, 0xb8, 0x06, 0xb9, 0xa7}};
constexpr GUID kConditionIpRemoteAddress{
    0xb235ae9a, 0x1d64, 0x49b8, {0xa4, 0x4c, 0x5f, 0xf3, 0xd9, 0x09, 0x50, 0x45}};
// MinGW-w64's WFP headers are incomplete on some releases. Keep the
// stable Windows SDK condition GUIDs local instead of depending on the
// FWPM_CONDITION_IP_*_PORT declarations being present in fwpmu.h.
constexpr GUID kConditionIpRemotePort{
    0xc35a604d, 0xd22b, 0x4e1a, {0x91, 0xb4, 0x68, 0xf6, 0x74, 0xee, 0x67, 0x4b}};

std::string error_code(const char* what, DWORD code) {
    std::ostringstream s;
    s << what << " (Win32/WFP 0x" << std::hex << std::uppercase << code << ')';
    return s.str();
}

GUID random_guid() {
    std::random_device rd;
    const auto tick = static_cast<std::uint64_t>(GetTickCount64());
    const auto pid = static_cast<std::uint64_t>(GetCurrentProcessId());
    auto mix = [&](std::uint64_t salt) {
        std::uint64_t v = tick ^ (pid << 32u) ^ salt;
        v ^= static_cast<std::uint64_t>(rd()) << 32u;
        v ^= static_cast<std::uint64_t>(rd());
        v ^= v >> 33u; v *= 0xff51afd7ed558ccdULL;
        v ^= v >> 33u; v *= 0xc4ceb9fe1a85ec53ULL;
        v ^= v >> 33u;
        return v;
    };
    const auto a = mix(0x474c4f5746503130ULL);
    const auto b = mix(0x4556454e54524956ULL);
    GUID g{};
    g.Data1 = static_cast<unsigned long>(a >> 32u);
    g.Data2 = static_cast<unsigned short>(a >> 16u);
    g.Data3 = static_cast<unsigned short>(a);
    for (int i = 0; i < 8; ++i) g.Data4[i] = static_cast<unsigned char>(b >> (i * 8));
    g.Data3 = static_cast<unsigned short>((g.Data3 & 0x0fffu) | 0x4000u);
    g.Data4[0] = static_cast<unsigned char>((g.Data4[0] & 0x3fu) | 0x80u);
    return g;
}



}  // namespace

struct WfpEndpointGate::Impl {
    HANDLE engine{nullptr};
    HANDLE events{nullptr};
    GUID session_key{};
    GUID sublayer_key{};
    std::vector<UINT64> block_filter_ids;
    std::atomic_bool accepting{false};
    std::atomic_bool installed{false};
    std::atomic<std::uint64_t> generation{0};
    mutable std::mutex mu;
    CandidateCallback callback;

    static void CALLBACK on_net_event(void* context, const FWPM_NET_EVENT1* event) {
        auto* self = static_cast<Impl*>(context);
        if (!self || !event || !self->accepting.load(std::memory_order_acquire)) return;
        if (event->type != FWPM_NET_EVENT_TYPE_CLASSIFY_DROP || !event->classifyDrop) return;

        {
            std::scoped_lock lock(self->mu);
            if (std::find(self->block_filter_ids.begin(), self->block_filter_ids.end(),
                          event->classifyDrop->filterId) == self->block_filter_ids.end()) {
                return;
            }
        }

        const auto& h = event->header;
        constexpr UINT32 required = FWPM_NET_EVENT_FLAG_IP_PROTOCOL_SET |
                                    FWPM_NET_EVENT_FLAG_IP_VERSION_SET |
                                    FWPM_NET_EVENT_FLAG_REMOTE_ADDR_SET |
                                    FWPM_NET_EVENT_FLAG_LOCAL_PORT_SET |
                                    FWPM_NET_EVENT_FLAG_REMOTE_PORT_SET;
        if ((h.flags & required) != required || h.ipVersion != FWP_IP_VERSION_V4) return;

        const PreflightEndpoint candidate{
            h.remoteAddrV4,
            h.remotePort,
            h.localPort,
            h.ipProtocol,
        };
        if (!preflight_endpoint_allowed(candidate)) return;

        CandidateCallback cb;
        {
            std::scoped_lock lock(self->mu);
            cb = self->callback;
        }
        if (cb && self->accepting.load(std::memory_order_acquire)) {
            cb(self->generation.load(std::memory_order_acquire), candidate);
        }
    }
};

WfpEndpointGate::WfpEndpointGate() : impl_(std::make_unique<Impl>()) {}
WfpEndpointGate::~WfpEndpointGate() { disarm(); }

bool WfpEndpointGate::arm_initial(const std::wstring& image_path,
                                               std::uint64_t generation,
                                               CandidateCallback callback,
                                               std::string& error) {
    return arm_impl(image_path, generation, 0, std::move(callback), error);
}

bool WfpEndpointGate::arm_handover(const std::wstring& image_path,
                                                              std::uint64_t generation,
                                                              std::uint32_t routed_ipv4_host,
                                                              CandidateCallback callback,
                                                              std::string& error) {
    if (routed_ipv4_host == 0) {
        error = "WFP handover guard requires the currently routed IPv4 host";
        return false;
    }
    return arm_impl(image_path, generation, routed_ipv4_host, std::move(callback), error);
}

bool WfpEndpointGate::arm_impl(const std::wstring& image_path,
                                    std::uint64_t generation,
                                    std::uint32_t excluded_remote_ipv4_host,
                                    CandidateCallback callback,
                                    std::string& error) {
    disarm();
    error.clear();
    if (image_path.empty()) {
        error = "WFP endpoint gate requires a Roblox executable path";
        return false;
    }
    if (!callback) {
        error = "WFP endpoint gate requires an endpoint callback";
        return false;
    }

    impl_->session_key = random_guid();
    impl_->sublayer_key = random_guid();
    impl_->generation.store(generation, std::memory_order_release);
    {
        std::scoped_lock lock(impl_->mu);
        impl_->callback = std::move(callback);
        impl_->block_filter_ids.clear();
    }

    FWPM_SESSION0 session{};
    session.sessionKey = impl_->session_key;
    session.displayData.name = const_cast<wchar_t*>(L"GLO v0.11 WFP endpoint gate");
    session.displayData.description = const_cast<wchar_t*>(L"Dynamic Roblox first-flow / handover guard; removed with GLO");
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;

    DWORD rc = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &impl_->engine);
    if (rc != ERROR_SUCCESS) {
        error = error_code("FwpmEngineOpen0 failed", rc);
        impl_->engine = nullptr;
        return false;
    }

    FWP_VALUE0* collect = nullptr;
    rc = FwpmEngineGetOption0(impl_->engine, FWPM_ENGINE_COLLECT_NET_EVENTS, &collect);
    const bool events_enabled = rc == ERROR_SUCCESS && collect && collect->type == FWP_UINT32 && collect->uint32 != 0;
    if (collect) FwpmFreeMemory0(reinterpret_cast<void**>(&collect));
    if (rc != ERROR_SUCCESS || !events_enabled) {
        error = rc != ERROR_SUCCESS
            ? error_code("WFP net-event status query failed", rc)
            : "WFP net-event collection is disabled; refusing to install a blind endpoint gate";
        disarm();
        return false;
    }

    FWPM_NET_EVENT_SUBSCRIPTION0 subscription{};
    subscription.sessionKey = impl_->session_key;
    rc = FwpmNetEventSubscribe0(impl_->engine, &subscription, &Impl::on_net_event, impl_.get(), &impl_->events);
    if (rc != ERROR_SUCCESS) {
        error = error_code("FwpmNetEventSubscribe0 failed", rc);
        disarm();
        return false;
    }

    FWP_BYTE_BLOB* app_id = nullptr;
    rc = FwpmGetAppIdFromFileName0(image_path.c_str(), &app_id);
    if (rc != ERROR_SUCCESS || !app_id || app_id->size == 0 || !app_id->data) {
        error = rc != ERROR_SUCCESS
            ? error_code("FwpmGetAppIdFromFileName0 failed", rc)
            : "FwpmGetAppIdFromFileName0 returned no application ID";
        if (app_id) FwpmFreeMemory0(reinterpret_cast<void**>(&app_id));
        disarm();
        return false;
    }
    bool txn = false;
    rc = FwpmTransactionBegin0(impl_->engine, 0);
    if (rc == ERROR_SUCCESS) txn = true;
    if (rc != ERROR_SUCCESS) {
        error = error_code("FwpmTransactionBegin0 failed", rc);
        FwpmFreeMemory0(reinterpret_cast<void**>(&app_id));
        disarm();
        return false;
    }

    FWPM_SUBLAYER0 sublayer{};
    sublayer.subLayerKey = impl_->sublayer_key;
    sublayer.displayData.name = const_cast<wchar_t*>(L"GLO v0.11 endpoint-gate sublayer");
    sublayer.displayData.description = const_cast<wchar_t*>(L"Temporary Roblox gameplay UDP gate");
    sublayer.weight = 0xF000;
    rc = FwpmSubLayerAdd0(impl_->engine, &sublayer, nullptr);
    if (rc != ERROR_SUCCESS) error = error_code("FwpmSubLayerAdd0 failed", rc);

    std::vector<Ipv4Cidr> block_ranges;
    for (const auto& cidr : roblox_game_allowlist()) {
        const auto pieces = excluded_remote_ipv4_host == 0
            ? std::vector<Ipv4Cidr>{cidr}
            : cidr_excluding_host(cidr, excluded_remote_ipv4_host);
        block_ranges.insert(block_ranges.end(), pieces.begin(), pieces.end());
    }

    for (const auto& cidr : block_ranges) {
        if (rc != ERROR_SUCCESS) break;

        FWP_V4_ADDR_AND_MASK remote{};
        remote.mask = cidr_mask(cidr.prefix_length);
        remote.addr = cidr.network_host & remote.mask;

        FWPM_FILTER_CONDITION0 conditions[4]{};
        conditions[0].fieldKey = kConditionAleAppId;
        conditions[0].matchType = FWP_MATCH_EQUAL;
        conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
        conditions[0].conditionValue.byteBlob = app_id;

        conditions[1].fieldKey = kConditionIpProtocol;
        conditions[1].matchType = FWP_MATCH_EQUAL;
        conditions[1].conditionValue.type = FWP_UINT8;
        conditions[1].conditionValue.uint8 = IPPROTO_UDP;

        conditions[2].fieldKey = kConditionIpRemoteAddress;
        conditions[2].matchType = FWP_MATCH_EQUAL;
        conditions[2].conditionValue.type = FWP_V4_ADDR_MASK;
        conditions[2].conditionValue.v4AddrMask = &remote;

        // Remote ports are UINT16, so >= 49152 is exactly the documented
        // gameplay range 49152-65535. Using one sortable comparison avoids an
        // extra FWP_RANGE0 dependency on older MinGW-w64 headers.
        conditions[3].fieldKey = kConditionIpRemotePort;
        conditions[3].matchType = FWP_MATCH_GREATER_OR_EQUAL;
        conditions[3].conditionValue.type = FWP_UINT16;
        conditions[3].conditionValue.uint16 = kRobloxGameplayUdpPortMin;

        FWPM_FILTER0 filter{};
        filter.displayData.name = const_cast<wchar_t*>(L"GLO Roblox gameplay UDP gate");
        filter.displayData.description = const_cast<wchar_t*>(L"Hold Roblox gameplay first flow until an exact Wintun /32 is ready");
        filter.layerKey = kLayerAleAuthConnectV4;
        filter.subLayerKey = impl_->sublayer_key;
        UINT64 block_weight = 0x1000;
        filter.weight.type = FWP_UINT64;
        filter.weight.uint64 = &block_weight;
        filter.numFilterConditions = 4;
        filter.filterCondition = conditions;
        filter.action.type = FWP_ACTION_BLOCK;

        UINT64 filter_id = 0;
        rc = FwpmFilterAdd0(impl_->engine, &filter, nullptr, &filter_id);
        if (rc == ERROR_SUCCESS) {
            std::scoped_lock lock(impl_->mu);
            impl_->block_filter_ids.push_back(filter_id);
        } else {
            error = error_code("FwpmFilterAdd0 endpoint gate failed", rc);
        }
    }

    FwpmFreeMemory0(reinterpret_cast<void**>(&app_id));

    if (rc == ERROR_SUCCESS) {
        impl_->accepting.store(true, std::memory_order_release);
        rc = FwpmTransactionCommit0(impl_->engine);
        if (rc == ERROR_SUCCESS) txn = false;
        else error = error_code("FwpmTransactionCommit0 failed", rc);
    }
    if (txn) FwpmTransactionAbort0(impl_->engine);

    if (rc != ERROR_SUCCESS) {
        disarm();
        return false;
    }

    impl_->installed.store(true, std::memory_order_release);
    return true;
}

void WfpEndpointGate::disarm() noexcept {
    if (!impl_) return;
    impl_->accepting.store(false, std::memory_order_release);
    impl_->installed.store(false, std::memory_order_release);

    if (impl_->events && impl_->engine) {
        FwpmNetEventUnsubscribe0(impl_->engine, impl_->events);
        impl_->events = nullptr;
    }
    if (impl_->engine) {
        FwpmEngineClose0(impl_->engine);
        impl_->engine = nullptr;
    }

    impl_->generation.store(0, std::memory_order_release);
    std::scoped_lock lock(impl_->mu);
    impl_->block_filter_ids.clear();
    impl_->callback = {};
}

bool WfpEndpointGate::armed() const noexcept {
    return impl_ && impl_->installed.load(std::memory_order_acquire);
}


std::unique_ptr<EndpointGate> make_endpoint_gate() {
    return std::make_unique<WfpEndpointGate>();
}

}  // namespace glo
