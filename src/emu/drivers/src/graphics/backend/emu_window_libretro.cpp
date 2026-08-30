// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <drivers/graphics/backend/emu_window_libretro.h>

namespace eka2l1::drivers {
    emu_window_libretro::emu_window_libretro() = default;

    void emu_window_libretro::surface_changed(int pixel_width, int pixel_height) {
        if ((pixel_width > 0) && (pixel_height > 0)) {
            fb_size_.x = pixel_width;
            fb_size_.y = pixel_height;
        }

        if (surface_change_hook) {
            // No surface to hand over - the frontend owns it. The hook exists
            // so the graphics driver knows to pick up the new geometry.
            surface_change_hook(nullptr);
        }
    }

    // Everything the frontend owns, answered and not implemented -----------

    void emu_window_libretro::init(std::string title, vec2 size, const std::uint32_t flags) {
        if ((size.x > 0) && (size.y > 0)) {
            fb_size_ = size;
        }
    }

    void emu_window_libretro::poll_events() {}
    void emu_window_libretro::shutdown() {}
    void emu_window_libretro::set_fullscreen(const bool is_fullscreen) {}
    void emu_window_libretro::change_title(std::string new_title) {}

    bool emu_window_libretro::should_quit() {
        return should_quit_.load();
    }

    void emu_window_libretro::request_quit() {
        should_quit_.store(true);
    }

    vec2 emu_window_libretro::window_size() {
        return fb_size_;
    }

    vec2 emu_window_libretro::window_fb_size() {
        return fb_size_;
    }

    // Pointer input reaches the emulator through the frontend, not from here.
    vec2d emu_window_libretro::get_mouse_pos() {
        return vec2d{ 0.0, 0.0 };
    }

    bool emu_window_libretro::get_mouse_button_hold(const int mouse_btt) {
        return false;
    }

    void emu_window_libretro::set_userdata(void *userdata) {
        userdata_ = userdata;
    }

    void *emu_window_libretro::get_userdata() {
        return userdata_;
    }

    bool emu_window_libretro::set_cursor(cursor *cur) {
        return false;
    }

    void emu_window_libretro::cursor_visiblity(const bool visi) {}

    bool emu_window_libretro::cursor_visiblity() {
        return false;
    }

    window_system_info emu_window_libretro::get_window_system_info() {
        // Headless in the sense the graphics backend cares about: there is no
        // native window or display connection for it to create a context from,
        // because the frontend already made one.
        window_system_info info;
        info.type = window_system_type::headless;
        info.render_surface_scale = 1.0f;
        return info;
    }
}
