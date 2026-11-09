# ESP-IDF project for the ESP32-S3: one generated Aether program, optionally
# with a C host. Configured by lane.sh / in_container.sh:
#   -DAE_PROG=<name>  /gen/<name>.c, the aetherc output (exe: it supplies
#                     app_main under ESP_PLATFORM; lib: a C host does)
#   -DAE_HOST=<file>  main/<file>.c, a C host for a --emit=lib program
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(aether_s3)
