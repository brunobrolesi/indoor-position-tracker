cmake_minimum_required(VERSION 3.16)

# Default to A1 if not specified via -DANCHOR_UNIT=A2
if(NOT DEFINED ANCHOR_UNIT)
    set(ANCHOR_UNIT "A1")
endif()

set(ANCHOR_ENV_FILE "${CMAKE_CURRENT_LIST_DIR}/main/anchor_config.env")
set(ANCHOR_GENERATED_CONFIG_DIR "${CMAKE_BINARY_DIR}/generated_config")
set(ANCHOR_GENERATED_ENV_HEADER "${ANCHOR_GENERATED_CONFIG_DIR}/anchor_config_env.h")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(anchor_firmware)
