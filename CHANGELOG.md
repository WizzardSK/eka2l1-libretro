# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream EKA2L1 are in upstream's own history; a merge is noted here with the upstream version it brought in.

## Unreleased

- Linux: the core exports only the libretro API (a version script); it exported some thirty thousand of its own symbols, which could clash with libraries the frontend loads.
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
