#include "air360/app.hpp"

extern "C" void app_main(void) {
    static air360::App app;
    app.run();
}
