#pragma once

#include <string>

namespace air360 {

struct BuildInfo {
    std::string project_name;
    std::string project_version;
    std::string idf_version;
    std::string compile_date;
    std::string compile_time;
    std::string board_name;
    std::string chip_name;
    std::string chip_revision;
    std::string chip_type;
    std::string chip_features;
    std::string crystal_frequency;
    std::string chip_id;
    std::string short_chip_id;
    std::string esp_mac_id;
};

BuildInfo getBuildInfo();

}  // namespace air360
