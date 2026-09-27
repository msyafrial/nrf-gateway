#pragma once

#include "app_config.hpp"
#include "esp_err.h"
#include <string_view>

namespace Iotaru::Core {

class ConfigStore {
public:
    static esp_err_t initNvs();
    static void initMetadata();
    static void initNvsConfiguration();
    static void loadConfig();
    static void saveConfigAuth();
    static void saveConfigSettings();
    static void resetConfigToFactory();

    // Helper getters
    static void getMqttClientId(char* outBuf, size_t maxLen);
    static void getModuleId(char* outBuf, size_t maxLen);
};

} // namespace Iotaru::Core
