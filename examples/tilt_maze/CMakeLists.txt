cmake_minimum_required(VERSION 3.20.0)

# Tilt Maze reuses the VDMA/HDMI drivers and the out-of-tree devicetree
# bindings from Flappy Microchip rather than copying them.
set(FLAPPY "${CMAKE_CURRENT_SOURCE_DIR}/../examples/flappy-microchip/source")

# The Flappy source root contains dts/bindings/ — tell Zephyr to search it.
list(APPEND DTS_ROOT "${FLAPPY}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(tilt_maze)

set(DRIVERS "${FLAPPY}/drivers")

target_sources(app PRIVATE
    src/main.c
    src/gy521.c
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
