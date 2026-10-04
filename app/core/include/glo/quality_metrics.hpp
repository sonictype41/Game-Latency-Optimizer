#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace glo {

// Tracks a monotonically increasing UDP sequence with a bounded reorder window.
// Missing packets are counted only after they have fallen completely behind the
// window, so ordinary UDP reordering does not create false packet-loss spikes.
class SequenceLossTracker {
public:
    static constexpr std::uint64_t kReorderWindow = 64;

    void reset() noexcept {
        initialized_ = false;
        base_ = 0;
        seen_ = 0;
        finalized_received_ = 0;
        finalized_lost_ = 0;
    }

    void observe(std::uint64_t sequence) noexcept {
        if (sequence == 0) return;
        if (!initialized_) {
            initialized_ = true;
            base_ = sequence;
        }
        if (sequence < base_) return; // duplicate or too-late packet

        const std::uint64_t delta = sequence - base_;
        if (delta >= kReorderWindow) {
            const std::uint64_t shift = delta - kReorderWindow + 1;
            finalize_prefix(shift);
        }

        const std::uint64_t offset = sequence - base_;
        if (offset >= kReorderWindow) return; // defensive overflow guard
        const std::uint64_t bit = std::uint64_t{1} << offset;
        if ((seen_ & bit) != 0) return; // duplicate
        seen_ |= bit;
        finalize_contiguous();
    }

    std::uint64_t finalized_received() const noexcept { return finalized_received_; }
    std::uint64_t finalized_lost() const noexcept { return finalized_lost_; }
    bool has_data() const noexcept { return initialized_; }

private:
    void finalize_prefix(std::uint64_t count) noexcept {
        if (count == 0) return;

        if (count >= kReorderWindow) {
            const std::uint64_t present = static_cast<std::uint64_t>(std::popcount(seen_));
            finalized_received_ += present;
            finalized_lost_ += kReorderWindow - present;
            finalized_lost_ += count - kReorderWindow;
            seen_ = 0;
            base_ += count;
            return;
        }

        const std::uint64_t mask = (std::uint64_t{1} << count) - 1;
        const std::uint64_t present = static_cast<std::uint64_t>(std::popcount(seen_ & mask));
        finalized_received_ += present;
        finalized_lost_ += count - present;
        seen_ >>= count;
        base_ += count;
    }

    void finalize_contiguous() noexcept {
        while ((seen_ & 1u) != 0) {
            ++finalized_received_;
            seen_ >>= 1u;
            ++base_;
        }
    }

    bool initialized_{false};
    std::uint64_t base_{0};
    std::uint64_t seen_{0};
    std::uint64_t finalized_received_{0};
    std::uint64_t finalized_lost_{0};
};

class RttWindow {
public:
    explicit RttWindow(std::size_t capacity = 5) : capacity_(std::max<std::size_t>(1, capacity)) {}

    void reset() { samples_.clear(); }

    void add(double milliseconds) {
        if (!(milliseconds > 0.0) || milliseconds > 10000.0) return;
        samples_.push_back(milliseconds);
        while (samples_.size() > capacity_) samples_.pop_front();
    }

    std::optional<double> median() const {
        if (samples_.empty()) return std::nullopt;
        std::vector<double> copy(samples_.begin(), samples_.end());
        std::sort(copy.begin(), copy.end());
        const std::size_t n = copy.size();
        if ((n & 1u) != 0) return copy[n / 2];
        return (copy[n / 2 - 1] + copy[n / 2]) * 0.5;
    }

private:
    std::size_t capacity_;
    std::deque<double> samples_;
};

}  // namespace glo
