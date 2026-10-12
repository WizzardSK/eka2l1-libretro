# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream EKA2L1 are in upstream's own history; a merge is noted here with the upstream version it brought in.

## Unreleased

- The phone's screen is shown: the core draws it into the frontend's framebuffer whenever the emulator has redrawn it, at the screen's own size, and tells the frontend that size when it changes (another device, or a title that turns the screen round). Until now nothing was drawn at all, so every title was a black screen.
- Controls: the phone's keys are on the RetroPad (d-pad, OK, the two soft keys, 5, 7, 0, *, #, the green and red keys, Clear and Menu), the number keys are on the keyboard as well, and the frontend's pointer (mouse or touch) is the phone's touch screen. There was no input.
- Core option "Device": which of the installed devices to run on. The core always used the first one.
- Sound: the core has an audio driver, which mixes the emulator's output streams for the frontend, each brought from its own rate and channel count to 44100 Hz stereo, one frame's worth per call. There was no sound at all.
- The system is brought up as the standalone does before content starts: the ROM drive is mounted, the patch libraries and the emulator's own functions are loaded, the after-boot services and the boot are started, and the package registries are read. Without it a game died in its main thread (gouchi, SSX 3).
- The shaders, the MIDI sound banks and the patch libraries are carried in the core and written under `system/eka2l1/` at start, so nothing has to be copied beside the firmware.
- The emulator's log goes to RetroArch's: warnings and errors from everywhere, and what installing and launching report. A package that would not install said only "Could not start" (gouchi).
- Linux: the core builds without the X11, OpenGL, Wayland, GTK, PulseAudio and ALSA development packages. It linked none of them; the GLX and Wayland contexts of the standalone are no longer compiled into it.
- The libretro buildbot recipe covers Windows x64 (cross-built on Linux with llvm-mingw, FFmpeg included), Android arm64-v8a and macOS arm64 and x86_64, besides Linux x86_64 and arm64.
- Windows Media Foundation player: a seek goes to the time asked for; every seek went to 0.
- Content behind a URI is read through the frontend's VFS: a `.sis`, `.sisx` or `.n-gage` picked from Android's storage access framework (`saf://`, the Play Store RetroArch) is installed, and `.eka2l1` shortcuts are read the same way.
- OpenGL runs on the frontend's thread with the functions the frontend gives out. The core crashed at start on Linux in the graphics driver, which had loaded no GL functions and ran on a thread where the frontend's context is not current (gouchi).
- Android builds are arm64-v8a only, as upstream's app.
- Linux and Android check TLS certificates with mbedtls against the system's authorities, in place of the Qt and Java code the core does not have.
- The repository holds the core only: upstream's Qt, Android and iOS apps, its tests and tools are removed (`.upstream-excluded`). The core reports upstream's version with the upstream commit it is based on (`upstream.version`, 11f5b6cd1).
- CI builds Linux x86_64 and arm64, Windows x86_64, macOS arm64 and x86_64, and Android arm64-v8a.
- SDL2 is not linked into the core; the frontend owns the window, the gamepad and the rumble.
- The core no longer picks up the system's compression libraries through FreeType, which kept it from loading where their names differ (a Flatpak RetroArch).
- The dialogs a title can ask its host for (text input, a yes/no question, opening a browser) are answered without a window: text input returns what the title already had, a question is answered with no, and the log says what was answered.
- Content: a `.sis`, `.sisx` or `.n-gage` is installed onto the device and launched; an `.eka2l1` shortcut launches an application that is installed already, by its UID. A package is installed once, remembered by name and size.
- A device is installed from `system/eka2l1/firmware/` the first time the core starts: a firmware `.vpl`, or a `.rom` with its `.rpkg` beside it. Without a device the core says so in the log and does not start the emulator.
- Everything the core writes is under `system/eka2l1/`: configuration, installed devices and the C, D and E drives.
- The first libretro core of EKA2L1: a fourth frontend beside upstream's Qt, Android and iOS ones, rendering with OpenGL (GLES on Android) into the frontend's framebuffer.
