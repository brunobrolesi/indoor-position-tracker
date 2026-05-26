cmake_minimum_required(VERSION 3.16)

# Default to A1 if not specified via -DANCHOR_UNIT=A2
if(NOT DEFINED ANCHOR_UNIT)
    set(ANCHOR_UNIT "A1")
endif()
add_compile_definitions(ANCHOR_UNIT_FILE="anchor_config_${ANCHOR_UNIT}.h")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(anchor_firmware)
