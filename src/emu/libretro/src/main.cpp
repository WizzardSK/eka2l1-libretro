/*
 * The libretro entry points: what the frontend asks and is told, the core
 * option, and the frontend's input turned into the phone's keys and touch
 * screen. The emulator itself is in the files beside this one.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <common/configure.h>
#include <common/log.h>

#include <spdlog/sinks/base_sink.h>

#include <memory>
#include <mutex>

#include <libretro.h>
#include <libretro_state.h>
#include <drivers/graphics/backend/context_libretro.h>
#include <drivers/input/common.h>
#include <services/window/keys.h>

#include <string>

// The frontend's VFS, for content behind a URI (content.cpp)
const retro_vfs_interface *libretro_vfs = nullptr;

namespace {
    retro_environment_t env_cb = nullptr;
    retro_video_refresh_t video_cb = nullptr;
    retro_audio_sample_t audio_cb = nullptr;
    retro_audio_sample_batch_t audio_batch_cb = nullptr;
    retro_input_poll_t input_poll_cb = nullptr;
    retro_input_state_t input_state_cb = nullptr;
    retro_log_printf_t log_cb = nullptr;

    // The N-Gage screen. Symbian devices vary, and a title may turn the screen
    // round; this is what the frontend is told until the emulator has drawn a
    // frame, whose size it is told then.
    constexpr unsigned DEFAULT_WIDTH = 176;
    constexpr unsigned DEFAULT_HEIGHT = 208;

    unsigned frame_width = DEFAULT_WIDTH;
    unsigned frame_height = DEFAULT_HEIGHT;

    // The audio the core hands over each run: the driver's rate over the 60
    // runs a second the core declares
    constexpr unsigned AUDIO_FRAMES_PER_RUN = eka2l1::libretro::audio_driver_libretro::OUTPUT_RATE / 60;

    eka2l1::libretro::emulator emu;

    retro_hw_render_callback hw_render{};
    bool emulator_started = false;
    std::string content_path;

    // Everything the emulator keeps - configuration, the devices installed from
    // firmware dumps, the virtual drives - under the directory the frontend
    // hands out for exactly that.
    // The emulator's own log (LOG_INFO, LOG_ERROR...) goes nowhere until a
    // logger is set up, and standalone's writes EKA2L1.log next to the
    // executable - so a core's messages, why a package did not install among
    // them, went unseen. They go to RetroArch's log instead.
    class frontend_log_sink final : public spdlog::sinks::base_sink<std::mutex> {
    protected:
        void sink_it_(const spdlog::details::log_msg &msg) override {
            if (!log_cb)
                return;
            retro_log_level level = RETRO_LOG_DEBUG;
            switch (msg.level) {
            case spdlog::level::info:
                level = RETRO_LOG_INFO;
                break;
            case spdlog::level::warn:
                level = RETRO_LOG_WARN;
                break;
            case spdlog::level::err:
            case spdlog::level::critical:
                level = RETRO_LOG_ERROR;
                break;
            default:
                break;
            }
            log_cb(level, "%.*s\n", static_cast<int>(msg.payload.size()), msg.payload.data());
        }

        void flush_() override {}
    };

    void setup_frontend_log() {
        if (eka2l1::log::spd_logger)
            return;
        eka2l1::log::spd_logger = std::make_shared<spdlog::logger>("EKA2L1", std::make_shared<frontend_log_sink>());
        eka2l1::log::spd_logger->set_level(spdlog::level::trace);
        // Warnings and errors from everywhere; the frontend glue's own
        // messages (installing, launching) from info, as they say what
        // happened to the content
        eka2l1::log::filterings = std::make_unique<eka2l1::log_filterings>();
        eka2l1::log::filterings->reset_all(spdlog::level::warn);
        eka2l1::log::filterings->set_minimum_level(eka2l1::FRONTEND_CMDLINE, spdlog::level::info);
    }

    // The device to run on, of those installed. They are only known once the
    // emulator has read its device list, so the option is published from
    // there (retro_init) and names them as the standalone's list does.
    constexpr const char *DEVICE_OPTION = "eka2l1_device";

    std::string device_name(const eka2l1::device &dvc) {
        std::string name = dvc.model + " (" + dvc.firmware_code + ")";
        for (char &c : name) {
            // The two characters an option's value list is cut at
            if ((c == '|') || (c == ';'))
                c = ' ';
        }
        return name;
    }

    std::size_t choose_device(const std::vector<eka2l1::device> &devices) {
        if (!env_cb || devices.empty())
            return 0;

        // Kept: the frontend holds on to the pointers
        static std::string description;
        description = "Device (Restart Required); ";
        for (std::size_t i = 0; i < devices.size(); i++) {
            if (i != 0)
                description += '|';
            description += device_name(devices[i]);
        }

        static retro_variable variables[2];
        variables[0] = { DEVICE_OPTION, description.c_str() };
        variables[1] = { nullptr, nullptr };
        env_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

        retro_variable var{ DEVICE_OPTION, nullptr };
        if (env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
            for (std::size_t i = 0; i < devices.size(); i++) {
                if (device_name(devices[i]) == var.value)
                    return i;
            }
        }

        return 0;
    }

    // The phone's keys on a RetroPad, and its number keys on the keyboard as
    // well. A key is down while anything bound to it is.
    struct key_binding {
        std::uint32_t scan_code;
        unsigned device;
        unsigned id;
    };

    const key_binding KEY_BINDINGS[] = {
        { eka2l1::epoc::std_key_up_arrow, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_UP },
        { eka2l1::epoc::std_key_down_arrow, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_DOWN },
        { eka2l1::epoc::std_key_left_arrow, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_LEFT },
        { eka2l1::epoc::std_key_right_arrow, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_RIGHT },
        { eka2l1::epoc::std_key_device_3, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_A },
        { '5', RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_B },
        { '7', RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_Y },
        { '0', RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_X },
        { eka2l1::epoc::std_key_device_0, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_L },
        { eka2l1::epoc::std_key_device_1, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_R },
        { eka2l1::epoc::std_key_nkp_asterisk, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_L2 },
        { eka2l1::epoc::std_key_hash, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_R2 },
        { eka2l1::epoc::std_key_application_0, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_L3 },
        { eka2l1::epoc::std_key_no, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_R3 },
        { eka2l1::epoc::std_key_yes, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_START },
        { eka2l1::epoc::std_key_backspace, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_SELECT },
        { '0', RETRO_DEVICE_KEYBOARD, RETROK_0 },
        { '1', RETRO_DEVICE_KEYBOARD, RETROK_1 },
        { '2', RETRO_DEVICE_KEYBOARD, RETROK_2 },
        { '3', RETRO_DEVICE_KEYBOARD, RETROK_3 },
        { '4', RETRO_DEVICE_KEYBOARD, RETROK_4 },
        { '5', RETRO_DEVICE_KEYBOARD, RETROK_5 },
        { '6', RETRO_DEVICE_KEYBOARD, RETROK_6 },
        { '7', RETRO_DEVICE_KEYBOARD, RETROK_7 },
        { '8', RETRO_DEVICE_KEYBOARD, RETROK_8 },
        { '9', RETRO_DEVICE_KEYBOARD, RETROK_9 },
        { '0', RETRO_DEVICE_KEYBOARD, RETROK_KP0 },
        { '1', RETRO_DEVICE_KEYBOARD, RETROK_KP1 },
        { '2', RETRO_DEVICE_KEYBOARD, RETROK_KP2 },
        { '3', RETRO_DEVICE_KEYBOARD, RETROK_KP3 },
        { '4', RETRO_DEVICE_KEYBOARD, RETROK_KP4 },
        { '5', RETRO_DEVICE_KEYBOARD, RETROK_KP5 },
        { '6', RETRO_DEVICE_KEYBOARD, RETROK_KP6 },
        { '7', RETRO_DEVICE_KEYBOARD, RETROK_KP7 },
        { '8', RETRO_DEVICE_KEYBOARD, RETROK_KP8 },
        { '9', RETRO_DEVICE_KEYBOARD, RETROK_KP9 },
        { eka2l1::epoc::std_key_nkp_asterisk, RETRO_DEVICE_KEYBOARD, RETROK_KP_MULTIPLY },
        { eka2l1::epoc::std_key_hash, RETRO_DEVICE_KEYBOARD, RETROK_KP_DIVIDE }
    };

    // Scan codes are below this
    constexpr std::size_t SCAN_CODES = 256;
    bool key_down[SCAN_CODES] = {};

    bool touch_down = false;
    int touch_x = 0;
    int touch_y = 0;

    void release_input() {
        std::memset(key_down, 0, sizeof(key_down));
        touch_down = false;
    }

    void update_input() {
        if (!input_state_cb)
            return;

        bool now_down[SCAN_CODES] = {};
        for (const key_binding &binding : KEY_BINDINGS) {
            if (input_state_cb(0, binding.device, 0, binding.id))
                now_down[binding.scan_code] = true;
        }

        for (std::size_t code = 0; code < SCAN_CODES; code++) {
            if (now_down[code] != key_down[code]) {
                key_down[code] = now_down[code];
                emu.key(static_cast<std::uint32_t>(code), now_down[code]);
            }
        }

        // The touch screen: the frontend's pointer, which is its mouse and
        // its touch screen both, over the picture
        const bool pressed = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_PRESSED) != 0;
        if (pressed) {
            const int raw_x = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_X);
            const int raw_y = input_state_cb(0, RETRO_DEVICE_POINTER, 0, RETRO_DEVICE_ID_POINTER_Y);
            const int x = (raw_x + 0x7fff) * static_cast<int>(frame_width) / 0xffff;
            const int y = (raw_y + 0x7fff) * static_cast<int>(frame_height) / 0xffff;

            if (!touch_down) {
                emu.touch(x, y, eka2l1::drivers::mouse_action_press);
            } else if ((x != touch_x) || (y != touch_y)) {
                emu.touch(x, y, eka2l1::drivers::mouse_action_repeat);
            }

            touch_x = x;
            touch_y = y;
        } else if (touch_down) {
            emu.touch(touch_x, touch_y, eka2l1::drivers::mouse_action_release);
        }
        touch_down = pressed;
    }

    void set_input_descriptors() {
        static const retro_input_descriptor descriptors[] = {
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, "Up" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, "Down" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, "Left" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, "OK (Middle Key)" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, "5" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, "7" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, "0" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, "Left Soft Key" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, "Right Soft Key" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2, "*" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2, "#" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3, "Menu Key" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3, "End Key (Red)" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Call Key (Green)" },
            { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Clear Key (C)" },
            { 0, 0, 0, 0, nullptr }
        };
        if (env_cb)
            env_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, const_cast<retro_input_descriptor *>(descriptors));
    }

    std::string data_root() {
        const char *system_dir = nullptr;
        if (env_cb && env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir)
            return std::string(system_dir) + "/eka2l1";
        return "eka2l1";
    }
}

extern "C" {

RETRO_API void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;

    // A title is required: with none there is nothing to show, and EKA2L1 has
    // no application menu of its own to fall back on - drivers::ui is a bridge
    // for the dialogs Symbian asks for, not a launcher. Every frontend has a
    // file browser, and it is a better one than a core could draw.
    bool no_content = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);

    retro_log_callback log{};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;

    // Content behind a URI (saf://, Android's Play Store RetroArch) is read
    // through the frontend; v1 (open/read/size/close) is all content.cpp uses
    retro_vfs_interface_info vfs{ 1, nullptr };
    if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs) && vfs.iface)
        libretro_vfs = vfs.iface;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void retro_init(void) {
    setup_frontend_log();

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "EKA2L1 " CURRENT_EKA2L1_VERSION_STRING " libretro core\n");

    const std::string root = data_root();
    emu.choose_device = choose_device;
    emu.bring_up(root);

    emu.install_device_if_needed(root + "/firmware");

    const std::size_t devices = emu.device_count();
    if (log_cb) {
        log_cb(RETRO_LOG_INFO, "Data directory: %s\n", root.c_str());
        log_cb(RETRO_LOG_INFO, "Devices installed: %zu\n", devices);

        // Worth saying plainly rather than failing obscurely later: a Symbian
        // title runs on a device built from a firmware dump, and without one
        // there is nothing for the emulator to start.
        if (devices == 0)
            log_cb(RETRO_LOG_WARN, "No device installed. Put a firmware dump in %s/firmware and load content again.\n", root.c_str());
    }
}

RETRO_API void retro_deinit(void) {
    emu.shut_down();
}

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->library_name = "EKA2L1";
    info->library_version = CURRENT_EKA2L1_VERSION_STRING " " EKA2L1_UPSTREAM_COMMIT;
    // A package to install and launch, or a shortcut naming what is already
    // installed - see CONTENT_MODEL.md.
    info->valid_extensions = "sis|sisx|n-gage|eka2l1";
    info->need_fullpath = true;
    info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->geometry.base_width = frame_width;
    info->geometry.base_height = frame_height;
    // The frontend's framebuffer is made this large once; the phone's screen,
    // whichever way up, is drawn into a corner of it
    info->geometry.max_width = eka2l1::libretro::emulator::MAX_FRAME_SIZE;
    info->geometry.max_height = eka2l1::libretro::emulator::MAX_FRAME_SIZE;
    info->geometry.aspect_ratio = static_cast<float>(frame_width) / static_cast<float>(frame_height);
    info->timing.fps = 60.0;
    info->timing.sample_rate = static_cast<double>(eka2l1::libretro::audio_driver_libretro::OUTPUT_RATE);
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API void retro_reset(void) {}

RETRO_API void retro_run(void) {
    if (input_poll_cb)
        input_poll_cb();

    // Exactly one frame of audio a run - 44100 / 60 - whatever the emulator
    // is doing: the frontend paces itself on it
    static std::int16_t audio_frame[AUDIO_FRAMES_PER_RUN * 2];

    if (!emulator_started) {
        // Nothing is running: hand the frontend a repeat so it has something
        // to pace on rather than a stall.
        if (video_cb)
            video_cb(nullptr, frame_width, frame_height, 0);
        if (audio_batch_cb) {
            std::memset(audio_frame, 0, sizeof(audio_frame));
            audio_batch_cb(audio_frame, AUDIO_FRAMES_PER_RUN);
        }
        return;
    }

    // The Symbian OS is not frame-stepped; this runs its graphics, on the
    // thread the frontend's context is current on, until it presents a frame.
    // A title that draws nothing still returns - the frontend then repeats the
    // last frame, which is better than a frozen frontend.
    update_input();

    const bool got_frame = emu.run_frame();

    // The size of the screen as the emulator drew it: another device, or a
    // title that turned the screen round
    unsigned width = 0, height = 0;
    if (got_frame && emu.frame_size(width, height) && ((width != frame_width) || (height != frame_height))) {
        frame_width = width;
        frame_height = height;

        retro_game_geometry geometry{};
        geometry.base_width = frame_width;
        geometry.base_height = frame_height;
        geometry.max_width = eka2l1::libretro::emulator::MAX_FRAME_SIZE;
        geometry.max_height = eka2l1::libretro::emulator::MAX_FRAME_SIZE;
        geometry.aspect_ratio = static_cast<float>(frame_width) / static_cast<float>(frame_height);
        if (env_cb)
            env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
    }

    if (video_cb)
        video_cb(got_frame ? RETRO_HW_FRAME_BUFFER_VALID : nullptr, frame_width, frame_height, 0);

    if (audio_batch_cb) {
        emu.mix_audio(audio_frame, AUDIO_FRAMES_PER_RUN);
        audio_batch_cb(audio_frame, AUDIO_FRAMES_PER_RUN);
    }
}

namespace {
    // The frontend's context exists from here until context_destroy, and this
    // is the thread it is current on - so this is where the emulator's
    // graphics driver is made, and its OS thread started, and where they have
    // to stop.
    void context_reset() {
        if (log_cb)
            log_cb(RETRO_LOG_INFO, "Frontend GL context ready\n");

        if (emu.device_count() == 0) {
            // Refused rather than half-started: without a device there is
            // nothing to run, and saying so once is kinder than a black screen.
            if (log_cb)
                log_cb(RETRO_LOG_ERROR, "No device installed - not starting the emulator.\n");
            return;
        }

        eka2l1::drivers::graphics::gl_context_libretro::set_proc_address_getter(hw_render.get_proc_address);

        // Loaded by the emulator's OS thread, once the system is up
        emu.queue_content(content_path);

        emulator_started = emu.start([]() -> unsigned int {
            // Per frame, never cached: the frontend is entitled to hand over a
            // different framebuffer each time.
            return static_cast<unsigned int>(hw_render.get_current_framebuffer());
        });

    }

    void context_destroy() {
        emu.shut_down();
        emulator_started = false;
        release_input();
    }
}

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
    // Which context to ask for: a phone has GLES, a desktop has both and the
    // emulator's GL backend detects what it got either way.
#ifdef ANDROID
    hw_render.context_type = RETRO_HW_CONTEXT_OPENGLES3;
#else
    hw_render.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
    hw_render.version_major = 3;
    hw_render.version_minor = 3;
#endif
    hw_render.context_reset = context_reset;
    hw_render.context_destroy = context_destroy;
    hw_render.depth = true;
    hw_render.bottom_left_origin = true;
    hw_render.cache_context = false;

    if (!env_cb || !env_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw_render)) {
        if (log_cb)
            log_cb(RETRO_LOG_ERROR, "The frontend cannot provide an OpenGL context; this core needs one.\n");
        return false;
    }

    // The title is remembered rather than started: the emulator itself comes up
    // in context_reset, when there is a context to render into.
    if (!game || !game->path) {
        return false;
    }

    content_path = game->path;
    frame_width = DEFAULT_WIDTH;
    frame_height = DEFAULT_HEIGHT;
    release_input();
    set_input_descriptors();
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t) { return false; }

RETRO_API void retro_unload_game(void) {
    emu.shut_down();
    emulator_started = false;
    release_input();
}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *, size_t) { return false; }
RETRO_API bool retro_unserialize(const void *, size_t) { return false; }

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}

RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }

} // extern "C"
