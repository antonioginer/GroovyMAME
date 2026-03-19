# GroovyMAME real-time tips and configuration

**Table of contents:**

- [**Getting ready for real-time.**](#getting-ready-for-real-time)
    - [Introduction](#introduction)
    - [System requirements](#system-requirements)
    - [MAME settings](#mame-settings)
    - [System tweaks](#system-tweaks)
    - [Example](#example)
- [**Configuring automatic framedelay**](#configuring-automatic-framedelay)
    - [Understanding the tear bar](#understanding-the-tear-bar)
    - [Troubleshooting autoframedelay](#troubleshooting-autoframedelay)
    - [Adjusting -fd_margin](#adjusting-fd_margin)
    - [Adjusting -vsync_offset](#adjusting-vsync_offset)
    - [The fallback method: -vblank_thread](#the-fallback-method-vblank_thread)
- [**Configuring PART, the real-time audio backend**](#configuring-part-the-real-time-audio-backend)
    - [API/Device selection](#apidevice-selection)
    - [Audio latency information](#audio-latency-information)
    - [Manual configuration on Windows](#manual-configuration-on-windows)
    - [Manual configuration on Linux](#manual-configuration-on-linux)
- [**Chosing a video backend**](#chosing-a-video-backend)
    - [Escaping the modern video paradigm](#escaping-the-modern-video-paradigm)
    - [-video kmsraw](#-video-kmsraw)
    - [-video d3d11](#-video-d3d11)
    - [What’s the problem with Fullscreen Optimizations?](#whats-the-problem-with-fullscreen-optimizations)
- [**Why can't -vsync_offset be automatic?**](#why-cant-vsync_offset-be-automatic)
    - [Different systems, different timestamps](#why-cant-vsync_offset-be-automatic)
    - [Rendering takes time](#rendering-takes-time)
    - [You promised me automatic framedelay. Instead, I got two manual settings.](#you-promised-me-automatic-framedelay-instead-i-got-two-manual-settings)

## Getting ready for real-time

### Introduction

GroovyMAME with [_emusync_](emusync.md) and [PART audio](#configuring-part-the-real-time-audio-backend) can provide latencies comparable to running on a real system, but might require some additional effort to get everything running smoothly.

*Emusync* estimates where the CRT is currently drawing —it does *beam-racing*—. Since the timestep of each frame is usually in the order of 16-20 ms, it is important that MAME is allowed to run uninterrupted to not miss the next deadline —which is VBlank.

*Emusync* also enables automatic framedelay, making uninterrupted execution even more important. The `-nosleep` parameter helps out with this and is recommended to set. Keep in mind though that using `-nosleep` can increase power consumption, heat dissipation and thus fan noise.

### System requirements

* At least a 4th generation Intel i5 4-core or similar.
* 4 GB RAM.
* System running from SSD.
* A supported graphics card.

### MAME settings

* Use `-nosleep` for best performance, this might increase power consumption/heat but helps out with stability on both *emusync* and PART audio.
* Use `-framedelay 0` and `-autoframedelay` to enable automatic framedelay.
* Press F11 to bring out the tear bar (`-tearbar`) and get real-time framedelay information when a game is running.
* Use `-sound part -audio_latency 0` (0=sets defaults) to enable the PART audio backend.
* Set `-samplerate` to a sample rate natively supported by the audio interface (or just leave it at 48000, the default).
* The `-audio_latency` parameter for PART is specified in milliseconds. Check the verbose log if you are unsatisfied with the defaults. On Windows the defaults work well with on-board audio.

### System tweaks

* Use on-board audio connected to analogue wired speakers.
* Try to have as few applications open as possible.
* For `-sound part`, make sure no other application aside from MAME is outputting audio (this includes frontends).
* For Windows, make sure “Allow applications to take exclusive control” is enabled for the audio interface (enabled by default).
* For Windows, right click the MAME binary, click Compatibility and select "Disable fullscreen optimizations".
* Power plan: Use High Performance / Ultimate Performance (might increase power consumption/heat/fan noise).
* Enable "Game mode".
* If you're having problems on Linux with sudden tearing, disable NTP or sync once on boot before launching GroovyMAME.

### Example

If starting from a clean `mame.ini`, the following command should work for most setups, with little or no additional configuration required.

    mame [game] -nosleep -autoframedelay -framedelay 0 -sound part -audio_latency 0

Read the following sections if the above example doesn't work for you, or you experience glitches.

## Configuring automatic framedelay

As a starting point, open `mame.ini` and check that both `-autosync` and `-autoframedelay` are enabled —they should be by default.

Run a game and bring out the FPS display by pressing [F11]. If the `-tearbar` option is enabled, you should see a vertical green bar crossing the screen from left to right.

Now look at the FPS display at top-right corner: `skip 0/10 fd 8.750 100%`

If automatic framedelay is working, you should notice the `fd` number fluctuates rapidly. This figure is the current value of framedelay being applied.

### Troubleshooting autoframedelay

If you see that `fd` is stuck at a fixed number, it can be due to several reasons:

* `fd` equals `0`:
    * `-autoframedelay` is disabled &#8594; framedelay isn't updated automatically.

* `fd` equals `0.000`:
    * `-autosync` is disabled &#8594; syncrefresh isn't activated automatically.
    * `-autosync` is enabled, but the screen refresh rate is off by a difference greater than the tolerance set by `-syncrefresh_tolerance`. Framedelay won't be functional in this case because syncrefresh is turned off.
    * MAME was started in windowed mode.

* `fd` equals an integer ranging `1-9`:
    * `framedelay` is set to a value different from `0`.
    * `framedelay` is `0` but there's a previous manual UI adjustment in place. Go into Slider Controls &#8594; Frame Delay, press [Delete] to reset the default value.

### Understanding the tear bar

Once you know that automatic framedelay is working, you can focus on the tear bar. This tear bar is the basic tool we will use for adjustment. It is meant to expose the existing tearing, which might otherwise be difficult to spot.

Ideally, the tear bar should move smoothly through the screen, without visible jumps or tear. If you notice issues on the tear bar, try this before going any further:

* Set `-nosleep` (`sleep 0` in mame.ini).
* On Windows, right click on MAME's executable and check "Disable fullscreen optimizations".
* On Linux, make sure `drm.debug` isn't enabled. If you don't know what this is, simply ignore this.

Now that the usual disruptors are ruled out, take a look at the glitches on the tear bar and try to determine which pattern they follow:

* Random distortions at the top of the screen, which may be followed by periods of apparent perfection. This might point to variability in emulation times. If it consistently occurs in certain phases of the game, such as stage transitions, or if it's accompanied by large fluctuations in the`fd` value, you can be sure this is the case. This issue is addressed by `-fd_margin`.
* Tearing that usually appears at the top of the screen. Sometimes —less usual— it may happen at the bottom of the screen. On some systems, instead of a tear line it will appear as stutter on the upper segment of the bar. The problem is consistent and not clearly related to a certain phase of the game. These issues are addressed by `-vsync_offset`.

The above distinction is meant to be a rule of thumb rather than a scientific statement. In practice, `-fd_margin` and `-vsync_offset` effects are intertwined: offsetting V-Sync earlier provides an extra margin for framedelay. Think of `-fd_margin` as more focused on addressing CPU-related issues —emulation time variability— while `-vsync_offset` is meant for adjusting a GPU-related aspect —finding the sweet spot within the scanout at which we should send the frame to the GPU for rendering.

### Adjusting -fd_margin

Some systems have very consistent frame time emulation (`neogeo`), and some fluctuate wildly (`cv1k`). `-fd_margin` provides a safety margin for systems with fluctuating frame times, to avoid visible tearing at the cost of latency. The value of `-fd_margin` is a set time (specified in milliseconds) that GroovyMAME will add to the current emulation time average. By default, `-fd_margin` is set to 1.0 ms.

For instance, let's say the current time average of the recently emulated frames is 2.5 ms. And the frame period is 16.67 ms.

    fd = (period - (emutime_average + fd_margin)) / period * 10
    fd = (16.67 - (2.5 + 1.0)) / 16.67 * 10 = 7.900

So in this case, GroovyMAME will apply an automatic framedelay of 7.900. Now, imagine the next frame takes a bit longer to emulate than average, say 3.0 ms. Since this value is smaller than 2.5 + 1.00, we'd still be on the safe side and the frame would be rendered just in time.

But it's also possible that, due to a spike in the emulation time, the next frame takes so long to emulate that it exceeds the margin we've established. Say it takes 4.0 ms, we'd be 0.5 ms late, causing tearing at the top of the screen. If this happens often in the game, it'd be reasonable to raise `fd_margin` to 1.5 ms, so most or all the spikes are covered by the new margin.

GroovyMAME detects when `-fd_margin` has been exceeded and lowers `fd` automatically for a while until *emutime* stabilizes. You'll notice this as big fluctuations in `fd` value, so these are an indication that `-fd_margin` might need an adjustment.

Needless to say, `-fd_margin` comes at the cost of latency, so you need to find the balance that fits the case. You'll notice that `-fd_margin` has an effect on the final `fd` value that is achievable. In an ideal world, where *emutime* was absolutely stable, `-fd_margin` could be lowered to zero, maxing out `fd`.

### Adjusting -vsync_offset

A consistent tear line or stutter at the top of the screen usually indicates that `-vsync_offset` needs an adjustment. Although less common, a tear line may also appear at the bottom of the screen. Depending on the case, apply this:

* Tearing at the top of the screen means we're blitting too late: set a negative value to `-vsync_offset` to blit earlier moving the tear line up.
* Tearing at the bottom of the screen means we're blitting too early: set a positive value to `-vsync_offset` to delay the blit moving the tear line down.

**Important tip!** Always tweak `-vsync_offset` through the on-screen slider "Vsync Offset" —which brings the slider at the bottom of the screen— by pressing the tilde key (~ or `), located to the left of the '1' key on most keyboards. Never do it through the full sliders menu that's toggled by the [TAB] key. The reason for this is that MAME's UI rendering is so inefficient that it has an effect on the tear position.

The value you set in `-vsync_offset` is the number of lines —scanlines—, positive or negative, that GroovyMAME will offset the sync position, relative to the VBlank timestamp reported by the operating system.

Setting a negative value, i.e. synchronizing earlier than the real VBlank —what you'll typically do to hide tearing at the top of the screen— has the effect of increasing latency. Keep in mind we're talking of sub-frame latency figures. So, for a typical 15 kHz video mode, each 16 lines will add around 1 ms of latency.

On the contrary, a positive value will delay the sync position, adding to the benefit of framedelay and reducing latency accordingly. On a fast computer, this can be leveraged to squeeze out the last millisecond in the scanout by pushing the emulation down into VBlank. Logically, this makes synchronization more susceptible to spikes in *emutime*.

Please be aware that the correct `-vsync_offset` setting depends on the video renderer you select. It also depens on the GPU —more specifically, its drivers.

### The fallback method: -vblank_thread

On certain setups, the timestamps available to *emusync* are buggy. You'll know this when the methods discussed above don't have a clear effect removing or reducing tearing. Of course, you're supposed to check that your CPU is powerful enough to emulate that specific system fluently —don't take this for granted, please.

Enabling `-vblank_thread` may fix the problem in most cases. This option starts a background thread to register VBlank timestamps. Since this option has a higher CPU consumption, you should only use it when the default timestamps are buggy.

On Windows, when using the renderers `-video bgfx` and `-video opengl`, this option is enabled internally.

Also, notice this option may require a different `-vsync_offset` setting.

## Configuring PART, the real-time audio backend

The PART (PortAudio Real-Time) sound backend can provide close to PCB-level latencies, but is not as forgiving as the other APIs.

To get the lowest possible latencies, *exclusive* access to the audio output device is required. That means that *no other* application can use it at the same time, which could cause problems with frontends and other running applications. So if you're having problems with no audio output, shut down all other apps and try launching MAME directly.

Compared to the native APIs, it only supports a single stereo output device, and no mic input.

PART also greatly benefits from `-nosleep`.

### API/Device selection

When initializing GroovyMAME —if started with `-sound part`—, the info log will output something like this:

    PART: API ALSA has 13 devices
    PART: ALSA: "HDA NVidia: 27GL850 (hw:0,3)"
    PART: ALSA: "HDA NVidia: HDMI 1 (hw:0,7)"
    PART: ALSA: "HDA NVidia: HDMI 2 (hw:0,8)"
    PART: ALSA: "HDA NVidia: HDMI 3 (hw:0,9)"
    PART: ALSA: "HDA ATI HDMI: 0 (hw:1,3)"
    PART: ALSA: "HD-Audio Generic: ALCS1200A Analog (hw:2,0)"
    PART: ALSA: "HD-Audio Generic: ALCS1200A Digital (hw:2,1)"
    PART: ALSA: "HD-Audio Generic: ALCS1200A Alt Analog (hw:2,2)"
    PART: ALSA: "hdmi"
    PART: ALSA: "jack"
    PART: ALSA: "pipewire"
    PART: ALSA: "pulse"
    PART: ALSA: "default" (default)
    PART: API OSS has 0 devices

If you want to specify a device explicitly, you can do it using the `-part_api`
parameter in combination with the `-part_device` parameter, such as:

    -part_api "ALSA" -part_device "HD-Audio Generic: ALCS1200A Analog (hw:2,0)"

Later down in the log, when opening the sound device, something like this will
be output:

    PART: Opening device "ALSA: HD-Audio Generic: ALCS1200A Analog (hw:2,0)"
    PART: Sample rate is 48000 Hz, device output latency is 18.67 ms
    PART: Allowed additional buffering latency is 2.00 ms/96 frames

Which reports a latency figure. The resulting latency with the above example
(default `-audio_latency`) is reported to be around ~18.67 ms + 2.0 ms = ~20.67ms.

The following sections outline how to get better latency figures.

### Audio latency information

The lower the latency, the more likely it is that you'll experience audio
crackles.

The main source of latency is the resulting latency of the frequency at which
the operating system forwards audio to the audio device (given you're using
analog, wired, on-board audio). For PART, this is controlled with the `-audio_latency`
parameter (specified in milliseconds). This is almost always the source of
crackling audio and might require some trial and error to figure out what the
system is capable of. The Linux/Windows sections detail how to figure out this
setting.

### Manual configuration on Windows

In Windows, the info log with available devices can look like this:

    PART: API Windows WASAPI has 4 devices
    PART: Windows WASAPI: "Speakers (High Definition Audio Device)" (default)
    PART: Windows WASAPI: "Digital Audio (S/PDIF) (High Definition Audio Device)"
    PART: Windows WASAPI: "Speakers (High Definition Audio Device) [Loopback]"
    PART: Windows WASAPI: "Digital Audio (S/PDIF) (High Definition Audio Device) [Loopback]"
    PART: API Windows WDM-KS has 2 devices
    PART: Windows WDM-KS: "Speakers (HD Audio Speaker)" (default)
    PART: Windows WDM-KS: "SPDIF Out (HD Audio SPDIF out)"

`-part_api "Windows WASAPI"` or `-part_api "Windows WDM-KS"` are supported.

Start out with `-audio_latency 4`, increase to `8` and then `16` if audio crackles.

Example WASAPI invocation:

    mame -verbose -nosleep -sound part -part_api "Windows WASAPI" -audio_latency 4

Example WDM-KS invocation:

    mame -verbose -nosleep -sound part -part_api "Windows WDM-KS" -audio_latency 4

For WASAPI the verbose log tells us that we end up with ~6.0ms, and WDM-KS
gives us ~7.0 ms:

    PART: Opening device "Windows WASAPI: Speakers (High Definition Audio Device)"
    PART: Sample rate is 48000 Hz, device output latency is 4.00 ms
    PART: Allowed additional buffering latency is 2.00 ms/96 frames
    ...
    PART: Opening device "Windows WDM-KS: Speakers (HD Audio Speaker)"
    PART: Sample rate is 48000 Hz, device output latency is 5.00 ms
    PART: Allowed additional buffering latency is 2.00 ms/96 frames

Use the `-part_device` parameter to force a specific device if desired, the reported
latency numbers are put in the verbose log (like above).

### Manual configuration on Linux

On Linux, the raw ALSA device or PipeWire should be used. Most of the testing
has been done with the raw ALSA device directly. Only specifying the `-part_api`
parameter uses the default device of the selected API (usually pipewire).

Most often, pipewire ends up being the default. To use the raw ALSA device of
the sound chip, specify it explicitly using both `-part_api` and `-part_device`
like the example below. Latency figures using the raw ALSA device seem to be
reported accurately by the verbose log (based on actual measurements).

To figure out what latency the system supports, start out with a non-demanding
game and set `-audio_latency 4` along with the other desired settings. Increase
`-audio_latency` to either `8` or `16` if you get crackling audio.

Example ALSA invocation with raw ALSA device (bypassing pipewire/pulseaudio):

    mame -verbose -nosleep -sound part -part_api "ALSA" -part_device "ALSA: HD-Audio Generic: ALCS1200A Analog (hw:2,0)" -audio_latency 4

Example PipeWire invocation:

    mame -verbose -nosleep -sound part -part_api "ALSA" -part_device pipewire -audio_latency 4

For both examples, the verbose log reports a latency of ~5.5ms:

    PART: Opening device "ALSA: HD-Audio Generic: ALCS1200A Analog (hw:2,0)"
    PART: Sample rate is 48000 Hz, device output latency is 4.00 ms
    PART: Allowed additional buffering latency is 2.00 ms/96 frames
    ...
    PART: Opening device "ALSA: pipewire"
    PART: Sample rate is 48000 Hz, device output latency is 4.00 ms
    PART: Allowed additional buffering latency is 2.00 ms/96 frames

## Chosing a video backend

### Escaping the modern video paradigm

The new *emusync* features are included in all baseline MAME video backends, with the exception of `-video gdi`. This includes:

* `-video d3d` on Windows
* `-video opengl` on Windows and Linux
* `-video bgfx` on Windows and Linux
* `-video accel` on Linux

The safest choice is `d3d` for Windows and `accel` for Linux.

For all these renderers, sub-frame latency —next-frame response— has been tested and confirmed.

Notice each renderer may require a slightly different `-vsync_offset` adjustment. So better settle for one before doing adjustments. A longer [render time](#rendering-takes-time) impacts `-vsync_offset`. Compute-intensive shaders like CRT filters have a huge impact on render times.

The modern graphics pipeline is a nightmare when your purpose is to simply blit a bitmap on a frame buffer. Below are some minimalist software-based renderers you should use if your setup allows it.

### -video kmsraw

KMS "raw" is a pure-software, front-buffer KMS renderer for Linux.

* **Use this renderer for** &#8594; low resolutions & CRTs. No desktop environment (KMS).
* **Don't use this renderer for** &#8594; high resolutions or LCDs, artwork. Desktop environment (X11, etc.)

This renderer has the potential of providing the lowest latency on a PC setup. Front-buffer blitting means the scanout begins even before the bitmap transfer has finished!

No CPU-GPU parallelization: all the process is handled by the CPU, so we know exactly *when* the bytes are written on the frame buffer.

This renderer completely bypasses SDL and OpenGL. Modesetting as fast as it gets, finally free from resource acquisition overhead.

As resolution increases or scaling is applied, software rendering becomes rapidly inefficient. For high resolutions, better use the `accel` or `opengl` renderers.

### -video d3d11

A software-based, bare-bones D3D11 renderer for Windows.

* **Use this renderer for** &#8594; low resolutions & CRTs. GCN+ AMD GPUs.
* **Don't use this renderer for** &#8594; high resolutions or LCDs, artwork. pre-GCN ATI/AMD GPUs.

This is a partially software-based renderer. Frame compositing is done by software at the native's game resolution —you'll notice by the blocky UI fonts—, but the last upscaling step, if required, is done on the GPU. Think of this as an updated version of the ancient `ddraw` backend.

It also includes an interesting shader-based filter —enabled through `-autofilter`— that performs smart, axis-independent pixel interpolation, particularly useful for super-resolution scaling.

This renderer was originated basically to gain access to the new swapchain interface —which currently preserves fullscreen-exclusive capabilities— in an attempt to overcome the decline of the native D3D9 renderer, a consequence of modern Windows’ fullscreen-exclusive abolition —via the euphemistically named Fullscreen Optimizations.

Unfortunately, it was later discovered that this backend adds a full frame of latency on older ATI GPUs —confirmed at least on the HD 5000 series, likely due to legacy drivers— which provided strong reasons to remove it. However, since it remains useful on modern hardware and played an important role during the early development of *emusync*, it is currently kept. It also comes with the firm compromise of never supporting CRT shaders.

### What's the problem with Fullscreen Optimizations?

Fullscreen Optimizations (FSO) is a Windows 10 feature introduced in 2017, which basically makes programs believe they're running in exclusive fullscreen mode, while Windows keeps them in a borderless-windowed mode. With the excuse of faster Alt-Tab behaviour and the introduction of nonsense overlays they broke a basic functionality that many programs relied on.

Although in early Windows 10 updates it was possible to disable FSO through a global switch, at some point they felt it was good enough for everyone so the setting was forced on us. Now the only option to disable FSO is on a per-executable basis, by right-clicking on the .exe and checking **"Disable Fullscreen Optimizations"** in the Compatibility tab.

Ok, so what's so bad about FSO? We don't know exactly what FSO does behind the scenes, but for sure it's nothing good —it gets between us and the frame buffer, when we're pursuing real-time exclusiveness—. In our experience, it causes a full frame of latency on the secondary screen, and messes up black frame insertion.

Notice the `-video d3d11` renderer natively bypasses this issue.

## Why can't -vsync_offset be automatic?

### Different systems, different timestamps

One issue with VBlank timestamps is that, depending on the implementation, they point to a different position within the VBlank period.

Consider the following example setup. For `amdgpu`, on Linux the timestamps point to the end of the VBlank —or the first visible line—, while on Windows, the same hardware reports its timestamps shortly before the V-Sync pulse:

- Linux `-video kmsraw`
- AMD RX 7600

`modeline "768x448_59 31.43Hz 59.637Hz" 30.486045 768 797 912 970 448 476 478 527`

        -28 ---------------------      last visible line
                    VFP
          0 =====================      v. sync
                    VBP
         52 —————————————————————--+-  timestamp (first visible line)
                                   |
                                   |0.85
                active video       |vratio
                                   |
                                   V
        499 —————————————————————--+   last visible line (before blit)
                    VFP
        526 =====================      v. sync


- Windows `-video d3d11`
- AMD RX 7600

`modeline "768x448_59 31.43Hz 59.637Hz" 30.486045 768 797 912 970 448 476 478 527`

        -28 ---------------------      last visible line
                    VFP            +-- timestamp (last visible line + ≈4)
          0 =====================  |   v. sync
                    VBP            |
         52 —————————————————————  |   first visible line
                                   |
                                   |0.85
                active video       |vratio
                                   |
                                   V
        424 -----------------------+-- (372 =424-52) (before blit)
        499 —————————————————————      last visible line
                    VFP
        526 =====================      v. sync

Because of this difference in timestamps, the default settings will yield a significantly lower latency on Linux than on Windows —just for this specific setup—. But it will also make the Linux setup more susceptible to tearing and glitches. So, the reasonable adjustment here would be to apply a small negative offset on Linux, to gain stability, and a generous positive offset on Windows, to reduce latency, but with caution so as not to compromise stability.

### Rendering takes time

But there's another issue we need to deal with: the rendering pipeline consumes time. When we submit a new frame to the GPU, it doesn't start drawing it immediately on the screen. Instead, it needs some processing time before it's ready for the scanout. Therefore, we must take this time into account to shift the theoretical sync position and avoid falling behind the raster. In other words, we need to submit the frame earlier so that rendering finishes at the exact point that we want.

Unfortunately, since GPU processing is done in parallel relative to the CPU, we don't have an easy way of knowing how long it takes the GPU to finish its job —at least, not programmatically—. Besides, the rendering time depends on multiple aspects:

* The video renderer —API backend
* The frame's resolution
* The frame's complexity —cf. MAME's UI issue, use of shaders, etc.
* The GPU's internal state
* The GPU's speed

Non-deterministic rendering times is what makes GPU's parallel processing undesirable when we pursue ultra-low latency. This is why —contrary to intuition— removing or at least reducing this parallelism benefits latency-wise performance.

In summary, there are several sources of uncertainty that affect our estimate of the optimal timing position. Addressing them all programmatically would require a level of complexity that would be difficult to manage. Solving the problem through empirical tuning is more appropriate in practice.

### You promised me automatic framedelay. Instead, I got two manual settings.

Seen this way, the new implementation wouldn't be completely automatic, would it? Well, we believe that stating this would be unfair. That's why it needs to be qualified.

The previous implementation, as is well known, presented the following problems:

* The `-syncrefresh` function itself required more CPU than strictly necessary for emulation, potentially causing performance drops of up to 50% in extreme cases.
* The enormous differences in CPU requirements between different systems made it impractical to enable `-framedelay` globally.
* Manual adjustments per game also didn't completely prevent speed fluctuations, which caused the infamous audio distortion.

All these problems are a thing of the past thanks to [the new synchronization system](emusync.md). In fact, in many cases, the default options will already work well enough without any adjustments. In those cases where `-vsync_offset` requires adjustment, a global setting may be sufficient.

This is why framedelay is now enabled by default, achieving sub-frame latencies even on modest systems, with an identical CPU demand to that of MAME without V-Sync.
