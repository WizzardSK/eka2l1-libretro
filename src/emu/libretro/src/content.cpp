// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Turning what a frontend hands over into something Symbian can run. See
// CONTENT_MODEL.md next to this file for why that is three steps rather than
// one, and which parts of it are still open questions.

#include <libretro_state.h>

#include <common/algorithm.h>
#include <common/log.h>
#include <common/path.h>
#include <common/pystr.h>
#include <package/manager.h>
#include <services/applist/applist.h>
#include <system/devices.h>
#include <system/installation/firmware.h>
#include <system/installation/rpkg.h>
#include <utils/apacmd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace eka2l1::libretro {
    namespace {
        std::string lowercase_extension(const std::string &path) {
            const std::string ext = eka2l1::path_extension(path);
            std::string lowered = ext;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lowered;
        }

        bool is_package(const std::string &path) {
            const std::string ext = lowercase_extension(path);
            return (ext == ".sis") || (ext == ".sisx") || (ext == ".n-gage");
        }
    }

    bool emulator::install_device_if_needed(const std::string &firmware_dir) {
        if (device_count() > 0) {
            return true;
        }

        std::error_code ec;
        if (!fs::is_directory(firmware_dir, ec)) {
            LOG_INFO(FRONTEND_CMDLINE, "No firmware directory at {} - a device has to be installed before anything can run", firmware_dir);
            return false;
        }

        // What a dump looks like varies: a .vpl beside its parts, or a ROM
        // image, sometimes needing an RPKG next to it. Take the first of each
        // that turns up rather than asking the user to choose, since there is
        // nothing to ask with.
        std::string vpl_path, rom_path, rpkg_path;

        for (const auto &entry : fs::directory_iterator(firmware_dir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }

            const std::string path = entry.path().string();
            const std::string ext = lowercase_extension(path);

            if ((ext == ".vpl") && vpl_path.empty()) {
                vpl_path = path;
            } else if (((ext == ".rom") || (ext == ".fpsx")) && rom_path.empty()) {
                rom_path = path;
            } else if ((ext == ".rpkg") && rpkg_path.empty()) {
                rpkg_path = path;
            }
        }

        if (vpl_path.empty() && rom_path.empty()) {
            LOG_INFO(FRONTEND_CMDLINE, "Nothing to install in {} - expected a firmware .vpl, or a .rom (with its .rpkg beside it)", firmware_dir);
            return false;
        }

        device_manager *dvcmngr = symsys->get_device_manager();

        const std::string root_c = eka2l1::add_path(conf.storage, "drives/c/");
        const std::string root_e = eka2l1::add_path(conf.storage, "drives/e/");
        const std::string root_z = eka2l1::add_path(conf.storage, "drives/z/");
        const std::string roms = eka2l1::add_path(conf.storage, "roms/");

        eka2l1::common::create_directories(roms);

        device_installation_error result = device_installation_general_failure;

        if (!vpl_path.empty()) {
            LOG_INFO(FRONTEND_CMDLINE, "Installing device from {}", vpl_path);
            result = eka2l1::install_firmware(dvcmngr, vpl_path, root_c, root_e, root_z, roms,
                [](const std::vector<std::string> &variants) -> int { return 0; }, nullptr, nullptr);
        } else {
            LOG_INFO(FRONTEND_CMDLINE, "Installing device from {}", rom_path);
            result = eka2l1::loader::install_rom_with_optional_rpkg(dvcmngr, rom_path, rpkg_path, roms, root_z, nullptr, nullptr);
        }

        if (result != device_installation_none) {
            LOG_ERROR(FRONTEND_CMDLINE, "Device installation failed (error {})", static_cast<int>(result));
            return false;
        }

        dvcmngr->save_devices();
        symsys->startup();
        symsys->set_device(0);

        LOG_INFO(FRONTEND_CMDLINE, "Device installed");
        return true;
    }

    // The index: one line per package, "<uid> <size> <path>". Small, readable,
    // and repairable with a text editor when it inevitably goes stale.
    std::string emulator::index_path() const {
        return eka2l1::add_path(data_root_, "libretro-content.txt");
    }

    std::uint32_t emulator::remembered_uid(const std::string &path) const {
        std::ifstream index(index_path());
        if (!index.is_open()) {
            return 0;
        }

        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);

        std::string line;
        while (std::getline(index, line)) {
            std::istringstream parts(line);
            std::uint32_t uid = 0;
            std::uintmax_t recorded_size = 0;
            std::string recorded_path;

            parts >> std::hex >> uid >> std::dec >> recorded_size;
            std::getline(parts, recorded_path);

            if (!recorded_path.empty() && (recorded_path.front() == ' ')) {
                recorded_path.erase(0, 1);
            }

            // The size guards against a file replaced in place - a different
            // build of the same game under the same name is a different thing.
            if ((recorded_path == path) && (recorded_size == size)) {
                return uid;
            }
        }

        return 0;
    }

    void emulator::remember_uid(const std::string &path, std::uint32_t uid) {
        std::error_code ec;
        const std::uintmax_t size = fs::file_size(path, ec);

        std::ofstream index(index_path(), std::ios::app);
        if (index.is_open()) {
            index << std::hex << uid << " " << std::dec << size << " " << path << "\n";
        }
    }

    bool emulator::launch_uid(std::uint32_t uid) {
        kernel_system *kern = symsys->get_kernel_system();
        if (!kern) {
            return false;
        }

        auto *alserv = reinterpret_cast<eka2l1::applist_server *>(
            kern->get_by_name<service::server>(get_app_list_server_name_by_epocver(kern->get_epoc_version())));

        if (!alserv) {
            LOG_ERROR(FRONTEND_CMDLINE, "The application list server is not up; cannot launch {:08x}", uid);
            return false;
        }

        apa_app_registry *reg = alserv->get_registration(uid);
        if (!reg) {
            LOG_ERROR(FRONTEND_CMDLINE, "No application registered with UID {:08x}", uid);
            return false;
        }

        epoc::apa::command_line cmdline;
        cmdline.launch_cmd_ = epoc::apa::command_create;

        kern->lock();
        const bool launched = alserv->launch_app(*reg, cmdline, nullptr, nullptr);
        kern->unlock();

        if (!launched) {
            LOG_ERROR(FRONTEND_CMDLINE, "Failed to launch UID {:08x}", uid);
        }

        return launched;
    }

    bool emulator::install_package_and_launch(const std::string &path) {
        if (const std::uint32_t known = remembered_uid(path); known != 0) {
            LOG_INFO(FRONTEND_CMDLINE, "Already installed as {:08x}", known);
            return launch_uid(known);
        }

        manager::packages *pkgmngr = symsys->get_packages();
        if (!pkgmngr) {
            return false;
        }

        // Which UID appeared is how the installed application is identified: a
        // package's UID cannot be read without installing it, so the set is
        // compared before and after.
        const std::vector<manager::uid> before = pkgmngr->installed_uids();
        const std::set<manager::uid> before_set(before.begin(), before.end());

        const drive_number install_drive = symsys->is_s80_device_active() ? drive_d : drive_e;

        LOG_INFO(FRONTEND_CMDLINE, "Installing {}", path);

        if (symsys->install_package(common::utf8_to_ucs2(path), install_drive) != 0) {
            LOG_ERROR(FRONTEND_CMDLINE, "Installation failed for {}", path);
            return false;
        }

        std::uint32_t installed_uid = 0;
        for (const manager::uid uid : pkgmngr->installed_uids()) {
            if (before_set.find(uid) == before_set.end()) {
                installed_uid = uid;
                break;
            }
        }

        if (installed_uid == 0) {
            LOG_ERROR(FRONTEND_CMDLINE, "The package installed but registered no new application");
            return false;
        }

        LOG_INFO(FRONTEND_CMDLINE, "Installed as {:08x}", installed_uid);
        remember_uid(path, installed_uid);

        return launch_uid(installed_uid);
    }

    bool emulator::load_content(const std::string &path) {
        if (!symsys) {
            return false;
        }

        if (is_package(path)) {
            return install_package_and_launch(path);
        }

        // Otherwise a shortcut: a text file naming what is already installed.
        std::ifstream shortcut(path);
        if (!shortcut.is_open()) {
            LOG_ERROR(FRONTEND_CMDLINE, "Cannot open {}", path);
            return false;
        }

        std::string line;
        while (std::getline(shortcut, line)) {
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) {
                continue;
            }

            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);

            key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
            value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());

            if (key != "uid") {
                continue;
            }

            const std::uint32_t uid = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 0));
            if (uid == 0) {
                LOG_ERROR(FRONTEND_CMDLINE, "{} names an unreadable UID: {}", path, value);
                return false;
            }

            return launch_uid(uid);
        }

        LOG_ERROR(FRONTEND_CMDLINE, "{} is neither a package nor a shortcut naming a uid", path);
        return false;
    }
}
