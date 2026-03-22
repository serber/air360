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
};

BuildInfo getBuildInfo();

}  // namespace air360
