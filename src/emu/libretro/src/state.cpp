// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <libretro_state.h>

#include <common/algorithm.h>
#include <common/cvt.h>
#include <common/fileutils.h>
#include <common/log.h>
#include <common/path.h>
#include <package/manager.h>
#include <system/devices.h>
#include <vfs/vfs.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <tuple>
#include <vector>

namespace eka2l1::libretro {
    struct embedded_file {
        const char *path;
        const unsigned char *data;
        std::size_t size;
    };

    extern const embedded_file embedded_files[];
    extern const std::size_t embedded_file_count;
}

namespace eka2l1::libretro {
    void emulator::unpack_resources() {
        for (std::size_t i = 0; i < embedded_file_count; i++) {
            const embedded_file &file = embedded_files[i];
            const std::string target = eka2l1::add_path(data_root_, file.path);

            // Written again only when it differs: a newer core brings newer
            // files, and an unchanged one touches nothing.
            bool same = false;
            {
                std::ifstream existing(target, std::ios::binary | std::ios::ate);
                if (existing.is_open() && (static_cast<std::size_t>(existing.tellg()) == file.size)) {
                    std::vector<char> content(file.size);
                    existing.seekg(0);
                    existing.read(content.data(), static_cast<std::streamsize>(file.size));
                    same = existing.good() && (std::memcmp(content.data(), file.data, file.size) == 0);
                }
            }

            if (same) {
                continue;
            }

            common::create_directories(eka2l1::file_directory(target));

            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                LOG_ERROR(FRONTEND_CMDLINE, "Could not write {}", target);
                continue;
            }
            out.write(reinterpret_cast<const char *>(file.data), static_cast<std::streamsize>(file.size));
        }
    }

    void emulator::finish_bring_up() {
        if (brought_up_ || !symsys) {
            return;
        }

        device_manager *dvcmngr = symsys->get_device_manager();
        if (!dvcmngr->get_current()) {
            LOG_ERROR(FRONTEND_CMDLINE, "No current device; the system is not brought up");
            return;
        }

        // The ROM drive, after the ROM was loaded (set_device)
        symsys->mount(drive_z, drive_media::rom,
            eka2l1::add_path(conf.storage, "/drives/z/"), io_attrib_internal | io_attrib_write_protected);

        // The patch libraries, the functions the emulator provides to the
        // guest, the services that start after boot, and the boot itself.
        // Without this an application starts into a system that is not
        // there: SSX3 died in its Main thread reading address 0x550 (gouchi).
        symsys->initialize_user_parties();

        // Libraries that replace the firmware's own from a Symbian version on
        io_system *io = symsys->get_io_system();
        const std::vector<std::tuple<std::u16string, std::string, epocver>> replacements = {
            { u"Z:\\sys\\bin\\goommonitor.dll", "patch/goommonitor_general.dll", epocver::epoc94 },
            { u"Z:\\sys\\bin\\avkonfep.dll", "patch/avkonfep_general.dll", epocver::epoc93fp1 }
        };

        for (const auto &[guest_path, patch_path, needed] : replacements) {
            if (symsys->get_symbian_version_use() < needed) {
                continue;
            }

            auto host_path = io->get_raw_path(guest_path);
            if (!host_path.has_value()) {
                continue;
            }

            const std::string target = common::ucs2_to_utf8(host_path.value());
            const std::string backup = target + ".bak";
            if (common::exists(target) && !common::exists(backup)) {
                common::move_file(target, backup);
            }
            common::copy_file(eka2l1::add_path(data_root_, patch_path), target, true);
        }

        manager::packages *pkgmngr = symsys->get_packages();
        pkgmngr->load_registries();
        pkgmngr->migrate_legacy_registries();

        brought_up_ = true;
    }

    bool emulator::bring_up(const std::string &data_root) {
        data_root_ = data_root;
        unpack_resources();
        eka2l1::set_runtime_resource_root(data_root);

        conf.deserialize();

        // The frontend's directory wins over whatever the config file carries:
        // a core does not get to keep state where it likes.
        conf.storage = data_root;
        data_root_ = data_root;

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

}
