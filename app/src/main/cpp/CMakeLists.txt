cmake_minimum_required(VERSION 3.22.1)
project(rogue_recovery LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

find_package(shadowhook REQUIRED CONFIG)

add_library(rogue_recovery SHARED
        template_native.cpp
        native_utils.cpp
)

if(NOT DEFINED TEMPLATE_VERBOSE_LOGS)
    set(TEMPLATE_VERBOSE_LOGS 0)
endif()

target_compile_definitions(rogue_recovery PRIVATE
        TEMPLATE_VERBOSE_LOGS=${TEMPLATE_VERBOSE_LOGS}
)

target_compile_options(rogue_recovery PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -ffunction-sections -fdata-sections
        -fno-rtti -fno-exceptions
        $<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-fmerge-all-constants>
)

target_link_options(rogue_recovery PRIVATE
        -Wl,--gc-sections
        -Wl,--exclude-libs,ALL
        -Wl,--icf=safe
)

target_link_libraries(rogue_recovery PRIVATE
        shadowhook::shadowhook
        log
        android
        dl
)
