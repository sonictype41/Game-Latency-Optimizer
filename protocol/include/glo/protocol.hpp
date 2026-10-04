#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace glo::protocol {

inline constexpr std::array<std::uint8_t, 4> kMagic{'G', 'L', 'O', '2'};
inline constexpr std::array<std::uint8_t, 4> kDataMagic{'G', 'L', 'O', 'D'};
inline constexpr std::uint8_t kVersion = 2;
inline constexpr std::uint8_t kDataVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 43170;
inline constexpr std::size_t kHeaderSize = 32;
inline constexpr std::size_t kMaxPayload = 1400;
inline constexpr std::size_t kMaxDatagram = kHeaderSize + kMaxPayload;
inline constexpr std::size_t kTunnelMtu = 1400;
inline constexpr std::size_t kIPv4UdpOverhead = 28;
inline constexpr std::size_t kFlowMetaSize = 8;
inline constexpr std::size_t kMaxInnerUdpPayload = kTunnelMtu - kIPv4UdpOverhead;
inline constexpr std::size_t kDataHeaderSize = 32;
inline constexpr std::uint16_t kFinishFlagGLOD1 = 0x0001;
inline constexpr std::size_t kDataMaxDatagram = kDataHeaderSize + kFlowMetaSize + kMaxInnerUdpPayload;

enum class PacketType : std::uint8_t {
    Hello = 1,
    Welcome = 2,
    Ping = 3,
    Pong = 4,
    Bye = 5,
    Error = 6,
    // Packet type IDs 7-8 are reserved. The retired target-probe/ICMP path
    // was removed in v0.5.1 and these IDs must not be reused within GLO2 v2.
    FlowClose = 11,
    StatsRequest = 14,
    StatsResponse = 15,
    Finish = 16,
    FinishAck = 17,
};


enum class RelayRejectReason : std::uint16_t {
    None = 0,
    Capacity = 1,
    Maintenance = 2,
    Temporary = 3,
};

constexpr std::uint16_t relay_reject_flags(RelayRejectReason reason) noexcept {
    return static_cast<std::uint16_t>(reason);
}

constexpr RelayRejectReason relay_reject_reason(std::uint16_t flags) noexcept {
    switch (flags) {
        case 1: return RelayRejectReason::Capacity;
        case 2: return RelayRejectReason::Maintenance;
        case 3: return RelayRejectReason::Temporary;
        default: return RelayRejectReason::None;
    }
}

const char* relay_reject_name(RelayRejectReason reason) noexcept;

inline constexpr std::size_t kLegacyDataCountersSize = 32;
inline constexpr std::size_t kDataCountersSize = 48;

struct DataCounters {
    std::uint64_t client_to_relay{};
    std::uint64_t relay_to_game{};
    std::uint64_t game_to_relay{};
    std::uint64_t relay_to_client{};
    // v0.4.1 telemetry extension. These are finalized sequence outcomes after
    // the relay's reorder window, so reordered UDP packets are not reported as
    // loss. Legacy 32-byte StatsResponse payloads decode with these set to 0.
    std::uint64_t c2r_seq_received{};
    std::uint64_t c2r_seq_lost{};
};

// Generic OSS session-quality report carried by StatsRequest. It is intentionally
// service-agnostic: the relay may expose the aggregate to any local consumer.
inline constexpr std::uint8_t kQualityReportVersion = 1;
inline constexpr std::size_t kQualityReportSize = 24;
struct QualityReport {
    std::uint32_t relay_rtt_us{}; // 0 when no gameplay RTT sample is available yet
    std::uint64_t s2c_seq_received{};
    std::uint64_t s2c_seq_lost{};
};


enum class DataDirection : std::uint8_t {
    C2S = 1,
    S2C = 2,
};

struct DataView {
    DataDirection direction{};
    std::uint64_t session_id{};
    std::uint64_t sequence{};
    std::uint32_t flow_id{};
    std::span<const std::uint8_t> endpoint{};
    std::span<const std::uint8_t> payload{};
};

bool encode_data_c2s(std::uint64_t session_id, std::uint64_t sequence, std::uint32_t flow_id,
                     std::uint32_t ipv4_be, std::uint16_t target_port, std::uint16_t client_port,
                     std::span<const std::uint8_t> payload, std::span<std::uint8_t> out,
                     std::size_t& written) noexcept;
bool decode_data_view(std::span<const std::uint8_t> bytes, DataView& out) noexcept;

struct Packet {
    PacketType type{};
    std::uint16_t flags{};
    std::uint64_t session_id{};
    std::uint64_t nonce{};
    std::uint32_t flow_id{};
    std::vector<std::uint8_t> payload;
};

bool encode(const Packet& packet, std::vector<std::uint8_t>& out) noexcept;
bool decode(std::span<const std::uint8_t> bytes, Packet& out) noexcept;
const char* type_name(PacketType type) noexcept;

std::array<std::uint8_t, 8> encode_flow_endpoint(std::uint32_t ipv4_be,
                                                 std::uint16_t target_port,
                                                 std::uint16_t client_port) noexcept;
bool decode_flow_endpoint(std::span<const std::uint8_t> payload,
                          std::uint32_t& ipv4_be,
                          std::uint16_t& target_port,
                          std::uint16_t& client_port) noexcept;

std::array<std::uint8_t, kDataCountersSize> encode_data_counters(const DataCounters& counters) noexcept;
bool decode_data_counters(std::span<const std::uint8_t> payload, DataCounters& counters) noexcept;
std::array<std::uint8_t, kQualityReportSize> encode_quality_report(const QualityReport& report) noexcept;
bool decode_quality_report(std::span<const std::uint8_t> payload, QualityReport& report) noexcept;

}  // namespace glo::protocol
