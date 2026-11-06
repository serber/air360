#include "air360/config_transaction.hpp"
#include "nvs.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void requireLoadedPair(std::uint16_t port, std::uint16_t window) {
    air360::DeviceConfig device{};
    air360::CellularConfig cellular{};
    bool loaded = false;
    bool defaults = false;
    require(air360::ConfigRepository{}.loadOrCreate(device, loaded, defaults) == ESP_OK &&
            loaded && !defaults, "device repository loads persisted config");
    require(air360::CellularConfigRepository{}.loadOrCreate(cellular, loaded, defaults) == ESP_OK &&
            loaded && !defaults, "cellular repository loads persisted config");
    require(device.http_port == port && cellular.wifi_debug_window_s == window,
            "both boot loaders see the same saved pair");
}
}

int main() {
    air360::ConfigRepository device_repo;
    air360::CellularConfigRepository cellular_repo;
    auto device = air360::makeDefaultDeviceConfig();
    auto cellular = air360::makeDefaultCellularConfig();
    bool loaded = false;
    bool defaults = false;
    require(device_repo.loadOrCreate(device, loaded, defaults) == ESP_OK &&
            !loaded && defaults, "empty NVS creates device defaults");
    require(cellular_repo.loadOrCreate(cellular, loaded, defaults) == ESP_OK &&
            !loaded && defaults, "empty NVS creates cellular defaults");
    device.http_port = 8080;
    cellular.wifi_debug_window_s = 123;
    require(device_repo.save(device) == ESP_OK && cellular_repo.save(cellular) == ESP_OK,
            "legacy separate saves work");
    requireLoadedPair(8080, 123);
    const auto legacy_blobs = nvs_test::blobs;

    device.http_port = 8081;
    cellular.wifi_debug_window_s = 456;
    nvs_test::fail_write = true;
    require(air360::saveDeviceAndCellularConfig(device_repo, cellular_repo, device, cellular) != ESP_OK,
            "failed first paired save propagates error");
    nvs_test::fail_write = false;
    requireLoadedPair(8080, 123);
    nvs_test::writes = 0;
    require(air360::saveDeviceAndCellularConfig(device_repo, cellular_repo, device, cellular) == ESP_OK,
            "paired save succeeds");
    require(nvs_test::writes == 1, "both configurations use one atomic NVS write");
    requireLoadedPair(8081, 456);
    require(nvs_test::blobs.at("device_cfg") == legacy_blobs.at("device_cfg") &&
            nvs_test::blobs.at("cellular_cfg") == legacy_blobs.at("cellular_cfg"),
            "migration leaves legacy records intact");

    device.http_port = 8082;
    cellular.wifi_debug_window_s = 789;
    nvs_test::fail_write = true;
    require(air360::saveDeviceAndCellularConfig(device_repo, cellular_repo, device, cellular) != ESP_OK,
            "failed replacement propagates error");
    nvs_test::fail_write = false;
    requireLoadedPair(8081, 456);
    nvs_test::fail_commit = true;
    require(air360::saveDeviceAndCellularConfig(device_repo, cellular_repo, device, cellular) != ESP_OK,
            "commit failure propagates");
    nvs_test::fail_commit = false;
    requireLoadedPair(8082, 789);  // Immediate writes still leave a complete pair.

    device.http_port = 8083;
    require(device_repo.save(device) == ESP_OK, "standalone device save updates pair");
    requireLoadedPair(8083, 789);
    cellular.wifi_debug_window_s = 321;
    require(cellular_repo.save(cellular) == ESP_OK, "standalone cellular save updates pair");
    requireLoadedPair(8083, 321);
    device.magic = 0;
    require(air360::saveDeviceAndCellularConfig(device_repo, cellular_repo, device, cellular) ==
            ESP_ERR_INVALID_ARG, "invalid new record rejected before write");
    requireLoadedPair(8083, 321);

    nvs_test::fail_read = true;
    require(device_repo.loadOrCreate(device, loaded, defaults) == ESP_FAIL,
            "read errors cannot silently resurrect legacy settings");
    nvs_test::fail_read = false;
    nvs_test::blobs.at("network_cfg")[0] ^= 0xFF;
    require(device_repo.loadOrCreate(device, loaded, defaults) == ESP_ERR_INVALID_STATE,
            "corrupt pair is rejected by device loader");
    require(cellular_repo.loadOrCreate(cellular, loaded, defaults) == ESP_ERR_INVALID_STATE,
            "corrupt pair is rejected by cellular loader");
    std::cout << "config transaction tests passed\n";
}
