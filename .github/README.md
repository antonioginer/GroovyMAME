# GroovyMAME

## About

GroovyMAME is a fork of [MAME](https://github.com/mamedev/mame) designed for CRT monitors, focused on accurate video output and ultra-low latency.

The ultimate goal is to make the emulation indistinguishable from the real hardware.

## Features

GroovyMAME's main features compared to official MAME include:

* Automatic generation of custom video timings for CRT monitors, through [Switchres](https://github.com/antonioginer/switchres).
* Ultra-low latency audio and video synchronization through [emusync](https://github.com/antonioginer/GroovyMAME/blob/emusync/src/osd/modules/emusync/emusync.md).
* Specialized audio and video backends:
    * `-sound part`: an optimized PortAudio backend, for exclusive-access, ultra-low-latency audio.
    * `-video kmsraw`: a pure-software, front-buffer KMS renderer for Linux.
    * `-video d3d11`: a software-based, bare-bones D3D11 renderer for Windows.
    * `-video mister`: a network renderer for the MiSTer FPGA.
* Integer scaled UI fonts.

## Downloading

Go to the [releases](https://github.com/antonioginer/GroovyMAME/releases) section to download the latest package for your operating system.

## Configuration

GroovyMAME's specific [options](https://github.com/antonioginer/GroovyMAME/blob/emusync/src/osd/modules/emusync/configuration.md).

## Support

Please **DO NOT** ask official MAME developers for GroovyMAME related support. Use these channels to reach us instead:

* GroovyMAME [forum](https://forum.arcadecontrols.com/index.php/board,52.0.html)
* GroovyArcade [discord](https://discord.gg/YtQ6pJh)

## Target audience

GroovyMAME is aimed at users who are willing to have a dedicated emulation setup, whether it's a desktop PC or an arcade cabinet. It's geared towards users who aren't intimidated by a slightly more complex setup. Those individuals who know that CRT shaders will never truly be good enough.

If you are the kind of person that hates tinkering, GroovyMAME is not for you.

## Why a fork?

GroovyMAME is a highly experimental project. By its nature, it works best as a separate codebase, allowing new ideas to be tested more freely. GroovyMAME often relies on operating system-level hacks that wouldn't fit nicely upstream. Moreover, it is only relevant for a subset of the systems that MAME emulates —namely, raster-based systems.

However, when we believe a particular feature could benefit MAME itself, we try to contribute it upstream as a pull request.

## Philosophy

The golden rule of the project is not to break the emulation. This means avoiding any changes that affect the emulation layer.

Because this project is a fork, its long-term viability depends on keeping its footprint on the codebase to a minimum. No extra features will be included except those directly related with to goals of the project.

The project places special emphasis on CRT preservation, since this remains the only display technology that accurately replicates the genuine video game experience.

## What do I need to use GroovyMAME?

To take full advantage of GroovyMAME's features, you need a **CRT** screen, a decent PC, and one of these specific combinations of software and hardware:

- A Linux distribution with a [15 kHz kernel](https://github.com/D0023R/linux_kernel_15khz) such as [GroovyArcade](https://github.com/substring/os), and virtually any ATI/AMD video card.
- Windows 10, and any ATI/AMD video card from the list supported by [CRT Emudriver](http://geedorah.com/eiusdemmodi/forum/viewtopic.php?id=295).
- Linux, Windows or macOS, a Gigabit Ethernet connection and a [MiSTer](https://mister-devel.github.io/MkDocs_MiSTer/) FPGA running the [GroovyMiSTer core](https://github.com/psakhis/Groovy_MiSTer).

## I have an LCD screen, should I use GroovyMAME?

If you have a VRR screen, either FreeSync or G-Sync, then the answer is **no**. These technologies are meant to address mostly the same issues that GroovyMAME solves, though in their own way. So, better stick to baseline MAME and simply use the `-lowlatency` option.

On the other hand, if yours is a traditional non-VRR LCD screen, then GroovyMAME's low latency V-Sync can still be useful, although it will be seriously limited by the screen's fixed refresh rate.

## I don't have an ATI/AMD graphics card, will GroovyMAME work for me?

Short answer: **no**. The long answer follows.

GroovyMAME expects to talk to your GPU directly in order to generate custom video timings on demand. At present, this works reliably only with ATI/AMD graphics cards. On Windows, this requires modified drivers because the operating system does not normally allow this level of control over the video hardware. Such drivers exist only for certain generations of ATI/AMD cards.

Linux allows a higher degree of control over graphics hardware, so other GPU brands may work to some extent. However, AMD hardware still performs best in several important areas (interlaced modes, low pixel clocks, etc.), partly thanks to the availability of open-source drivers that allow the necessary modifications.

With an NVIDIA card, it is theoretically possible to use an external program such as [CRU](https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU) to create a limited set of custom video modes that GroovyMAME can later use. However, this approach requires a non-trivial amount of manual configuration: first defining the modes, and then ensuring that GroovyMAME selects the appropriate one for each game.

In summary, running GroovyMAME with a GPU other than ATI/AMD is likely a recipe for frustration, and certainly far from experiencing its full potential.

If an ATI/AMD GPU is not an option, you may consider running GroovyMAME through a MiSTer FPGA acting as an external GPU. In this setup, the MiSTer is connected to your PC via Gigabit Ethernet and runs the [GroovyMiSTer core](https://github.com/psakhis/Groovy_MiSTer).

## Repository structure

For each upstream release, a new branch is created, named by the `groovymame` prefix plus the version number, e.g.: `groovymame_0286`.

The development branch is named `rebased`. As the name implies, this branch is regularly rebased, so keep this in mind if you clone the repository.

## License

Refer to [MAME license](https://github.com/mamedev/mame?tab=readme-ov-file#license).

