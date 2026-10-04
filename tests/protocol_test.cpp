#include "glo/protocol.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while(0)

int main() {
    using namespace glo::protocol;

    Packet in;
    in.type = PacketType::StatsResponse;
    in.flags = 7;
    in.session_id = 0x1122334455667788ULL;
    in.nonce = 0x8877665544332211ULL;
    in.flow_id = 0x12345678u;
    in.payload = {1,2,3,4,5,6,7,8,9};

    std::vector<std::uint8_t> encoded;
    CHECK(encode(in, encoded));
    CHECK(encoded.size() == kHeaderSize + in.payload.size());

    Packet out;
    CHECK(decode(encoded, out));
    CHECK(out.type == in.type && out.flags == in.flags);
    CHECK(out.session_id == in.session_id && out.nonce == in.nonce);
    CHECK(out.flow_id == in.flow_id && out.payload == in.payload);

    auto flow = encode_flow_endpoint(0x80743021u, 53928, 61234);
    std::uint32_t ip{}; std::uint16_t tp{}, cp{};
    CHECK(decode_flow_endpoint(flow, ip, tp, cp));
    CHECK(ip == 0x80743021u && tp == 53928 && cp == 61234);

    std::array<std::uint8_t, kDataMaxDatagram> data_wire{};
    std::size_t data_len{};
    const std::array<std::uint8_t, 4> gameplay{10,20,30,40};
    CHECK(encode_data_c2s(99, 7, 42, 0x80743021u, 53928, 61234, gameplay, data_wire, data_len));
    CHECK(data_len == kDataHeaderSize + kFlowMetaSize + gameplay.size());
    CHECK(std::equal(kDataMagic.begin(), kDataMagic.end(), data_wire.begin()));
    DataView dv{};
    CHECK(decode_data_view(std::span<const std::uint8_t>(data_wire.data(), data_len), dv));
    CHECK(dv.direction == DataDirection::C2S && dv.session_id == 99 && dv.sequence == 7 && dv.flow_id == 42);
    CHECK(dv.endpoint.size() == kFlowMetaSize && dv.payload.size() == gameplay.size());
    CHECK(std::equal(gameplay.begin(), gameplay.end(), dv.payload.begin()));
    data_wire[kDataHeaderSize + kFlowMetaSize] = 77;
    CHECK(dv.payload[0] == 77); // zero-copy view aliases datagram storage
    data_wire[30] = 0;
    CHECK(!decode_data_view(std::span<const std::uint8_t>(data_wire.data(), data_len), dv));

    DataCounters counters{11,22,33,44,55,66}, decoded{};
    auto stats = encode_data_counters(counters);
    CHECK(decode_data_counters(stats, decoded));
    CHECK(decoded.client_to_relay == 11 && decoded.relay_to_game == 22 &&
          decoded.game_to_relay == 33 && decoded.relay_to_client == 44 &&
          decoded.c2r_seq_received == 55 && decoded.c2r_seq_lost == 66);

    std::array<std::uint8_t, kLegacyDataCountersSize> legacy{};
    std::copy(stats.begin(), stats.begin() + kLegacyDataCountersSize, legacy.begin());
    DataCounters legacy_decoded{};
    CHECK(decode_data_counters(legacy, legacy_decoded));
    CHECK(legacy_decoded.client_to_relay == 11 && legacy_decoded.relay_to_client == 44);
    CHECK(legacy_decoded.c2r_seq_received == 0 && legacy_decoded.c2r_seq_lost == 0);

    QualityReport qr{};
    qr.relay_rtt_us = 52340;
    qr.s2c_seq_received = 1234;
    qr.s2c_seq_lost = 7;
    auto quality_wire = encode_quality_report(qr);
    QualityReport qr_out{};
    CHECK(decode_quality_report(quality_wire, qr_out));
    CHECK(qr_out.relay_rtt_us == 52340 && qr_out.s2c_seq_received == 1234 && qr_out.s2c_seq_lost == 7);
    quality_wire[1] = 1;
    CHECK(!decode_quality_report(quality_wire, qr_out));

    Packet stats_packet;
    stats_packet.type = PacketType::StatsResponse;
    stats_packet.session_id = 99;
    stats_packet.payload.assign(stats.begin(), stats.end());
    CHECK(encode(stats_packet, encoded));
    CHECK(decode(encoded, out));
    CHECK(out.type == PacketType::StatsResponse && out.payload.size() == kDataCountersSize);

    CHECK(relay_reject_reason(relay_reject_flags(RelayRejectReason::Capacity)) == RelayRejectReason::Capacity);
    CHECK(relay_reject_reason(relay_reject_flags(RelayRejectReason::Maintenance)) == RelayRejectReason::Maintenance);
    CHECK(std::string(relay_reject_name(RelayRejectReason::Temporary)) == "temporary");

    encoded[31] = 1;
    CHECK(!decode(encoded, out));

    // GLO2 control IDs 7-10 and 12-13 are retired/reserved in v0.12.2.
    for (auto id : {7,8,9,10,12,13}) {
        Packet reserved;
        reserved.type = static_cast<PacketType>(id);
        reserved.session_id = 1;
        CHECK(!encode(reserved, encoded));
    }

    std::cout << "GLO v0.12.2 protocol tests PASS\n";
    return 0;
}
