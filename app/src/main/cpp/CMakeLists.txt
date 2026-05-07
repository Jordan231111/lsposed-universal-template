cmake_minimum_required(VERSION 3.22.1)
project(firestonehooks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

find_package(shadowhook REQUIRED CONFIG)

add_library(firestonehooks SHARED
        firestonehooks.cpp
        settings.cpp
        hooks/common.cpp
        hooks/easy_wins.cpp
        hooks/event_exchange.cpp
        hooks/god_mode.cpp
        hooks/game_speed.cpp
        hooks/one_hit_kill.cpp
        hooks/attack_speed.cpp
        native_utils.cpp
)

if(NOT DEFINED TEMPLATE_VERBOSE_LOGS)
    set(TEMPLATE_VERBOSE_LOGS 0)
endif()

target_compile_definitions(firestonehooks PRIVATE
        TEMPLATE_VERBOSE_LOGS=${TEMPLATE_VERBOSE_LOGS}
)

target_compile_options(firestonehooks PRIVATE
        -Wall -Wextra -Wno-unused-parameter
        -ffunction-sections -fdata-sections
        -fno-rtti -fno-exceptions
)

target_link_options(firestonehooks PRIVATE
        -Wl,--gc-sections
        -Wl,--exclude-libs,ALL
)

target_link_libraries(firestonehooks PRIVATE
        shadowhook::shadowhook
        log
        android
        dl
)
