// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <common/vecx.h>
#include <drivers/graphics/emu_window.h>

#include <atomic>
#include <cstdint>

namespace eka2l1::drivers {
    // There is no window here at all: a libretro frontend owns the surface, the
    // context and the presentation, and hands the core a framebuffer to draw
    // into once per frame. So this is what emu_window_android already is - a
    // holder for the geometry the frontend last reported, with the rest of the
    // interface answered rather than implemented.
    //
    // Input does not come through here either; the frontend polls it and the
    // core forwards it, the same way the Android build bypasses emu_window.
    class emu_window_libretro final : public emu_window {
    public:
        emu_window_libretro();

        // Frontend hook: the geometry the frontend is rendering at, which can
        // change when the user resizes the window or rotates the screen.
        void surface_changed(int pixel_width, int pixel_height);

        void init(std::string title, vec2 size, const std::uint32_t flags) override;
        void poll_events() override;
        void shutdown() override;
        void set_fullscreen(const bool is_fullscreen) override;
        bool should_quit() override;
        void change_title(std::string new_title) override;

        vec2 window_size() override;
        vec2 window_fb_size() override;
        vec2d get_mouse_pos() override;
        bool get_mouse_button_hold(const int mouse_btt) override;

        void set_userdata(void *userdata) override;
        void *get_userdata() override;

        bool set_cursor(cursor *cur) override;
        void cursor_visiblity(const bool visi) override;
        bool cursor_visiblity() override;

        window_system_info get_window_system_info() override;

        void request_quit();

    private:
        void *userdata_ = nullptr;
        vec2 fb_size_ = vec2(0, 0);
        std::atomic<bool> should_quit_{false};
    };
}
