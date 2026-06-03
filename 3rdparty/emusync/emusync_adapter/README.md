# emusync serial dongle

The emusync serial dongle allows measurement of events relative to the actual
VSYNC signal. This helps troubleshoot problematic timings and provides
real-world latency figures (given working next-frame response).

GM continously sends "tags" over the serial port corresponding to events
(POLL_INPUT etc), the STM32F103 registers these events in relation to VSYNC and
GM collects the registered events once per frame.

## Example timing diagram:

                   frame -1:                               frame 0:
           ... ___   _____________________________________   _____________________________ ...
    VSYNC         |_|                                     |_|
                    ^                            ^^           ^^                         ^ ...
                    |<---------------------------||-------->|<||-------------------------| ...
    reference point |        dump and clear tags ||           ||     dump and clear tags |
        for frame 0 |           poll input (~70%) |           ||
                                          before draw (~103%) ||
                                          after draw (~103.5%) |

Output can look like this:

    [1608.429][/dev/ttyAMA0] 74, 60.600 Hz: POLL_INPUT @  81.025%, BEFORE_PRESENT @  89.366%, AFTER_PRESENT @  91.654%

or like this:

    [2240757.489][/dev/ttyS0] 337, 57.572 Hz: POLL_INPUT @  93.533%, BEFORE_DRAW @  98.471%, AFTER_DRAW @  99.170%

## Limitations:

Only negative sync polarity for VSYNC is supported.

This will not work well with USB-based serial ports due to timing constraints.
A PCI-express card or a serial port integrated on the mainboard (not USB-based)
should work.

Baud rate is limited to 115200. This means that we have a theoretical bit
resolution of ~8.7us. It also means that one byte (and one stop bit) will take
~78 us to transfer, and this is thus the smallest time difference we'll be able
to measure (BEFORE_PRESENT/AFTER_PRESENT for instance). We'll also be affected
by whatever the operating system decides to do, but in practice this doesn't
seem to be a problem.

## Items required:

* STM32F103 "Blue Pill" board.
* MAX232/MAX3232 RS-232 level shifter board.
* A way to get the VSYNC signal from the VGA connector to the "Blue Pill".

## Connections:

    STM32F103 "Blue Pill"

    +5V  ---------------------- +5V  RS-232 Adapter
    PA9  (USART1_TX) ---------> RX   RS-232 Adapter
    PA10 (USART1_RX) <--------- TX   RS-232 Adapter
    GND  ---------------------- GND  RS-232 Adapter

    PA8  ---------------------- VSYNC  VGA Connector
    GND  ---------------------- GND    VGA Connector (*)

(*) Can be omitted if "Blue Pill" and VGA output already share the same ground,
    which will most likely be the case if they're powered from the same PC.

## Flashing

Download stm32flash, AUR on arch or from https://sourceforge.net/projects/stm32flash/files/stm32flash-0.7-binaries.zip/download

Disconnect adapter, set BOOT0 = 1 and BOOT1 = 0, connect adapter.

Replace /dev/ttyS0 with the actual serial port (COM1 under Windows for
instance).

    stm32flash -w emusync_adapter.hex -v -g 0x0 /dev/ttyS0

If init fails, disconnect and connect adapter and try the above command again.

When successfully flashed, disconnect adapter, set BOOT0 = 0 and BOOT1 = 0,
connect adapter.

Run GM with option "-emusyncserial /dev/ttyS0".

## Building

Import into STM32Cube IDE, change optimization level to -O2 in project settings,
add the following line in Project Properties > C/C++ Build > Settings >
Build steps > Post-build steps > Command:

    arm-none-eabi-objcopy -O ihex ${ProjName}.elf ${ProjName}.hex

To create a hex file that can be flashed with stm32flash.
