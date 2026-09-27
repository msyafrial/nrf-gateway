FILE(GLOB_RECURSE app_sources 
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp 
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "${CMAKE_SOURCE_DIR}/include" "bsp" "core" "utils" "network" "network/drivers" "web"
    REQUIRES esp_http_server app_update esp_wifi esp_netif esp_eth nvs_flash driver esp_timer spi_flash
)
