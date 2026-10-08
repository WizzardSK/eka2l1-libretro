// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <libretro_state.h>

#include <common/log.h>
#include <common/thread.h>
#include <drivers/graphics/backend/context_libretro.h>

#include <chrono>

namespace eka2l1::libretro {
    // EKA2L1 runs its Symbian OS on a thread of its own and has no "run one
    // frame" entry point, so a core cannot step it a frame at a time. Its
    // graphics driver, though, only processes the command lists that thread
    // submits, and GL may only be used on the thread the frontend's context is
    // current on - so the driver lives there: created here (from
    // context_reset) and run from retro_run, which processes lists until the
    // emulator presents a frame.
    bool emulator::start(std::function<unsigned int()> framebuffer_getter) {
        if (!symsys) {
            return false;
        }

        if (graphics_driver_ || os_thread_) {
            return true;
        }

        should_quit_.store(false);
        frame_ready_ = false;

        // Where "the screen" is. The context does not create anything - the
        // frontend's is current on this thread - it only answers with the
        // framebuffer the frontend wants drawn into, per frame.
        drivers::graphics::gl_context_libretro::set_framebuffer_getter(framebuffer_getter);

        window_ = std::make_unique<drivers::emu_window_libretro>();
        window_->init("EKA2L1", eka2l1::vec2(0, 0), 0);

        graphics_driver_ = drivers::create_graphics_driver(drivers::graphic_api::opengl,
            window_->get_window_system_info());

        if (!graphics_driver_) {
            LOG_ERROR(FRONTEND_CMDLINE, "Could not create the graphics driver");
            window_.reset();
            return false;
        }

        // A frame has been drawn into the frontend's framebuffer: run_frame
        // stops there and hands it to the frontend.
        graphics_driver_->set_display_hook([this]() {
            frame_ready_ = true;
        });

        symsys->set_graphics_driver(graphics_driver_.get());

        os_thread_ = std::make_unique<std::thread>([this]() {
            os_thread_main();
        });

        return true;
    }

    bool emulator::run_frame() {
        if (!graphics_driver_) {
            return false;
        }

        frame_ready_ = false;

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);

        while (!frame_ready_ && !should_quit_.load()) {
            const auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) {
                break;
            }

            if (!graphics_driver_->run_once(static_cast<int>(left))) {
                break;
            }
        }

        return frame_ready_;
    }

    void emulator::os_thread_main() {
        common::set_thread_name("EKA2L1 Symbian OS");

        while (!should_quit_.load()) {
            symsys->loop();
        }
    }

    void emulator::shut_down() {
        should_quit_.store(true);

        // Aborted first: the OS thread may be waiting on the driver for a
        // command to finish, or for room in its queue, and returns from both
        // once it is aborted.
        if (graphics_driver_) {
            graphics_driver_->abort();
        }

        if (os_thread_ && os_thread_->joinable()) {
            os_thread_->join();
        }

        os_thread_.reset();

        if (symsys) {
            symsys->set_graphics_driver(nullptr);
        }

        // Its GL objects go with the frontend's context still current
        graphics_driver_.reset();
        window_.reset();

        symsys.reset();
        app_settings.reset();
    }
}
