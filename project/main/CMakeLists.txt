# The released Aether cooperative runtime supplies the complete source list.
set(AE /aether)
file(STRINGS "${AE}/runtime/coop_runtime_sources.txt" AE_RUNTIME REGEX "^[^#]")
list(TRANSFORM AE_RUNTIME PREPEND "${AE}/")
set(SRCS "/gen/${AE_PROG}.c" ${AE_RUNTIME})
set(EMBED "")
if(AE_PROG STREQUAL "runner-cyd-usb")
    list(APPEND SRCS "runner_peer.c" "runner_switch.c" "runner_switch_esp.c" "runner_settings.c" "runner_settings_esp.c")
    set(EMBED "/scripts/services/labels.rexx")
endif()
if(AE_PROG MATCHES "^(psk-probe|runner-signed-psk|runner-cyd-wifi)$")
    list(APPEND SRCS "runner_psk.c")
endif()
# IDF expands component requirements before AE_HOST is available in its
# dependency scan. Keep the dependencies unconditional; unused code is removed
# by the linker and networking is started only by runner_http_host.
set(HOST_REQUIRES esp_timer esp_partition esp_http_server esp_eth esp_netif esp-tls bootloader_support mbedtls nvs_flash esp_driver_gpio esp_driver_i2c esp_driver_uart esp_driver_ledc esp_driver_pcnt esp_wifi esp_driver_spi esp_lcd)
if(AE_HOST STREQUAL "runner_http_host" OR AE_HOST STREQUAL "runner_usb_host")
    list(APPEND SRCS "runner_host.c")
endif()
if(AE_PROG MATCHES "^(runner-signed(-psk)?|runner-cyd-wifi|runner-cyd-usb)$")
    list(APPEND SRCS "runner_bundle.c" "runner_store.c")
endif()
if(AE_HOST)
    list(APPEND SRCS "${AE_HOST}.c")
    if(AE_HOST STREQUAL "rexx_host")
        set(EMBED "/scripts/demo.rexx" "/scripts/bench.rexx")
    elseif(AE_HOST STREQUAL "runner_host")
        list(APPEND SRCS "runner_peripherals.c" "runner_pulse.c" "runner_wifi_scan.c")
        if("$ENV{RUNNER_CYD_CONSOLE}" STREQUAL "1")
            list(APPEND SRCS "cyd_console.c" "cyd_serial.c" "cyd_touch.c")
        endif()
        set(EMBED "/scripts/toit/led.rexx" "/scripts/toit/bubble_sort.rexx"
                  "/scripts/toit/mandelbrot.rexx" "/scripts/toit/hello.rexx"
                  "/scripts/toit/service_log.rexx" "/scripts/toit/i2c_scan.rexx"
                  "/scripts/toit/uart_loopback.rexx" "/scripts/toit/random_die.rexx"
                  "/scripts/toit/pulse_counter.rexx" "/scripts/toit/deep_sleep.rexx"
                  "/scripts/toit/gpio_wake.rexx" "/scripts/toit/wifi_scan.rexx"
                  "/scripts/examples/auto_off.rexx" "/scripts/examples/timer_probe.rexx")
    endif()
endif()
idf_component_register(
    SRCS ${SRCS}
    INCLUDE_DIRS "${AE}/include" "${AE}/runtime" "${AE}/runtime/actors" "${AE}/runtime/scheduler"
                 "${AE}/runtime/utils" "${AE}/runtime/memory" "${AE}/runtime/config"
                 "${AE}/std" "${AE}/std/string" "${AE}/std/io" "${AE}/std/math"
                 "${AE}/std/net" "${AE}/std/collections" "${AE}/std/json"
                 "/rexxae/include"
    EMBED_TXTFILES ${EMBED}
    PRIV_REQUIRES ${HOST_REQUIRES})
# No threads, files, sockets or environment (as aether's own esp32 lane).
# The scheduler is sized for internal DRAM: at its defaults its static
# tables alone are about 10 MB.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    AETHER_NO_THREADING AETHER_NO_FILESYSTEM AETHER_NO_NETWORKING AETHER_NO_GETENV
    AETHER_NO_SIMD AETHER_NO_AFFINITY AETHER_NO_NUMA
    MAX_CORES=1 QUEUE_SIZE=64 SCHEDULER_DRAIN_BATCH=32 MAX_ACTORS_PER_CORE=256
    # The execution-account address filter: 2 KiB rather than 32 KiB of
    # DRAM .bss (honoured since v0.876.0).
    AETHER_ACCT_FILTER_LOG2=14)
# Same warning policy as aether's lane for the generated C and the runtime
# (-w: they are not warning-clean for xtensa gcc). Only those files: gcc's
# -w beats any -Werror, so a target-wide -w would silence the host too.
set_source_files_properties("/gen/${AE_PROG}.c" ${AE_RUNTIME} PROPERTIES COMPILE_OPTIONS "-w")
if(AE_HOST)
    set_source_files_properties("${AE_HOST}.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()
# lane.sh AE_TRACE_ALLOC=1: log large allocations (see rexx_host.c).
if(AE_HOST AND "$ENV{AE_TRACE_ALLOC}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE AE_TRACE_ALLOC)
    target_link_options(${COMPONENT_LIB} INTERFACE
        "-Wl,--wrap=malloc" "-Wl,--wrap=calloc" "-Wl,--wrap=realloc")
endif()

if(AE_HOST STREQUAL "runner_http_host" OR AE_HOST STREQUAL "runner_usb_host")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_HTTP)
    set_source_files_properties("runner_host.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

if(AE_PROG MATCHES "^(runner-signed(-psk)?|runner-cyd-wifi|runner-cyd-usb)$")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_SIGNED)
    set_source_files_properties("runner_bundle.c" "runner_store.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

if(AE_PROG MATCHES "^(runner-signed-psk|runner-cyd-wifi)$")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_PSK)
endif()

# Default to a simulated LED. A board image can name one physical output pin.
if(AE_HOST STREQUAL "runner_host" AND NOT "$ENV{RUNNER_LED_GPIO}" STREQUAL "")
    if(NOT "$ENV{RUNNER_LED_GPIO}" MATCHES "^(0|[1-9][0-9]?)$")
        message(FATAL_ERROR "RUNNER_LED_GPIO must be a decimal GPIO number")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_LED_GPIO=$ENV{RUNNER_LED_GPIO})
endif()

if(AE_HOST STREQUAL "runner_host")
    set_source_files_properties("runner_peripherals.c" "runner_pulse.c" "runner_wifi_scan.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
    if("$ENV{RUNNER_TOIT_IO}" STREQUAL "1")
        if(NOT IDF_TARGET STREQUAL "esp32")
            message(FATAL_ERROR "RUNNER_TOIT_IO uses classic ESP32 DevKit pin assignments")
        endif()
        target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_TOIT_IO RUNNER_TOIT_WIFI)
    endif()
    if(NOT "$ENV{RUNNER_TOIT_WAKE}" STREQUAL "")
        if(NOT IDF_TARGET STREQUAL "esp32" OR "$ENV{RUNNER_TOIT_IO}" STREQUAL "1")
            message(FATAL_ERROR "RUNNER_TOIT_WAKE requires classic ESP32 and its own image")
        endif()
        if("$ENV{RUNNER_TOIT_WAKE}" STREQUAL "timer")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_TOIT_WAKE=1)
        elseif("$ENV{RUNNER_TOIT_WAKE}" STREQUAL "gpio")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_TOIT_WAKE=2)
        else()
            message(FATAL_ERROR "RUNNER_TOIT_WAKE must be timer or gpio")
        endif()
    endif()
endif()

if(AE_HOST STREQUAL "runner_host" AND "$ENV{RUNNER_BOARD_SMOKE}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_BOARD_SMOKE)
endif()

if(AE_HOST STREQUAL "runner_host" AND "$ENV{RUNNER_CYD_CONSOLE}" STREQUAL "1")
    if(NOT IDF_TARGET STREQUAL "esp32" OR NOT "$ENV{RUNNER_LED_GPIO}" STREQUAL "" OR
       NOT "$ENV{RUNNER_TOIT_WAKE}" STREQUAL "" OR "$ENV{RUNNER_TOIT_IO}" STREQUAL "1")
        message(FATAL_ERROR "RUNNER_CYD_CONSOLE requires classic ESP32 and exclusive LCD pins")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_BOARD_SMOKE RUNNER_CYD_CONSOLE)
    set_source_files_properties("cyd_console.c" "cyd_serial.c" "cyd_touch.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

if(AE_PROG STREQUAL "runner-cyd-wifi")
    target_sources(${COMPONENT_LIB} PRIVATE "runner_wifi.c" "cyd_console.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_WIFI)
    set_source_files_properties("runner_wifi.c" "cyd_console.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()

if(AE_PROG STREQUAL "runner-cyd-usb")
    target_sources(${COMPONENT_LIB} PRIVATE "cyd_console.c" "cyd_touch.c" "cyd_layout.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_USB)
    if("$ENV{RUNNER_CYD_SWITCH_LED}" STREQUAL "1")
        if(NOT IDF_TARGET STREQUAL "esp32")
            message(FATAL_ERROR "RUNNER_CYD_SWITCH_LED requires the FNK0114B ESP32")
        endif()
        target_compile_definitions(${COMPONENT_LIB} PRIVATE RUNNER_CYD_SWITCH_LED)
    endif()
    set_source_files_properties("cyd_console.c" "cyd_touch.c" "cyd_layout.c" "runner_peer.c" "runner_switch.c" "runner_switch_esp.c" "runner_settings.c" "runner_settings_esp.c" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
endif()
