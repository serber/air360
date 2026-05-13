#pragma once

#include "air360/data/data_layer.hpp"
#include "air360/network/network_layer.hpp"
#include "air360/ota_service.hpp"
#include "air360/platform/platform_layer.hpp"
#include "air360/status_service.hpp"
#include "air360/web_server.hpp"

namespace air360 {

class App {
  public:
    App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    void run();

  private:
    void bootInstrumentation();
    [[nodiscard]] bool bootSystem();
    [[nodiscard]] bool bootWebServer();
    void indicateReady();
    void runMaintenanceLoop();
    void runFailedBootLoop();

    PlatformLayer platform_;
    StatusService status_service_;
    NetworkLayer  network_;
    DataLayer     data_;
    OtaService    ota_service_;
    WebServer     web_server_;
};

}  // namespace air360
