#pragma once

#include "air360/uploads/backend_config.hpp"
#include "esp_err.h"

namespace air360 {

class BackendConfigRepository {
  public:
    esp_err_t loadOrCreate(
        BackendConfigList& out_config,
        bool& loaded_from_storage,
        bool& wrote_defaults);
    esp_err_t save(const BackendConfigList& config);

  private:
    bool isValid(const BackendConfigList& config) const;
};

}  // namespace air360
