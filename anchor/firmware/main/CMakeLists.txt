if(NOT DEFINED ANCHOR_UNIT OR ANCHOR_UNIT STREQUAL "")
    set(ANCHOR_UNIT "A1")
endif()

if(NOT ANCHOR_UNIT MATCHES "^A[123]$")
    message(FATAL_ERROR "ANCHOR_UNIT must be one of A1, A2, or A3")
endif()

if(NOT DEFINED ANCHOR_ENV_FILE OR ANCHOR_ENV_FILE STREQUAL "")
    set(ANCHOR_ENV_FILE "${CMAKE_CURRENT_LIST_DIR}/anchor_config.env")
endif()

if(NOT DEFINED ANCHOR_GENERATED_CONFIG_DIR OR ANCHOR_GENERATED_CONFIG_DIR STREQUAL "")
    set(ANCHOR_GENERATED_CONFIG_DIR "${CMAKE_BINARY_DIR}/generated_config")
endif()

if(NOT DEFINED ANCHOR_GENERATED_ENV_HEADER OR ANCHOR_GENERATED_ENV_HEADER STREQUAL "")
    set(ANCHOR_GENERATED_ENV_HEADER "${ANCHOR_GENERATED_CONFIG_DIR}/anchor_config_env.h")
endif()

if(NOT EXISTS "${ANCHOR_ENV_FILE}")
    message(FATAL_ERROR
        "Missing ${ANCHOR_ENV_FILE}. Copy main/anchor_config.env.example "
        "to main/anchor_config.env and fill in local credentials."
    )
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ANCHOR_ENV_FILE}")

if(EXISTS "${ANCHOR_ENV_FILE}")
    file(MAKE_DIRECTORY "${ANCHOR_GENERATED_CONFIG_DIR}")
    file(STRINGS "${ANCHOR_ENV_FILE}" ANCHOR_ENV_LINES)

    foreach(ANCHOR_ENV_LINE IN LISTS ANCHOR_ENV_LINES)
        string(STRIP "${ANCHOR_ENV_LINE}" ANCHOR_ENV_LINE)
        if(ANCHOR_ENV_LINE STREQUAL "" OR ANCHOR_ENV_LINE MATCHES "^#")
            continue()
        endif()

        if(NOT ANCHOR_ENV_LINE MATCHES "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$")
            message(FATAL_ERROR "Invalid env line in ${ANCHOR_ENV_FILE}: ${ANCHOR_ENV_LINE}")
        endif()

        set("ANCHOR_ENV_${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
    endforeach()

    foreach(ANCHOR_ENV_REQUIRED
            ANCHOR_WIFI_SSID
            ANCHOR_WIFI_PASSWORD
            ANCHOR_MQTT_HOST
            ANCHOR_MQTT_PORT
            ANCHOR_MQTT_USER
            ANCHOR_MQTT_PASSWORD
            ANCHOR_MQTT_TLS_ENABLED)
        if(NOT DEFINED "ANCHOR_ENV_${ANCHOR_ENV_REQUIRED}")
            message(FATAL_ERROR "Missing ${ANCHOR_ENV_REQUIRED} in ${ANCHOR_ENV_FILE}")
        endif()
    endforeach()

    file(WRITE "${ANCHOR_GENERATED_ENV_HEADER}"
        "#pragma once\n"
        "\n"
        "#define ANCHOR_WIFI_SSID \"${ANCHOR_ENV_ANCHOR_WIFI_SSID}\"\n"
        "#define ANCHOR_WIFI_PASSWORD \"${ANCHOR_ENV_ANCHOR_WIFI_PASSWORD}\"\n"
        "#define ANCHOR_MQTT_HOST \"${ANCHOR_ENV_ANCHOR_MQTT_HOST}\"\n"
        "#define ANCHOR_MQTT_PORT ${ANCHOR_ENV_ANCHOR_MQTT_PORT}\n"
        "#define ANCHOR_MQTT_USER \"${ANCHOR_ENV_ANCHOR_MQTT_USER}\"\n"
        "#define ANCHOR_MQTT_PASSWORD \"${ANCHOR_ENV_ANCHOR_MQTT_PASSWORD}\"\n"
        "#define ANCHOR_MQTT_TLS_ENABLED ${ANCHOR_ENV_ANCHOR_MQTT_TLS_ENABLED}\n"
    )
endif()

idf_component_register(
    SRCS
        "app_main.c"
        "ble_scan.c"
        "led_status.c"
        "wifi_mqtt.c"
    INCLUDE_DIRS "." "${ANCHOR_GENERATED_CONFIG_DIR}"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ANCHOR_UNIT_FILE="anchor_config_${ANCHOR_UNIT}.h"
)
