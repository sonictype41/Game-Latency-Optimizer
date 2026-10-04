#include "glo/route_policy.hpp"
#include <cassert>
#include <iostream>

using glo::PathChoice;
using glo::PathStats;

int main() {
    PathStats direct{40.0, 1.0, 0.0, 5, 5};
    PathStats relay{55.0, 1.0, 0.0, 5, 5};
    assert(glo::recommend_path(direct, relay, 8.0) == PathChoice::Direct);

    direct = {82.0, 8.0, 1.0, 5, 5};
    relay = {55.0, 2.0, 0.0, 5, 5};
    assert(glo::recommend_path(direct, relay, 8.0) == PathChoice::Relay);

    // Tiny differences are informational noise, not a reason to prefer relay.
    direct = {50.0, 1.0, 0.0, 5, 5};
    relay = {47.0, 1.0, 0.0, 5, 5};
    assert(glo::recommend_path(direct, relay, 8.0) == PathChoice::Direct);

    // Loss is expensive even when raw RTT is slightly lower.
    direct = {45.0, 2.0, 3.0, 10, 7};
    relay = {50.0, 2.0, 0.0, 10, 10};
    assert(glo::path_score(relay) < glo::path_score(direct));

    PathStats invalid{};
    assert(glo::recommend_path(invalid, relay, 8.0) == PathChoice::Relay);
    std::cout << "GLO v0.3.15 route policy tests PASS\n";
}
