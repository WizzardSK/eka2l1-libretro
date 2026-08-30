// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <libretro_state.h>

#include <common/log.h>
#include <common/thread.h>
#include <drivers/graphics/backend/context_libretro.h>

#include <chrono>

namespace eka2l1::libretro {
    // EKA2L1 runs its own two threads and has no "run one frame" entry point,
    // so a core cannot drive it a frame at a time. It waits for it instead:
    // the graphics driver's display hook fires when a frame has been drawn,
    // retro_run wakes on it, and the emulator carries on. The same shape the
    // android frontend has, with presentation taken out - the frontend owns
    // that.
    bool emulator::start(std::function<unsigned int()> framebuffer_getter) {
        if (!symsys) {
            return false;
        }

        if (graphics_thread_ || os_thread_) {
            return true;
        }

        should_quit_.store(false);

        graphics_thread_ = std::make_unique<std::thread>([this, framebuffer_getter]() {
            graphics_thread_main(framebuffer_getter);
        });

        os_thread_ = std::make_unique<std::thread>([this]() {
            os_thread_main();
        });

        return true;
    }

    void emulator::graphics_thread_main(std::function<unsigned int()> framebuffer_getter) {
        common::set_thread_name("EKA2L1 graphics");

        // Where "the screen" is. The context does not create anything - the
        // frontend's is already current on this thread - it only answers with
        // the framebuffer the frontend wants drawn into, per frame.
        drivers::graphics::gl_context_libretro::set_framebuffer_getter(framebuffer_getter);

        window_ = std::make_unique<drivers::emu_window_libretro>();
        window_->init("EKA2L1", eka2l1::vec2(0, 0), 0);

        graphics_driver_ = drivers::create_graphics_driver(drivers::graphic_api::opengl,
            window_->get_window_system_info());

        if (!graphics_driver_) {
            LOG_ERROR(FRONTEND_CMDLINE, "Could not create the graphics driver");
            return;
        }

        symsys->set_graphics_driver(graphics_driver_.get());

        graphics_driver_->set_display_hook([this]() {
            // A frame has been drawn into the frontend's framebuffer. Wake
            // retro_run, and wait until it has been presented before letting
            // the emulator draw into the same framebuffer again.
            std::unique_lock lock(frame_mutex_);
            frame_ready_ = true;
            frame_cv_.notify_all();

            frame_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !frame_ready_ || should_quit_.load();
            });
        });

        // Blocks, processing graphics commands, until the driver is aborted.
        graphics_driver_->run();

        symsys->set_graphics_driver(nullptr);
        graphics_driver_.reset();
        window_.reset();
    }

    void emulator::os_thread_main() {
        common::set_thread_name("EKA2L1 Symbian OS");

        while (!should_quit_.load()) {
            symsys->loop();
        }
    }

    bool emulator::wait_for_frame() {
        std::unique_lock lock(frame_mutex_);

        // A title that draws nothing must not take the frontend down with it:
        // retro_run has to return either way, and a frontend that gets no frame
        // simply repeats the last one.
        const bool got_frame = frame_cv_.wait_for(lock, std::chrono::milliseconds(100), [this]() {
            return frame_ready_;
        });

        if (got_frame) {
            frame_ready_ = false;
            frame_cv_.notify_all();
        }

        return got_frame;
    }

    void emulator::shut_down() {
        should_quit_.store(true);

        {
            std::lock_guard lock(frame_mutex_);
            frame_ready_ = false;
        }
        frame_cv_.notify_all();

        if (graphics_driver_) {
            graphics_driver_->abort();
        }

        if (os_thread_ && os_thread_->joinable()) {
            os_thread_->join();
        }

        if (graphics_thread_ && graphics_thread_->joinable()) {
            graphics_thread_->join();
        }

        os_thread_.reset();
        graphics_thread_.reset();

        symsys.reset();
        app_settings.reset();
    }
}
