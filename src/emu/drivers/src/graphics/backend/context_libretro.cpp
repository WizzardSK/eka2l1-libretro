// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "context_libretro.h"

namespace eka2l1::drivers::graphics {
    std::function<unsigned int()> gl_context_libretro::s_framebuffer_getter;

    gl_context_libretro::gl_context_libretro(const window_system_info &info, bool stereo, bool core) {
        m_opengl_mode = mode::detect;
        m_is_shared = false;
    }

    // The frontend makes its context current on the thread it calls the core
    // from, and takes it back afterwards. Claiming or releasing it here would
    // be taking something that is not ours.
    bool gl_context_libretro::make_current() {
        return true;
    }

    bool gl_context_libretro::clear_current() {
        return true;
    }

    // The frontend presents. All that is left of a swap here is the moment it
    // happens, which the graphics driver already reports through its display
    // hook - that is where retro_run picks the frame up.
    void gl_context_libretro::swap_buffers() {}

    void gl_context_libretro::update(const std::uint32_t new_width, const std::uint32_t new_height) {
        m_backbuffer_width = new_width;
        m_backbuffer_height = new_height;
    }

    // Pacing is the frontend's, and asking for a swap interval on a context we
    // do not own would not mean anything.
    void gl_context_libretro::set_swap_interval(const std::int32_t interval) {}

    bool gl_context_libretro::is_headless() const {
        return false;
    }

    // No second context: a core gets the one the frontend made.
    std::unique_ptr<gl_context> gl_context_libretro::create_shared_context() {
        return nullptr;
    }

    unsigned int gl_context_libretro::swapchain_framebuffer() const {
        return s_framebuffer_getter ? s_framebuffer_getter() : 0;
    }

    void gl_context_libretro::set_framebuffer_getter(std::function<unsigned int()> getter) {
        s_framebuffer_getter = std::move(getter);
    }
}
