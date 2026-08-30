// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <config/app_settings.h>
#include <config/config.h>
#include <system/epoc.h>

#include <memory>
#include <string>

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

        void shut_down();
    };
}
