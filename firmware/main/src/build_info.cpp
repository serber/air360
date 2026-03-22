#include "air360/build_info.hpp"

#include "esp_app_desc.h"
#include "esp_idf_version.h"

namespace air360 {

BuildInfo getBuildInfo() {
    const esp_app_desc_t* app = esp_app_get_description();
    BuildInfo build_info;
    build_info.project_name = app->project_name;
    build_info.project_version = app->version;
    build_info.idf_version = esp_get_idf_version();
    build_info.compile_date = app->date;
    build_info.compile_time = app->time;
    build_info.board_name = CONFIG_AIR360_BOARD_NAME;
    return build_info;
}

}  // namespace air360
