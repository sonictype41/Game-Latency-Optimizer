#include "glo/client_core.hpp"

#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " #x "\n"; return 1; } } while (0)

int main() {
    using namespace glo;
    CHECK(classify_ui_phase(false, false, false) == UiPhase::Disconnected);
    CHECK(classify_ui_phase(true, false, false) == UiPhase::ConnectedWaitingForGame);
    CHECK(classify_ui_phase(true, true, false) == UiPhase::ConnectedStandby);
    CHECK(classify_ui_phase(true, true, true) == UiPhase::Gameplay);
    ClientOptions options;
    CHECK(options.routing_policy == RoutingPolicy::RelayPreferred);
    CHECK(options.force_handover_grace_ms == 1800);
    CHECK(!options.dbg_log);
    ClientSnapshot snapshot;
    CHECK(snapshot.active_route == ActiveRoute::Direct);
    CHECK(snapshot.direct_reason == DirectReason::None);
    CHECK(std::string(direct_reason_name(DirectReason::CapacityFull)) == "capacity_full");
    CHECK(!snapshot.gameplay_ping_ms.has_value());
    CHECK(!snapshot.gameplay_loss_pct.has_value());
    std::cout << "GLO generic UI phase tests PASS\n";
    return 0;
}
