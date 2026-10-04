#pragma once
#include "glo/protocol.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace glo::secure {
inline constexpr std::size_t kMaxDatagram=1472;
inline constexpr std::size_t kHelloSize=104;
inline constexpr std::size_t kAuthHelloPrefix=106;
inline constexpr std::uint64_t kMaxSequence=std::uint64_t{1}<<32;
class Client {
public:
    Client() = default;
    ~Client();
    Client(const Client&)=delete;
    Client& operator=(const Client&)=delete;
    // Create an ephemeral transport identity. v0.12 bearer grants are independent
    // of this identity until the relay redeems the grant after allocation.
    bool begin();
    bool set_relay_key(const std::string& pinned_hex);
    bool set_ticket(std::span<const std::uint8_t> ticket);
    // Compatibility helper for tests/tools that already know the relay key.
    bool start(const std::string& pinned_hex){return begin()&&set_relay_key(pinned_hex);}
    std::vector<std::uint8_t> hello() const;
    std::vector<std::uint8_t> auth_hello() const;
    bool retry(std::span<const std::uint8_t> b);
    bool welcome(std::span<const std::uint8_t> b);
    bool seal(const protocol::Packet& p,std::vector<std::uint8_t>& out);
    bool open(std::span<const std::uint8_t> b,protocol::Packet& p);
    std::uint64_t session_id() const {return ready_.load(std::memory_order_acquire)?sid_:0;}
    bool expired() const;
    std::uint32_t remaining_seconds() const;
private:
    mutable std::mutex state_mu_,tx_mu_,rx_mu_;
    std::array<std::uint8_t,32> secret_{},pub_{},random_{},pin_{},cookie_{},tx_{},rx_{};
    std::array<std::uint64_t,64> seen_{};
    std::vector<std::uint8_t> ticket_;
    std::uint64_t sid_{},next_{},high_{};
    bool started_{},pin_set_{},challenged_{};
    std::atomic<bool> ready_{false};
    std::chrono::steady_clock::time_point expires_{};
};
}
