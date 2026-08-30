// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <libretro_state.h>

#include <common/algorithm.h>
#include <common/log.h>
#include <common/path.h>
#include <system/devices.h>

namespace eka2l1::libretro {
    bool emulator::bring_up(const std::string &data_root) {
        conf.deserialize();

        // The frontend's directory wins over whatever the config file carries:
        // a core does not get to keep state where it likes.
        conf.storage = data_root;

        app_settings = std::make_unique<config::app_settings>(&conf);

        system_create_components comp;
        comp.audio_ = nullptr;    // installed with the audio driver, later
        comp.graphics_ = nullptr; // installed with the graphics driver, later
        comp.conf_ = &conf;
        comp.settings_ = app_settings.get();

        symsys = std::make_unique<eka2l1::system>(comp);

        device_manager *dvcmngr = symsys->get_device_manager();
        if (dvcmngr->total() == 0) {
            // Not an error yet, and not something a core can fix on its own -
            // a device is built from a firmware dump the user has to provide.
            LOG_INFO(FRONTEND_CMDLINE, "No device installed under {}", data_root);
            return true;
        }

        symsys->startup();

        if (!symsys->set_device(conf.device)) {
            LOG_ERROR(FRONTEND_CMDLINE, "Device index {} is out of range; falling back to the first installed device", conf.device);
            conf.device = 0;
            symsys->rescan_devices(drive_z);
            symsys->set_device(0);
        }

        symsys->mount(drive_c, drive_media::physical, eka2l1::add_path(conf.storage, "/drives/c/"), io_attrib_internal);
        symsys->mount(drive_d, drive_media::physical, eka2l1::add_path(conf.storage, "/drives/d/"), io_attrib_internal);
        symsys->mount(drive_e, drive_media::physical, eka2l1::add_path(conf.storage, "/drives/e/"), io_attrib_removeable);

        device *dvc = dvcmngr->get_current();
        if (dvc) {
            LOG_INFO(FRONTEND_CMDLINE, "Device: {} ({})", dvc->model, dvc->firmware_code);
        }

        return true;
    }

    std::size_t emulator::device_count() const {
        if (!symsys) {
            return 0;
        }
        return symsys->get_device_manager()->total();
    }

    void emulator::shut_down() {
        symsys.reset();
        app_settings.reset();
    }
}
