#include "glo/quality_metrics.hpp"

#include <cmath>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while(0)

int main() {
    using glo::SequenceLossTracker;

    SequenceLossTracker ordered;
    for (std::uint64_t i = 1; i <= 200; ++i) ordered.observe(i);
    CHECK(ordered.finalized_received() == 200);
    CHECK(ordered.finalized_lost() == 0);

    SequenceLossTracker reordered;
    reordered.observe(1);
    reordered.observe(3);
    reordered.observe(2);
    reordered.observe(4);
    CHECK(reordered.finalized_received() == 4);
    CHECK(reordered.finalized_lost() == 0);

    SequenceLossTracker loss;
    loss.observe(1);
    loss.observe(3); // sequence 2 remains pending, not loss yet
    CHECK(loss.finalized_lost() == 0);
    for (std::uint64_t i = 4; i <= 67; ++i) loss.observe(i);
    CHECK(loss.finalized_lost() == 1);
    CHECK(loss.finalized_received() == 66);

    const auto before_dup = loss.finalized_received();
    loss.observe(67);
    loss.observe(1);
    CHECK(loss.finalized_received() == before_dup);

    glo::RttWindow rtt(5);
    rtt.add(40.0);
    rtt.add(100.0);
    rtt.add(42.0);
    CHECK(rtt.median().has_value());
    CHECK(std::abs(*rtt.median() - 42.0) < 0.001);
    rtt.add(41.0);
    rtt.add(43.0);
    CHECK(std::abs(*rtt.median() - 42.0) < 0.001);
    rtt.add(39.0); // evicts 40; median of 100,42,41,43,39 = 42
    CHECK(std::abs(*rtt.median() - 42.0) < 0.001);

    std::cout << "GLO v0.5.3 quality metrics tests PASS\n";
    return 0;
}
