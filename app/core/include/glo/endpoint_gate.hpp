#pragma once

#include "glo/preflight_policy.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace glo {

// Minimal endpoint gate contract. The routing core only asks the gate to hold a
// new eligible gameplay endpoint until an exact /32 route has been installed.
// It has no gameplay-state, quality, session, account, or telemetry role.
class EndpointGate {
public:
    using CandidateCallback = std::function<void(std::uint64_t, const PreflightEndpoint&)>;
    virtual ~EndpointGate() = default;
    virtual bool arm_initial(const std::wstring& image_path,
                             std::uint64_t generation,
                             CandidateCallback callback,
                             std::string& error) = 0;
    virtual bool arm_handover(const std::wstring& image_path,
                              std::uint64_t generation,
                              std::uint32_t routed_ipv4_host,
                              CandidateCallback callback,
                              std::string& error) = 0;
    virtual void disarm() noexcept = 0;
    [[nodiscard]] virtual bool armed() const noexcept = 0;
};

// Returns the platform endpoint gate used by the networking worker.  The core
// depends only on EndpointGate; the concrete Windows implementation remains isolated
// in wfp_endpoint_gate.cpp.
std::unique_ptr<EndpointGate> make_endpoint_gate();

// Windows implementation: a dynamic, user-mode WFP ALE_AUTH_CONNECT_V4 gate
// scoped to the selected game executable + UDP + approved game CIDRs + gameplay
// port range. The current routed host is excluded during handover. No custom
// callout driver or persistent firewall object is installed.
class WfpEndpointGate final : public EndpointGate {
public:
    WfpEndpointGate();
    ~WfpEndpointGate() override;
    WfpEndpointGate(const WfpEndpointGate&) = delete;
    WfpEndpointGate& operator=(const WfpEndpointGate&) = delete;

    bool arm_initial(const std::wstring& image_path,
                     std::uint64_t generation,
                     CandidateCallback callback,
                     std::string& error) override;
    bool arm_handover(const std::wstring& image_path,
                      std::uint64_t generation,
                      std::uint32_t routed_ipv4_host,
                      CandidateCallback callback,
                      std::string& error) override;
    void disarm() noexcept override;
    [[nodiscard]] bool armed() const noexcept override;

private:
    bool arm_impl(const std::wstring& image_path,
                  std::uint64_t generation,
                  std::uint32_t excluded_remote_ipv4_host,
                  CandidateCallback callback,
                  std::string& error);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace glo
