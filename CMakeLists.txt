cmake_minimum_required(VERSION 3.20.0)
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(custom_mcmd)
zephyr_include_directories(include)
target_sources(app PRIVATE 
        src/main.c
        src/digee_ctrl.c
        src/digee_ui.c
        src/display_hal.c
        src/ssd1306_ctrl.c
        
        src/md_encoder.c
        src/buttons.c
        src/midi.c
        src/clocks.c)

