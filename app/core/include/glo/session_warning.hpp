#pragma once
#include <cstdint>
namespace glo {
// One notification per threshold per validated session; 0 means no notification.
struct SessionWarningTracker {
    std::uint64_t tracked_session{0};
    bool five_sent{false};
    bool one_sent{false};
    int update(std::uint64_t session, std::uint64_t remaining, bool connected) noexcept {
        if (!connected || session == 0 || remaining == 0) {
            if (!connected || session == 0) { tracked_session = 0; five_sent = one_sent = false; }
            return 0;
        }
        if (session != tracked_session) {
            tracked_session = session; five_sent = one_sent = false;
        }
        if (remaining <= 60) {
            five_sent = true; // don't emit a late five-minute toast in the final minute
            if (!one_sent) { one_sent = true; return 1; }
        } else if (remaining <= 300 && !five_sent) {
            five_sent = true; return 5;
        }
        return 0;
    }
};
}
