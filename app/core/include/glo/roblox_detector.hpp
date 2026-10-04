#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace glo {

// Resolve the exact executable image already running for a detected Roblox PID.
// This queries process metadata only; it does not read Roblox memory, inject code,
// scan install directories, or inspect network traffic.
bool query_process_image_path(std::uint32_t pid, std::wstring& image_path, std::string& error);

struct RobloxState {
    bool process_running{false};
    std::uint32_t primary_pid{0};
    std::string detail;
};

// Process-only detector. Network/gameplay state belongs to EndpointGate + Wintun.
class RobloxDetector {
public:
    RobloxDetector() = default;
    ~RobloxDetector() = default;
    RobloxDetector(const RobloxDetector&) = delete;
    RobloxDetector& operator=(const RobloxDetector&) = delete;

    RobloxState poll();

private:
    std::vector<std::uint32_t> process_ids() const;
};

}  // namespace glo
