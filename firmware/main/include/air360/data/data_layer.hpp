#pragma once

#include "air360/ble_advertiser.hpp"
#include "air360/sensors/sensor_config_repository.hpp"
#include "air360/sensors/sensor_manager.hpp"
#include "air360/uploads/backend_config_repository.hpp"
#include "air360/uploads/measurement_store.hpp"
#include "air360/uploads/upload_manager.hpp"

namespace air360 {

class StatusService;
class PlatformLayer;
class NetworkLayer;

// Owns everything that produces or consumes measurements: sensor pipeline,
// in-memory measurement queue, BLE advertiser broadcasting the latest
// readings, and the upload manager that ships batches to backends.
// Layered above PlatformLayer (reads device config + Air360 secrets) and
// NetworkLayer (uses NetworkManager for upload availability).
class DataLayer {
  public:
    DataLayer();
    DataLayer(const DataLayer&) = delete;
    DataLayer& operator=(const DataLayer&) = delete;
    DataLayer(DataLayer&&) = delete;
    DataLayer& operator=(DataLayer&&) = delete;

    // Boot step 5/9: load sensor config, apply to sensor manager, start the
    // BLE advertiser bound to device config + measurement store.
    void bootSensors(PlatformLayer& platform, StatusService& status_service);

    // Boot step 6/9: load backend config (uploads do not start yet).
    void bootBackends(StatusService& status_service);

    // Boot step 8/9: start the upload manager and apply backend config.
    void bootUploads(
        PlatformLayer& platform,
        NetworkLayer& network,
        StatusService& status_service);

    SensorManager&            sensorManager()       { return sensor_manager_; }
    const SensorManager&      sensorManager() const { return sensor_manager_; }
    SensorConfigRepository&   sensorConfigRepo()    { return sensor_config_repository_; }
    SensorConfigList&         sensorConfigList()    { return sensor_config_list_; }
    MeasurementStore&         measurementStore()    { return measurement_store_; }
    BackendConfigRepository&  backendConfigRepo()   { return backend_config_repository_; }
    BackendConfigList&        backendConfigList()   { return backend_config_list_; }
    UploadManager&            uploadManager()       { return upload_manager_; }
    BleAdvertiser&            bleAdvertiser()       { return ble_advertiser_; }

  private:
    SensorConfigRepository  sensor_config_repository_;
    SensorConfigList        sensor_config_list_;
    SensorManager           sensor_manager_;
    MeasurementStore        measurement_store_;
    BackendConfigRepository backend_config_repository_;
    BackendConfigList       backend_config_list_;
    UploadManager           upload_manager_;
    BleAdvertiser           ble_advertiser_;
};

}  // namespace air360
