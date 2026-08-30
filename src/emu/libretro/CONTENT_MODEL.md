# The content model for the libretro core

Everything else in this port had a precedent to follow — the window, the GL
context, the threads. This does not, because the shape of a Symbian title does
not fit the shape a frontend expects.

A frontend hands a core **one file** and expects a game to start. EKA2L1 needs
**three** things, in order: a device built from a firmware dump, an application
installed onto that device, and then a launch by application UID. Nothing about
that is a file the user points at.

This is a proposal, not a decision. What is settled is the problem; the parts
below marked *open* are the ones worth arguing about.

## The device

Without one nothing runs at all, and the core already refuses to start rather
than fail obscurely later.

A frontend cannot show an installation wizard, so the core should do what a
core can: look in one directory and install what it finds, once.

    <system>/eka2l1/firmware/     a firmware dump (RPKG, or ROM + ROFS)

On startup, if the device manager reports nothing installed and that directory
has contents, install from it and log what came out. If the directory is empty,
say so plainly — that message is the whole of the user's setup instructions.

This mirrors what a user of this core already does for BIOS files elsewhere,
which is worth more than being clever.

## The content

`retro_load_game` should accept two kinds of path.

**A package** — `.sis`, `.sisx`, `.n-gage`. Install it if it is not installed
already, then launch it.

**A shortcut** — a small text file naming an application UID, for launching
something already installed without keeping the installer around:

    uid: 0x2000abcd
    name: Pathway to Glory

The shortcut exists because a playlist wants one entry per game, and because
after the first install the package is dead weight. It is also the only way to
launch an application that arrived as part of the firmware or was installed by
some other means.

### The problem with "if it is not installed already"

`package::manager` can say whether a **UID** is installed (`installed(uid)`,
`installed_uids()`), and the applist server can enumerate registrations. What
there is no obvious API for is reading a package's UID *without installing it*.

So one of these has to be chosen:

1. **Install on every load.** Correct, obvious, and rewrites files on every
   launch of the same game — which on a phone is both slow and unkind to the
   storage.
2. **Keep an index in the core.** After a successful install, record the
   content path (with size and mtime, so a replaced file is noticed) against
   the UID that appeared, in `<system>/eka2l1/libretro-content.yml`. A second
   load of the same file then goes straight to the launch.
3. **Parse the package header** for its UID before installing. The cleanest of
   the three and the one that needs the most knowledge of the format; if
   EKA2L1 already does this internally, it should be exposed rather than
   duplicated.

*Open.* 2 is the pragmatic answer and 3 is the right one. 2 does not preclude 3.

## Starting without content

The core currently declares `SET_SUPPORT_NO_GAME`, which was convenient while
there was nothing to load. It should stop: with no title there is nothing to
show, EKA2L1 has no built-in application menu to fall back on — `drivers::ui`
is a bridge for the dialogs Symbian asks for, not a launcher — and every
frontend already has a file browser, which is a better one than a core could
draw.

## What this does not answer

* **Multiple screens.** Symbian devices vary; the N-Gage is 176x208. The
  geometry has to come from the emulated device once one is loaded, and
  `retro_get_system_av_info` currently reports a fixed guess.
* **Input.** A 12-key phone keypad and two soft keys onto a RetroPad, with the
  N-Gage's own button layout on top. Its own piece of work.
* **Saving.** Symbian applications write into their own private directories on
  the emulated drives; nothing about that maps to a frontend's save directory
  yet.
