# EKA2L1 libretro core

A [libretro](https://www.libretro.com/) core of [EKA2L1](https://github.com/EKA2L1/EKA2L1), the Symbian OS and N-Gage emulator, for RetroArch and other libretro frontends.

The emulator is upstream's: this repository follows EKA2L1's `master` and adds the libretro frontend in `src/emu/libretro`. It holds the core only; upstream's Qt, Android and iOS apps, its tests and tools are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is rebased on (`upstream.version`).

## Downloads

Builds for Windows, Linux (x86_64, arm64), macOS (Apple Silicon, Intel) and Android (arm64-v8a) are on the [Releases](https://github.com/WizzardSK/eka2l1-libretro/releases) page.

## Setup

The core needs a device made from a firmware dump of a Symbian phone or an N-Gage, which you have to dump yourself:

- Put the firmware (an RPKG, or a ROM with its ROFS images) into RetroArch's system directory, under `system/eka2l1/firmware/`. The core installs it the first time it starts.

## Content

- `.n-gage`, `.sis`, `.sisx`: the core installs the package onto the device and launches it.
- `.eka2l1`: a small text file naming an application that is installed already, as `uid: 0x<application UID>`.

How a Symbian title is mapped onto the single file a frontend hands a core is described in [src/emu/libretro/CONTENT_MODEL.md](src/emu/libretro/CONTENT_MODEL.md).

## Building

```
git clone --recursive https://github.com/WizzardSK/eka2l1-libretro.git
cd eka2l1-libretro
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCI=ON -DEKA2L1_SCRIPTING_LUA=OFF
cmake --build build --target eka2l1_libretro
```

The core is `build/bin/eka2l1_libretro.so` (`.dll`, `.dylib`). The CI workflow `.github/workflows/libretro.yml` builds every platform.

## License

GPL-3.0, as EKA2L1. See [LICENSE](LICENSE).
