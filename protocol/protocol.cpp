#include "glo/protocol.hpp"

#include <algorithm>
#include <limits>

namespace glo::protocol {
namespace {

void put_u16_be(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
    p[1] = static_cast<std::uint8_t>(v & 0xffu);
}

void put_u32_be(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 24u) & 0xffu);
    p[1] = static_cast<std::uint8_t>((v >> 16u) & 0xffu);
    p[2] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
    p[3] = static_cast<std::uint8_t>(v & 0xffu);
}

void put_u64_be(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 7; i >= 0; --i) {
        p[i] = static_cast<std::uint8_t>(v & 0xffu);
        v >>= 8u;
    }
}

std::uint16_t get_u16_be(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8u) | p[1]);
}

std::uint32_t get_u32_be(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24u) |
           (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) |
           static_cast<std::uint32_t>(p[3]);
}

std::uint64_t get_u64_be(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8u) | p[i];
    return v;
}

bool valid_type(PacketType type) noexcept {
    switch (type) {
        case PacketType::Finish:
        case PacketType::FinishAck:
        case PacketType::Hello:
        case PacketType::Welcome:
        case PacketType::Ping:
        case PacketType::Pong:
        case PacketType::Bye:
        case PacketType::Error:
        case PacketType::FlowClose:
        case PacketType::StatsRequest:
        case PacketType::StatsResponse:
            return true;
    }
    return false;
}

}  // namespace

bool encode(const Packet& packet, std::vector<std::uint8_t>& out) noexcept {
    if (!valid_type(packet.type) || packet.payload.size() > kMaxPayload ||
        packet.payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    out.assign(kHeaderSize + packet.payload.size(), 0);
    std::copy(kMagic.begin(), kMagic.end(), out.begin());
    out[4] = kVersion;
    out[5] = static_cast<std::uint8_t>(packet.type);
    put_u16_be(out.data() + 6, packet.flags);
    put_u64_be(out.data() + 8, packet.session_id);
    put_u64_be(out.data() + 16, packet.nonce);
    put_u32_be(out.data() + 24, packet.flow_id);
    put_u16_be(out.data() + 28, static_cast<std::uint16_t>(packet.payload.size()));
    // 30..31 reserved = 0
    if (!packet.payload.empty()) {
        std::copy(packet.payload.begin(), packet.payload.end(), out.begin() + kHeaderSize);
    }
    return true;
}

bool decode(std::span<const std::uint8_t> bytes, Packet& out) noexcept {
    if (bytes.size() < kHeaderSize || bytes.size() > kMaxDatagram) return false;
    if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) return false;
    if (bytes[4] != kVersion || bytes[30] != 0 || bytes[31] != 0) return false;

    const auto type = static_cast<PacketType>(bytes[5]);
    if (!valid_type(type)) return false;
    const auto payload_len = get_u16_be(bytes.data() + 28);
    if (payload_len > kMaxPayload || bytes.size() != kHeaderSize + payload_len) return false;

    out.type = type;
    out.flags = get_u16_be(bytes.data() + 6);
    out.session_id = get_u64_be(bytes.data() + 8);
    out.nonce = get_u64_be(bytes.data() + 16);
    out.flow_id = get_u32_be(bytes.data() + 24);
    out.payload.assign(bytes.begin() + kHeaderSize, bytes.end());
    return true;
}

bool encode_data_c2s(std::uint64_t session_id, std::uint64_t sequence, std::uint32_t flow_id,
                     std::uint32_t ipv4_be, std::uint16_t target_port, std::uint16_t client_port,
                     std::span<const std::uint8_t> payload, std::span<std::uint8_t> out,
                     std::size_t& written) noexcept {
    written = 0;
    if (session_id == 0 || sequence == 0 || flow_id == 0 ||
        target_port == 0 || client_port == 0 || payload.size() > kMaxInnerUdpPayload) return false;
    const std::size_t total = kDataHeaderSize + kFlowMetaSize + payload.size();
    if (out.size() < total) return false;
    std::fill(out.begin(), out.begin() + kDataHeaderSize, 0);
    std::copy(kDataMagic.begin(), kDataMagic.end(), out.begin());
    out[4] = kDataVersion;
    out[5] = static_cast<std::uint8_t>(DataDirection::C2S);
    put_u64_be(out.data() + 8, session_id);
    put_u64_be(out.data() + 16, sequence);
    put_u32_be(out.data() + 24, flow_id);
    put_u16_be(out.data() + 28, static_cast<std::uint16_t>(payload.size()));
    out[30] = static_cast<std::uint8_t>(kFlowMetaSize);
    const auto meta = encode_flow_endpoint(ipv4_be, target_port, client_port);
    std::copy(meta.begin(), meta.end(), out.begin() + kDataHeaderSize);
    if (!payload.empty()) std::copy(payload.begin(), payload.end(), out.begin() + kDataHeaderSize + kFlowMetaSize);
    written = total;
    return true;
}

bool decode_data_view(std::span<const std::uint8_t> bytes, DataView& out) noexcept {
    if (bytes.size() < kDataHeaderSize || bytes.size() > kDataMaxDatagram) return false;
    if (!std::equal(kDataMagic.begin(), kDataMagic.end(), bytes.begin()) || bytes[4] != kDataVersion) return false;
    if (bytes[6] != 0 || bytes[7] != 0 || bytes[31] != 0) return false;
    const auto direction = static_cast<DataDirection>(bytes[5]);
    const std::size_t meta_len = bytes[30];
    if ((direction == DataDirection::C2S && meta_len != kFlowMetaSize) ||
        (direction == DataDirection::S2C && meta_len != 0)) return false;
    if (direction != DataDirection::C2S && direction != DataDirection::S2C) return false;
    const auto payload_len = static_cast<std::size_t>(get_u16_be(bytes.data() + 28));
    if (payload_len > kMaxInnerUdpPayload || bytes.size() != kDataHeaderSize + meta_len + payload_len) return false;
    const auto sid = get_u64_be(bytes.data() + 8);
    const auto seq = get_u64_be(bytes.data() + 16);
    const auto fid = get_u32_be(bytes.data() + 24);
    if (sid == 0 || seq == 0 || fid == 0) return false;
    out.direction = direction;
    out.session_id = sid;
    out.sequence = seq;
    out.flow_id = fid;
    out.endpoint = bytes.subspan(kDataHeaderSize, meta_len);
    out.payload = bytes.subspan(kDataHeaderSize + meta_len, payload_len);
    return true;
}

const char* relay_reject_name(RelayRejectReason reason) noexcept {
    switch (reason) {
        case RelayRejectReason::Capacity: return "capacity";
        case RelayRejectReason::Maintenance: return "maintenance";
        case RelayRejectReason::Temporary: return "temporary";
        case RelayRejectReason::None: return "none";
    }
    return "none";
}

const char* type_name(PacketType type) noexcept {
    switch (type) {
        case PacketType::Finish: return "FINISH";
        case PacketType::FinishAck: return "FINISH_ACK";
        case PacketType::Hello: return "HELLO";
        case PacketType::Welcome: return "WELCOME";
        case PacketType::Ping: return "PING";
        case PacketType::Pong: return "PONG";
        case PacketType::Bye: return "BYE";
        case PacketType::Error: return "ERROR";
        case PacketType::FlowClose: return "FLOW_CLOSE";
        case PacketType::StatsRequest: return "STATS_REQUEST";
        case PacketType::StatsResponse: return "STATS_RESPONSE";
    }
    return "UNKNOWN";
}

std::array<std::uint8_t, 8> encode_flow_endpoint(std::uint32_t ipv4_be,
                                                 std::uint16_t target_port,
                                                 std::uint16_t client_port) noexcept {
    std::array<std::uint8_t, 8> out{};
    // ipv4_be is already in network byte order in memory; serialize numerically as network order.
    put_u32_be(out.data(), ipv4_be);
    put_u16_be(out.data() + 4, target_port);
    put_u16_be(out.data() + 6, client_port);
    return out;
}

bool decode_flow_endpoint(std::span<const std::uint8_t> payload,
                          std::uint32_t& ipv4_be,
                          std::uint16_t& target_port,
                          std::uint16_t& client_port) noexcept {
    if (payload.size() != 8) return false;
    ipv4_be = get_u32_be(payload.data());
    target_port = get_u16_be(payload.data() + 4);
    client_port = get_u16_be(payload.data() + 6);
    return true;
}

std::array<std::uint8_t, kDataCountersSize> encode_data_counters(const DataCounters& counters) noexcept {
    std::array<std::uint8_t, kDataCountersSize> out{};
    put_u64_be(out.data() + 0, counters.client_to_relay);
    put_u64_be(out.data() + 8, counters.relay_to_game);
    put_u64_be(out.data() + 16, counters.game_to_relay);
    put_u64_be(out.data() + 24, counters.relay_to_client);
    put_u64_be(out.data() + 32, counters.c2r_seq_received);
    put_u64_be(out.data() + 40, counters.c2r_seq_lost);
    return out;
}

bool decode_data_counters(std::span<const std::uint8_t> payload, DataCounters& counters) noexcept {
    if (payload.size() != kLegacyDataCountersSize && payload.size() != kDataCountersSize) return false;
    counters = {};
    counters.client_to_relay = get_u64_be(payload.data() + 0);
    counters.relay_to_game = get_u64_be(payload.data() + 8);
    counters.game_to_relay = get_u64_be(payload.data() + 16);
    counters.relay_to_client = get_u64_be(payload.data() + 24);
    if (payload.size() == kDataCountersSize) {
        counters.c2r_seq_received = get_u64_be(payload.data() + 32);
        counters.c2r_seq_lost = get_u64_be(payload.data() + 40);
    }
    return true;
}

std::array<std::uint8_t, kQualityReportSize> encode_quality_report(const QualityReport& report) noexcept {
    std::array<std::uint8_t, kQualityReportSize> out{};
    out[0] = kQualityReportVersion;
    put_u32_be(out.data() + 4, report.relay_rtt_us);
    put_u64_be(out.data() + 8, report.s2c_seq_received);
    put_u64_be(out.data() + 16, report.s2c_seq_lost);
    return out;
}

bool decode_quality_report(std::span<const std::uint8_t> payload, QualityReport& report) noexcept {
    if (payload.size() != kQualityReportSize || payload[0] != kQualityReportVersion ||
        payload[1] != 0 || payload[2] != 0 || payload[3] != 0) return false;
    report = {};
    report.relay_rtt_us = get_u32_be(payload.data() + 4);
    report.s2c_seq_received = get_u64_be(payload.data() + 8);
    report.s2c_seq_lost = get_u64_be(payload.data() + 16);
    return true;
}

}  // namespace glo::protocol
