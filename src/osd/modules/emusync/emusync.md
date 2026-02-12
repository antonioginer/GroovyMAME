# The new synchronization system

>_Mixed feelings about all this progress. Feels like we fix basically all issues at once haha :D_
-- <cite>**intealls**</cite>

## Introduction and Acknowledgements

We are introducing _emusync_, a new synchronization system for GroovyMAME, coded by **intealls** and **Calamity** —with intealls taking care of most technical aspects, to be completely fair—. Its purpose is to achieve ultra-low latency audio and video synchronization.

Addressing input latency is an elusive matter for software developers, because latency cannot be measured through software methods. This has contributed to it remaining as a poorly understood phenomenon.

Emusync borrows its name and the core idea from an experimental library for software-based raster interrupts, coded by **Doozer** and **Calamity** back in 2019, which was never completed. It also brings in some ideas from **Mark Rejhon's** posts, although the current implementation is not frame-sliced.

The automatic frame delay implementation in _emusync_ is heavily based on prior work by **psakhis's** for the [Groovy_MiSTer](https://github.com/psakhis/Groovy_MiSTer) project, who proved the feasibility of this idea.

Previous implementations discussed in the following sections refer to those found in older releases of GroovyMAME. We are aware of other implementations in different software that might be partially analogous to certain of these features. The key difference lies in how these features are integrated.

## Isn't V-Sync enough?

While all graphics APIs provide some method to achieve vertical synchronization, the level of control they offer is unfortunately not enough to provide a latency-free experience.

The latency associated with V-Sync mainly comes from two sources: buffering and parallelization.

Buffering is the primary cause of latency. Some degree of buffering is expected, as the raster is fed with data coming from a framebuffer allocated in VRAM. The problem arises when graphics APIs use more buffering than is strictly necessary. Instead of keeping just a single back buffer for off-screen rendering, they allocate more than one.

Seemingly, all APIs inject at least one extra buffer (in addition to the back buffer) in order to avoid possible tearing at the top of the screen. This tearing would occur when a frame is submitted to the GPU near the end of the current scanout, and the GPU is not fast enough to fill the back buffer before the vertical retrace. The insertion of this buffer allows applications to submit frames at arbitrary times without visible tearing. However, this extra buffer alone makes next-frame response impossible.

By default, more than one extra buffer is often injected. Some APIs allow limiting this value to one, while others offer no control at all, usually queueing several buffers, each one introducing an unnecessary latency penalty.

Parallelization occurs when the CPU submits a frame to the GPU for rendering and then continues performing its own tasks. V-Sync responsibility is therefore offloaded to the GPU as part of the rendering process. The actual frame presentation is deferred until VBlank is reached, but in the meantime the CPU has already been working on the next frame. This decoupling between the CPU and the GPU translates into latency, since the frame being displayed is unnecessarily old relative to the input currently being processed.

Things get even worse when both effects combine. Because the CPU is not throttled by V-Sync due to parallelization, it submits frames at a faster rate than the GPU can display them, flooding the GPU's buffer queue. Only when the GPU can no longer accept new frames is the CPU finally forced to wait, and true coupling between the two emerges. This coupling, which is fundamental for smooth, glitch-free V-Sync, now comes at the cost of several frames of latency, and is the reason behind the ridiculous amount of lag users experience with V-Sync enabled.

## Frame delay

Paradoxically, the way to solve the fundamental flaw in conventional V-Sync implementations is to disable V-Sync entirely. More precisely, we replace it with our own implementation.

Frame delay has been described as a technique to reduce input latency. It is more accurate to think of it as a special kind of V-Sync —a _lag-less_ or _low-latency_ V-Sync, so to speak—. Therefore, frame delay implementations that are not tightly linked to V-Sync are not true frame delay.

To visualize this, imagine the sequence a normal emulator follows: first emulate a frame, then wait for VBlank, and finally present the frame. Latency arises because the emulated frame ages while waiting for VBlank. The amount of latency depends on the point within the scanout at which the final rendering step occurs. In the worst case, it amounts to a full refresh period of lag.

This is the picture people usually have in mind when they think about why V-Sync causes latency. The real situation —the buffering trainwreck explained in the section above— is much worse.

However, if we knew exactly when VBlank was going to occur, we could wait until the very last moment to emulate the frame using the most recent input, and then present it immediately with V-Sync disabled. This is how frame delay works: it _delays_ the emulation of a frame relative to the scanout in order to capture the freshest possible input.

The amount of time the emulation can be delayed depends mainly on how long it takes to emulate a single frame. The faster a system can be emulated on a given PC, the lower the input latency that can be achieved through this method — the longer we can wait without missing VBlank. GPU speed, while usually less critical, also matters, especially at higher resolutions where rendering time becomes significant.

There is an obvious catch to this method, though:
- Emulation time is not constant from frame to frame, and can vary significantly for the same system during gameplay depending on the internal state of the emulation at a given moment.
- We do not know beforehand how long the emulation of the next frame will take.

To deal with this issue, previous implementations allowed the user to specify the amount of delay in fractions of a frame (1–9). As a result, in order to stay on the safe side, one had to consider the worst-case frame and apply a frame delay setting that would avoid missing VBlank under that scenario. This approach was not only suboptimal in terms of latency, but also impractical, as it required ad-hoc, per-game adjustments, either determined empirically during gameplay or estimated through prior benchmarking.

Past implementations also relied on specific API calls to explicitly wait for VBlank on every frame. This allowed the next VBlank event to be predicted based on the previous one, and the delay to be scheduled accordingly. Unfortunately, this mechanism was not smart enough to accurately detect when a VBlank event had been missed — due to frame emulation taking longer than expected — nor to determine its actual timestamp. When this happened, the only option was to resynchronize to the next VBlank. This behavior is the root cause of the infamous speed fluctuations associated with frame delay, affecting both video and audio.

Automatic frame delay had been discussed for more than a decade, but achieving it required a complete reformulation of V-Sync. Instead of delegating V-Sync to the graphics API, we keep a record of VBlank timestamps, allowing us to accurately predict upcoming events without blocking the emulator at any point. In parallel, frame delay is adjusted adaptively based on recent frame emulation times, optimizing latency across different phases of execution.

The key difference now is that, if a VBlank event is missed, performance is not affected. Instead, a missed VBlank results in tearing at the top of the screen, which goes unnoticed in most situations. Frame delay then readjusts itself to the new conditions until emulation times stabilize.

It is worth noting that this kind of implementation requires a graphics API that allows tearing, which is generally the case when V-Sync is disabled. It then becomes our responsibility to hide this tearing within the VBlank window through accurate timing. An unexpected side effect of this approach is that it effectively dismantles the buffer queue described earlier, along with its associated latency. This latency reduction is several times greater than the theoretical gain provided by frame delay alone, which explains why the impact of frame delay on latency is so dramatic.

## Audio latency and V-Sync

Audio latency is often neglected even more than video latency. The starting point is already suboptimal, with the audio hardware being shared by multiple programs and the operating system performing the final mix. This introduces an initial level of buffering. On top of that, the emulator adds its own buffering, typically implemented with a conservative approach aimed at avoiding potential glitches rather than minimizing latency.

As a result, with default settings applied, audio latency figures on the order of 100 ms are commonplace. Because a significant portion of this latency originates within the emulator itself, selecting different audio APIs has only a partial effect on reducing overall latency.

With this in mind, an effective strategy for addressing the problem must operate on two fronts: buffer management and hardware ownership. Minimizing buffering is the primary goal, while exclusive access to the audio hardware —available only through certain APIs— provides the best possible results.

Aggressive audio buffer management is a challenging task that involves [real-time](https://github.com/intealls/GroovyMAME/blob/emusync_stuff/GMRT.md) concerns and demands a very stable environment. In order to avoid buffer overflows and underflows —in other words, audio glitches— we must ensure that samples are consumed by the audio hardware at the same rate at which they are produced by the emulator. The smaller the buffer, the less forgiving it becomes to even minor timing fluctuations.

Samples are consumed at the audio hardware's clock rate, so it is fundamental to estimate its real value, as it will never exactly match the nominal one. Ignoring this and blindly relying on the reported value will inevitably lead to an eventual buffer overflow or underflow, depending on whether the reported clock rate overestimates or underestimates the real one.

Now, at which rate are the samples produced? By default, the emulator will output samples at a fixed rate, based on the assumption that the emulation runs at the same speed as the original hardware. However, once V-Sync is in place, it is the video card's clock —which, ideally, has been programmed to faithfully replicate the original hardware's vertical refresh rate— that dictates the actual output speed.

Due to limitations in the granularity of the pixel clock, a perfect match with the theoretical refresh rate can never be achieved, although we can get very close. We need to know this deviation in order to compute a resampling factor that keeps the buffer contents stable.

At this point, it becomes evident that accurately estimating the actual video refresh rate also plays a fundamental role in audio synchronization. Our goal is to perform resampling in such a way that any pitch variation remains inaudible. Previous naive attempts, such as deducing the refresh rate from instantaneous speed, resulted in annoying pitch wobble.

Audio/video synchronization is a fundamental dilemma: one must synchronize to either of the two clocks. One might reasonably wonder why the video refresh rate should prevail over the audio rate. The only reason is that, because the audio buffer is linear —rather than composed of discrete frames as video is— it is perceptually easier to hide timing adjustments on the audio side.

## Emusync. Putting the pieces together

Now that the problem has been outlined, it is time to introduce _emusync_. Emusync is not the name of a specific library or implementation, but rather a centralized component within the emulator where real-time data from the audio and video hardware is collected, processed, and used for timing and synchronization, acting as a bridge between the two domains. It orchestrates V-Sync and turns it into the primary throttling mechanism, with a strong focus on minimizing latency. This is a unified effort to address all of the issues discussed above at once.

Emusync consistently achieves sub-frame end-to-end audio and video latency, even on modest hardware. Naturally, the faster the system the emulator runs on, the lower the latency that can be achieved. Typical measured values fall in the 5–7 ms range for audio, and even lower for video.

Needless to say, this does not include the internal buffering of the emulated system, which usually adds one or two frames — and even more in some extreme cases — but ensures next-frame response on systems that natively behaved that way.

Emusync brings together a bunch of techniques, namely:

- Scanout position estimation and VBlank prediction
- Automatic frame delay
- Video refresh rate estimation
- Audio sink rate estimation
- Adaptative audio resampling
- Real-time event logging
- Serial port real-world event debugging

Emusync features are fully functional across the different video backends available in MAME, on both Linux and Windows: `d3d`, `opengl`, `bgfx`, `accel`, etc., although `d3d` on Windows and `accel` on Linux are the preferred default options. The same applies to the audio backends, where the default selection is `alsa` on Linux and `wasapi` on Windows.

However, to unleash emusync's full potential, some new specialized backends have also been implemented. These should be the preferred options when possible:

- [PART](https://github.com/intealls/GroovyMAME/blob/emusync_stuff/GMRT.md) (PortAudio Real-Time) (`-sound part`): an optimized PortAudio backend providing exclusive-access, ultra-low-latency audio on Windows and Linux. Exclusive access is optional, but required to achieve the lowest possible latencies — be aware that this means no other applications can use the audio hardware at the same time.

- KMS "RAW" (`-video kmsraw`): a pure software, front-buffer KMS renderer for Linux. It completely bypasses SDL and OpenGL. Modesetting as fast as it gets, finally free from resource acquisition overhead. Zero parallelization. Front-buffer blitting: scanout begins even before blitting has finished. The Holy Grail renderer for low-resolution CRTs.

- An experimental D3D11 backend (`-video d3d11`): a software-based, bare-bones D3D11 renderer. Think of it as a modernized version of the ancient `ddraw` backend. It originated as an attempt to overcome the decline of the native D3D9 renderer, a consequence of modern Windows’ fullscreen-exclusive abolitionism — via the euphemistically named Fullscreen Optimizations.

  Its main purpose is to gain access to the new _swapchain_ interface, which currently preserves fullscreen-exclusive capabilities. It also includes an interesting shader-based filter — enabled through `-autofilter` — that performs smart, axis-independent pixel interpolation, particularly useful for super-resolution scaling.

  Unfortunately, it was later discovered that this backend adds a full frame of latency on older ATI GPUs — confirmed at least on the HD 5000 series, likely due to legacy drivers — which provided strong reasons to remove it. However, since it remains useful on modern hardware and played an important role during the early development of emusync, it is currently kept. It also comes with the firm compromise of never supporting CRT shaders.

For raster synchronization, VBlank timestamps are obtained through OS-specific APIs —_GetFrameStatistics_ on Windows/D3D, _drmCrtcGetSequence_ on Linux. When the backend does not provide these facilities, a fallback threaded VBlank polling implementation is used, which can also be optionally forced through `-vblank_thread` in situations where the default method yields inaccurate timestamps.

These raw timestamps are filtered to remove inherent jitter, allowing for highly precise estimations. As a bonus, we get a very good estimation of the actual video refresh, which in turn serves as the basis for audio resampling. This method has proven resilient to sudden performance drops, NTP-induced clock drift, and other real-time disturbances common in multitasking environments.

Although, as a user, you will probably never need to bother, event logging is of vital importance when debugging timing issues. Through the `-emusynclog -str` options, it is possible to collect a complete dump of in-game real-time statistics, which can later be processed in Python into customizable graphs for analysis. Real-world event debugging is also supported through the [optional serial port dongle](https://github.com/antonioginer/GroovyMAME/blob/emusync/3rdparty/emusync/emusync_adapter/README.md), enabled with the `-emusyncserial` option.

## Special Thanks

- To Substring, for his continued work on [GroovyArcade](https://gitlab.com/groovyarcade/os), the reference distro for GroovyMAME.
- To Oomek, for creating [GILT](https://github.com/oomek/GILT), that made all this possible.
