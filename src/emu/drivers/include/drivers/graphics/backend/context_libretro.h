// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <drivers/graphics/context.h>

#include <cstdint>
#include <functional>

namespace eka2l1::drivers::graphics {
    // The context a libretro frontend already made.
    //
    // Everything a normal backend does at startup - choose a config, create a
    // context, bind it to a surface, present it - the frontend has done, and it
    // makes the context current before it calls into the core. So this class
    // creates nothing and presents nothing.
    //
    // The one thing it has to answer is where "the screen" is. A frontend does
    // not render to FBO 0; it hands the core a framebuffer object per frame,
    // and that is exactly the case gl_context::swapchain_framebuffer() was
    // added for - iOS has no default framebuffer either. The libretro frontend
    // installs a getter, and every bind of handle 0 in the GL backend lands in
    // the frontend's own framebuffer.
    class gl_context_libretro final : public gl_context {
    public:
        explicit gl_context_libretro(const window_system_info &info, bool stereo = false, bool core = true);

        bool make_current() override;
        bool clear_current() override;

        void swap_buffers() override;
        void update(const std::uint32_t new_width, const std::uint32_t new_height) override;
        void set_swap_interval(const std::int32_t interval) override;

        bool is_headless() const override;

        std::unique_ptr<gl_context> create_shared_context() override;

        unsigned int swapchain_framebuffer() const override;

        // Set by the libretro frontend before the graphics driver starts. It
        // has to be called per frame rather than cached: the frontend is
        // allowed to hand over a different framebuffer each time.
        static void set_framebuffer_getter(std::function<unsigned int()> getter);

    private:
        static std::function<unsigned int()> s_framebuffer_getter;
    };
}
