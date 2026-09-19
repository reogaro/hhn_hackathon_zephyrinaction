cmake_minimum_required(VERSION 3.20.0)

# Flappy Microchip is the base of this project. Its VDMA/HDMI drivers and the
# out-of-tree devicetree bindings are reused from there rather than copied.
set(FLAPPY "${CMAKE_CURRENT_SOURCE_DIR}/../flappy-microchip/source")

# The Flappy source root contains dts/bindings/ — tell Zephyr to search it.
list(APPEND DTS_ROOT "${FLAPPY}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(flappy_accel11)

set(DRIVERS "${FLAPPY}/drivers")

target_sources(app PRIVATE
    src/main.c
    src/ui.c
    src/demo_manager.c
    src/screen_hud.c
    src/screen_dashboard.c
    src/screen_charts.c
    src/screen_widgets.c
    src/microchip_logo_img.c
    src/screen_game.c
    src/accel11.c
    ${DRIVERS}/vdma_disp/vdma_disp.c
    ${DRIVERS}/hdmi_fb/hdmi_fb.c
)

# hdmi_fb.c includes vdma_disp.h — expose both driver directories
target_include_directories(app PRIVATE
    src
    ${DRIVERS}/vdma_disp
    ${DRIVERS}/hdmi_fb
)

target_link_libraries(app PRIVATE m)
