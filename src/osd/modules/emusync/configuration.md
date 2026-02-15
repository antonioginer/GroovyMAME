## Synchronization

**-[no]syncrefresh** / **-srf** _(reimplemented)_

Enable speed throttling tied strictly to your monitor’s refresh rate. This adjusts the emulation speed to match the display refresh, allowing smooth, stutter-free, tear-free, video output. This option is ideally used in combination with **-syncaudio**.

This option enables V-Sync through _emusync_, which handles raster synchronization directly.

The default is OFF (this option is preferably managed through **-autosync**)

**-[no]syncrefresh** / **-srf**
___

**-[no]waitvsync** _(reimplemented)_

This option enables V-Sync and delagates it to the graphics API, for tear-free video output. Contrary to **-syncrefresh**, this option keeps the emulation speed unmodified. This means there will exist a certain amount of video stutter.

If **-syncrefresh** is enabled, it will override **-waitvsync**.

The default is OFF (this option is preferably managed through **-autosync**)
___

**-[no]autosync**

Automatically enable **-syncrefresh** if the refresh difference is below or equal the **-syncrefresh_tolerance**'s value. Otherwise, enable **-waitsync** and throttle the emulation at the system's original speed.

| -autosync | -syncrefresh_tolerance _<value\>_ | effect |
| :---: | :---: | :---: |
| 0 | - | no action taken on syncrefresh / waitvsync |
| 1 | _refresh_diff_in_Hz_ ≤ _value_ | -syncrefresh (-nothrottle) |
| 1 | _refresh_diff_in_Hz_ > _value_ | -nosyncrefresh -waitvsync (-throttle) |

_refresh_diff_in_Hz_ = difference between the emulated system's refresh rate and the host's monitor refresh, in Hz (absolute value).

The default is ON (**-autosync**).
___

**-syncrefresh_tolerance** _<value\>_

Maximum refresh difference, in Hz, allowed in order to synchronize to the monitor's refresh. Used by **-autosync** to decide whether it should apply **-syncrefresh** or **-waitvsync**.

The default is `2.0` (2 Hz).

Example (assuming a typical fixed monitor's refresh of 60 Hz):

`mame sf2 -autosync -syncrefresh_tolerance 2.0` &#8594; **-syncrefresh**; average speed 101% (60 vs 59.63 Hz)

`mame rtype -autosync -syncrefresh_tolerance 2.0` &#8594; **-waitvsync**; average speed 100% (60 vs 55.01 Hz)

`mame rtype -autosync -syncrefresh_tolerance 5.0` &#8594; **-syncrefresh**; average speed 109% (60 vs 55.01 Hz)
___

**-[no]syncaudio**

Enable audio resampling to stay synchronized with video. The resampling factor is adjusted dynamically by _emusync_. It performs cosine resampling on the final mix.

If **-syncrefresh** is enabled (either manually or through **-autosync**), it is recommended to keep this option enabled too, in order to minimize audio glitches.

The default is ON (**-syncaudio**).
___

**-framedelay** / **-fd** _<n\>_

Delay emulation of each frame to minimize input latency (0-9). The emulation is delayed by _n_ tenths of a frame period (not milliseconds!).

| -framedelay | -autoframedelay | effect |
| :---: | :---: | :---: |
| 0 | 0 | disabled |
| 0 | 1 | automatic |
| 1-9 | - | manual |

The default is 0.

_For convenience, a UI slider is provided to allow for persistent per-game settings._
___

**-[no]autoframedelay** / **-afd**

Enable automatic framedelay if **-framedelay** is set to 0. This adjusts framedelay dynamically based on the recent emulation times average. The current value is shown along with the FPS display by pressing [F11].

The default is ON (**-autoframedelay**).
___

**-fd_margin** _<value\>_

Reserve a margin of _<value\>_ milliseconds to account for variability in frame emulation time when automatic framedelay is enabled.

* Increase **-fd_margin** to reduce glitches.
* Decrease **-fd_margin** to reduce latency.

The default is `1.0`. (1 ms)

_For convenience, a UI slider is provided to allow for persistent per-game settings._
___

**-vsync_offset** _<n\>_

Offset vblank position by _<n\>_ lines to prevent tearing.

* Negative _n_ : syncs before real vblank &#8594; moves tear line upwards, hiding it at the top of screen.
* Positive _n_ : syncs after real vblank &#8594; moves tear line downwards, hiding it at bottom of screen tearing.

The default is `0`.

_For convenience, a UI slider is provided to allow for persistent per-game settings._

Example:

`mame sf2 -vsync_offset -24`
___

**[no]-tearbar** / **-tb**

Show a scrolling vertical bar to expose and detect tearing. When enabled, a green bar is shown along the FPS diplay by pressing [F11].

The default is ON. (**-tearbar**)
___

**-[no]vblank_thread** / **-vbt**

Use a background thread to register vblank timestamps.

Without this setting, vblank timestamps are queried from OS-specific APIs: _GetFrameStatistics_ on Windows/D3D and _drmCrtcGetSequence_ on Linux, which is generally preferred. When the video backend does not provide these facilities (on Windows: `-video bgfx`, `-video opengl`), this feature is enabled automatically regardless of `-vblank_thread`.

`-vblank_thread` is provided to allow forcing this feature optionally, replacing the default method, in situations where the default method yields inaccurate timestamps.

The default is OFF. (**-novblank_thread**)
___

**-[no]black_frame_insertion** _<n\>_ / **-bfi** _<n\>_

Insert _n_ black frames after each normal frame. Intended to reduce motion blur on 120+ Hz monitors when each original frame is  scanned more than once. The resulting emulation speed is the monitor's refresh rate divided by _n + 1_.

Inserting a black frame halves the normal brightness of the screen. Check **-bfi_brightness**.

Black frame insertion is very sensitive to system's performance. It requires a very stable environment to avoid nasty screen flashing. Use only in combination with **-nosleep**.

Example:

`mame alexkidd -monitor pc_31_120 -srf -bfi 1 -nosleep`

The default is `0`.
___

**-bfi_brightness** _<factor\>_

Adjust brightness for black frame insertion, where _factor_ (0.0-1.0) is applied to the normal screen brightness, so `0.0` is fully dark and `1.0` keeps the normal brightness unmodified. It is used in combination with **-black_frame_insertion** to reduce the darkening of the screen caused by that option, while preserving part of the blur reduction.

Example:

`mame alexkidd -monitor pc_31_120 -srf -bfi 1 -bfi_brightness 0.5 -nosleep`

The default is `0.0`
___

## Scaling and filtering

**-[no]autostretch**

Automatically set scaling mode (integer or fractional) based on the selected video mode. This option ensures that the configuration that Switchres considers ideal for the video mode it has computed or selected is correctly translated into MAME options.

| Switchres condition | action |
| :--- | :--- |
| emulated screen aspect &#8800; monitor aspect  | -keepaspect |
| video mode selected for integer scaling  | -nounevenstretch |
| video mode selected for fractional scaling | -unevenstretch |
| video mode is a super-resolution\* | -nounevenstretch -unevenstretchx |

_\*A super-resolution is any resolution which width is equal or bigger than **-super_width** (2560 by default)._

Default is ON. (**-autostretch**)
___

**-[no]autofilter**

Automatically set bilinear filtering when the resulting scaling mode is fractional stretching or the selected video mode is interlaced. This attempts to smooth out the aliasing caused by fractional scaling and the flicker introduced by interlace.

| Switchres condition | action |
| :--- | :--- |
| video mode selected for integer scaling  | -nofilter |
| video mode selected for fractional scaling | -filter |
| video mode is interlaced | -filter |

Exclusively for the **-video d3d11** renderer, **-autofilter** enables a shader-based smart filter that performs axis-independent pixel interpolation, particularly useful for super-resolution scaling.

Default is ON. (**-autosfilter**)
