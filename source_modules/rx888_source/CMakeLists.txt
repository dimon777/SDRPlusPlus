cmake_minimum_required(VERSION 3.13)
project(rx888_source)

file(GLOB SRC "src/*.cpp")

include(${SDRPP_MODULE_CMAKE})

find_library(SDDC_LIB NAMES sddc PATHS 
    /usr/local/lib
    /usr/lib
    /opt/homebrew/lib
)

find_path(SDDC_INCLUDE_DIR libsddc.h PATHS
    /usr/local/include
    /usr/include
    /opt/homebrew/include
)

target_include_directories(rx888_source PRIVATE ${SDDC_INCLUDE_DIR})

if (MSVC)
    target_link_libraries(rx888_source PRIVATE sddc)
else (MSVC)
    target_link_libraries(rx888_source PRIVATE ${SDDC_LIB})
endif ()
