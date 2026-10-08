// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <config/app_settings.h>
#include <config/config.h>
#include <drivers/graphics/backend/emu_window_libretro.h>
#include <drivers/graphics/graphics.h>
#include <system/epoc.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <functional>
#include <string>
#include <thread>

namespace eka2l1::libretro {
    // The emulator, as a core sees it.
    //
    // The android and qt frontends keep the same handful of objects; this is
    // that, minus everything a core does not own. There is no window here yet
    // and no threads: bringing the emulator up and finding out whether it has a
    // device to run on comes first, because without one there is nothing to
    // start.
    struct emulator {
        std::unique_ptr<eka2l1::system> symsys;
        std::unique_ptr<config::app_settings> app_settings;
        config::state conf;

        // Everything the emulator writes - configuration, installed devices,
        // the virtual drives - lives under one directory the frontend gives
        // us. Nothing is written beside the core or the content.
        bool bring_up(const std::string &data_root);

        // How many devices are installed. A Symbian title runs on a device
        // built from a firmware dump, so zero means there is nothing to run,
        // however good the rest of the core is.
        std::size_t device_count() const;

        // Install a device from a firmware dump if none is installed and one is
        // lying in the directory the core looks in. A core cannot run an
        // installation wizard; this is the whole of what it can do instead.
        bool install_device_if_needed(const std::string &firmware_dir);

        // A title: a package to install and launch, or a shortcut naming
        // something already installed. See CONTENT_MODEL.md for why those two
        // and not a path to "the game".
        bool load_content(const std::string &path);

        // Start the emulator: its graphics driver, on the calling thread - the
        // frontend's, where its GL context is current, the only thread GL may
        // be used from - and its Symbian OS thread. The OS thread is not
        // frame-stepped (EKA2L1 has no "run one frame" call); it submits
        // graphics command lists, which run_frame processes.
        bool start(std::function<unsigned int()> framebuffer_getter);

        // On the frontend's thread, in retro_run: processes the command lists
        // the OS thread submits until the emulator presents a frame, or until
        // the timeout - a title that draws nothing must not take the frontend
        // down with it. Returns false on the timeout.
        bool run_frame();

        // Stops the OS thread and frees the graphics driver; on the frontend's
        // thread, while its context is still there.
        void shut_down();

        drivers::emu_window_libretro *window() { return window_.get(); }

    private:
        bool install_package_and_launch(const std::string &path);
        bool launch_uid(std::uint32_t uid);

        // What was installed from where, so the same file is not installed
        // again on every launch: a package's UID cannot be read without
        // installing it, so the answer is remembered rather than asked for.
        std::string index_path() const;
        std::uint32_t remembered_uid(const std::string &path) const;
        void remember_uid(const std::string &path, std::uint32_t uid);

        std::string data_root_;

        void os_thread_main();

        std::unique_ptr<drivers::emu_window_libretro> window_;
        std::unique_ptr<drivers::graphics_driver> graphics_driver_;

        std::unique_ptr<std::thread> os_thread_;

        std::atomic<bool> should_quit_{false};

        // Set by the display hook, which runs inside run_frame
        bool frame_ready_ = false;
    };
}
