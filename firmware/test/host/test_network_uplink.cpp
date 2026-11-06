#include "air360/network_manager.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    air360::NetworkState state;
    const auto require = [](bool condition) {
        if (!condition) {
            std::cerr << "Unexpected bearer readiness for time synchronization\n";
            std::exit(1);
        }
    };
    require(!state.hasConnectedUplink());
    state.cellular_ip = "10.0.0.1";
    require(!state.time_synchronized && state.hasConnectedUplink());
    state.mode = air360::NetworkMode::kSetupAp;
    require(state.hasConnectedUplink());
    state.cellular_ip.clear();
    require(!state.hasConnectedUplink());
    state.mode = air360::NetworkMode::kStation;
    require(!state.hasConnectedUplink());
    state.station_connected = true;
    require(state.hasConnectedUplink());
    state.station_connected = false;
    require(!state.hasConnectedUplink());
    std::cout << "network uplink tests passed\n";
}
