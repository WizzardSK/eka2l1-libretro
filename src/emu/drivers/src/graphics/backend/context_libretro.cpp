// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <drivers/graphics/backend/context_libretro.h>

namespace eka2l1::drivers::graphics {
    std::function<unsigned int()> gl_context_libretro::s_framebuffer_getter;
    gl_context_libretro::proc_address_getter gl_context_libretro::s_proc_address_getter = nullptr;

    // The context the core asks the frontend for: GLES on Android, desktop GL
    // elsewhere. Said here, as the GL backend loads the functions by it.
    gl_context_libretro::gl_context_libretro(const window_system_info &info, bool stereo, bool core) {
#ifdef __ANDROID__
        m_opengl_mode = mode::opengl_es;
#else
        m_opengl_mode = mode::opengl;
#endif
        m_is_shared = false;
    }

    void gl_context_libretro::set_proc_address_getter(proc_address_getter getter) {
        s_proc_address_getter = getter;
    }

    void *gl_context_libretro::get_proc_address(const char *name) {
        return s_proc_address_getter ? reinterpret_cast<void *>(s_proc_address_getter(name)) : nullptr;
    }

    // The frontend makes its context current on the thread it calls the core
    // from, which is where the GL backend runs. Claiming or releasing it here
    // would be taking something that is not ours.
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
