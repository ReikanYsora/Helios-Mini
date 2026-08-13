# For more information about the build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
cmake_minimum_required(VERSION 3.16)

# Our components live nested under hardware/, networking/, ui/ instead of a
# flat components/ directory (mirrors docs/SPEC.md Section 20). Each path
# below is a leaf component directory (contains its own CMakeLists.txt).
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/hardware/board_config"
    "${CMAKE_CURRENT_LIST_DIR}/hardware/power"
    "${CMAKE_CURRENT_LIST_DIR}/hardware/i2c_bus"
    "${CMAKE_CURRENT_LIST_DIR}/hardware/touch"
    "${CMAKE_CURRENT_LIST_DIR}/hardware/display"
    "${CMAKE_CURRENT_LIST_DIR}/hardware/buttons"
    "${CMAKE_CURRENT_LIST_DIR}/hardware/audio"
    "${CMAKE_CURRENT_LIST_DIR}/networking/wifi"
    "${CMAKE_CURRENT_LIST_DIR}/networking/provisioning"
    "${CMAKE_CURRENT_LIST_DIR}/networking/settings_server"
    "${CMAKE_CURRENT_LIST_DIR}/networking/http_forms"
    "${CMAKE_CURRENT_LIST_DIR}/networking/ha_discovery"
    "${CMAKE_CURRENT_LIST_DIR}/helios/ha_client"
    "${CMAKE_CURRENT_LIST_DIR}/helios/ha_ws"
    "${CMAKE_CURRENT_LIST_DIR}/helios/energy_model"
    "${CMAKE_CURRENT_LIST_DIR}/storage"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostics"
    "${CMAKE_CURRENT_LIST_DIR}/ui/animations"
    "${CMAKE_CURRENT_LIST_DIR}/ui/home"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(helios_mini)
