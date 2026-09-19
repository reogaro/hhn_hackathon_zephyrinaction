# PIC64GX Zephyr Application Source

This directory is the embedded source snapshot used by the Flappy Microchip
reference example.

The source contains:

- out-of-tree PIC64GX display drivers
- device-tree bindings and overlays
- the `pic64_smp_hello` Zephyr application
- the Flappy Microchip game implementation

Build and payload instructions are maintained at the repository level:

- [Challenge](../../../CHALLENGE.md)
- [Native Windows setup](../../../BUILD_WINDOWS.md)
- [Flappy Microchip example](../README.md)

Do not use absolute Linux paths from an older local checkout. The workshop
tools use this embedded source directory directly.
