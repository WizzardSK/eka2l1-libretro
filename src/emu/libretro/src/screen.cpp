// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The phone's screen and its keys.
//
// The window server composes each screen into a texture of its own and tells
// whoever asked when it has redrawn it; putting that texture somewhere a
// person can see is the frontend's part, as it is in the android and qt
// frontends this follows. Here "somewhere" is the frontend's framebuffer, at
// the screen's own size - scaling it up is RetroArch's job.

#include <libretro_state.h>

#include <common/log.h>
#include <drivers/graphics/common.h>
#include <drivers/input/common.h>
#include <drivers/itc.h>
#include <kernel/kernel.h>
#include <services/window/screen.h>
#include <services/window/window.h>

#include <algorithm>

namespace eka2l1::libretro {
    void emulator::register_screen_callbacks() {
        window_server *winserv = reinterpret_cast<window_server *>(symsys->get_kernel_system()->get_by_name<service::server>(
            get_winserv_name_by_epocver(symsys->get_symbian_version_use())));

        if (!winserv) {
            LOG_ERROR(FRONTEND_CMDLINE, "The window server is not there; nothing will be drawn");
            return;
        }

        for (epoc::screen *scr = winserv->get_screens(); scr; scr = scr->next) {
            scr->add_screen_redraw_callback(this, [](void *userdata, epoc::screen *scr, const bool is_dsa) {
                reinterpret_cast<emulator *>(userdata)->draw_screen(scr);
            });
        }

        winserv_.store(winserv);
    }

    void emulator::draw_screen(epoc::screen *scr) {
        if (!graphics_driver_ || should_quit_.load()) {
            return;
        }

        // Until the frame before this one has been handed to the frontend.
        // This is what paces the Symbian side: it cannot get more than one
        // frame ahead of retro_run. Returns at once when the driver is aborted.
        graphics_driver_->wait_for(&present_status_);

        if (should_quit_.load()) {
            return;
        }

        const eka2l1::vec2 size = scr->current_mode().size;
        const bool sideways = (scr->ui_rotation % 180) != 0;

        eka2l1::vec2 frame = size;
        if (sideways) {
            std::swap(frame.x, frame.y);
        }

        if ((frame.x <= 0) || (frame.y <= 0) || (frame.x > static_cast<int>(MAX_FRAME_SIZE))
            || (frame.y > static_cast<int>(MAX_FRAME_SIZE))) {
            LOG_ERROR(FRONTEND_CMDLINE, "A screen of {}x{} cannot be shown", frame.x, frame.y);
            return;
        }

        const drivers::filter_option filter = conf.nearest_neighbor_filtering ? drivers::filter_option::nearest
                                                                              : drivers::filter_option::linear;

        drivers::graphics_command_builder builder;

        // The frame is the whole of what is drawn into: the backend flips by
        // this height, which puts the picture in the bottom-left corner of the
        // frontend's (larger) framebuffer, where it is told to look.
        builder.set_swapchain_size(frame);

        builder.backup_state();
        builder.bind_bitmap(0);

        builder.set_feature(drivers::graphics_feature::cull, false);
        builder.set_feature(drivers::graphics_feature::depth_test, false);
        builder.set_feature(drivers::graphics_feature::blend, false);
        builder.set_feature(drivers::graphics_feature::clipping, false);
        builder.set_feature(drivers::graphics_feature::stencil_test, false);

        eka2l1::rect viewport;
        viewport.size = frame;
        builder.set_viewport(viewport);

        builder.clear({ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }, drivers::draw_buffer_bit_color_buffer);

        // One pixel of the phone is one pixel of the frame, and the screen
        // starts at its corner: what the window server needs to turn a touch
        // on the frame into a position on the screen.
        scr->set_native_scale_factor(graphics_driver_.get(), 1.0f, 1.0f);
        scr->absolute_pos = eka2l1::vec2(0, 0);

        eka2l1::rect dest;
        eka2l1::rect src;
        dest.size = size;
        src.size = size;

        drivers::advance_draw_pos_around_origin(dest, scr->ui_rotation);

        if (sideways) {
            std::swap(dest.size.x, dest.size.y);
            std::swap(src.size.x, src.size.y);
        }

        src.size *= scr->display_scale_factor;

        std::uint32_t flags = 0;
        if (scr->flags_ & epoc::screen::FLAG_SCREEN_UPSCALE_FACTOR_LOCK) {
            flags |= drivers::bitmap_draw_flag_use_upscale_shader;
        }

        builder.set_texture_filter(scr->screen_texture, true, filter);
        builder.set_texture_filter(scr->screen_texture, false, filter);
        builder.draw_bitmap(scr->screen_texture, 0, dest, src, eka2l1::vec2(0, 0),
            static_cast<float>(scr->ui_rotation), flags);

        builder.load_backup_state();

        frame_width_.store(static_cast<unsigned>(frame.x));
        frame_height_.store(static_cast<unsigned>(frame.y));

        present_status_ = -100;
        builder.present(&present_status_);

        drivers::command_list retrieved = builder.retrieve_command_list();
        graphics_driver_->submit_command_list(retrieved);
    }

    bool emulator::frame_size(unsigned &width, unsigned &height) const {
        width = frame_width_.load();
        height = frame_height_.load();
        return (width != 0) && (height != 0);
    }

    void emulator::key(const std::uint32_t scan_code, const bool pressed) {
        window_server *winserv = winserv_.load();
        if (!winserv) {
            return;
        }

        // Raw: the code is the phone's own, so the standalone's table of
        // keyboard bindings is not gone through
        drivers::input_event evt;
        evt.type_ = drivers::input_event_type::key_raw;
        evt.key_.code_ = static_cast<int>(scan_code);
        evt.key_.state_ = pressed ? drivers::key_state::pressed : drivers::key_state::released;

        winserv->queue_input_from_driver(evt);
    }

    void emulator::touch(const int x, const int y, const int action) {
        window_server *winserv = winserv_.load();
        if (!winserv) {
            return;
        }

        drivers::input_event evt;
        evt.type_ = drivers::input_event_type::touch;
        evt.mouse_.pos_x_ = x;
        evt.mouse_.pos_y_ = y;
        evt.mouse_.pos_z_ = 0;
        evt.mouse_.mouse_id = 0;
        evt.mouse_.button_ = drivers::mouse_button::mouse_button_left;
        evt.mouse_.action_ = static_cast<drivers::mouse_action>(action);
        evt.mouse_.raw_screen_pos_ = false;

        winserv->queue_input_from_driver(evt);
    }
}
