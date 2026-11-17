// Host replacement for the parts of main/src/network_manager.cpp that
// UploadManager reads. The real manager drives Wi-Fi, SNTP and mDNS; here the
// uplink is a single switch in fake_network.hpp.
#include "air360/network_manager.hpp"

#include "fake_network.hpp"

namespace air360 {

NetworkManager::NetworkManager() {}

NetworkState NetworkManager::state() const {
    NetworkState state;
    if (fake_network::uplink_ready.load()) {
        state.mode = NetworkMode::kStation;
        state.station_connected = true;
    }
    return state;
}

UplinkStatus NetworkManager::uplinkStatus() const {
    UplinkStatus status;
    if (fake_network::uplink_ready.load()) {
        status.uplink_ready = true;
        status.active_bearer = UplinkBearer::kWifi;
    }
    return status;
}

}  // namespace air360
