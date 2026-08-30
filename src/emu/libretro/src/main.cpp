/*
 * The libretro entry points.
 *
 * This is the skeleton: it answers the frontend's questions and links against
 * the emulator, but does not run anything yet. Its job for now is to prove the
 * shape - that the emulator's libraries can live inside a shared object the
 * frontend dlopen()s, which is a different thing from linking an executable.
 *
 * What is deliberately absent: the emulation threads (EKA2L1 runs its own OS
 * and graphics threads and has no "run one frame" call, so retro_run will have
 * to hand-shake with them), the window and GL context, and the device/app
 * bootstrap, which is a design question rather than a coding one - a title is
 * installed into a virtual device and launched by UID, not loaded from a path.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <common/configure.h>

#include <libretro.h>
#include <libretro_state.h>

#include <string>

namespace {
    retro_environment_t env_cb = nullptr;
    retro_video_refresh_t video_cb = nullptr;
    retro_audio_sample_t audio_cb = nullptr;
    retro_audio_sample_batch_t audio_batch_cb = nullptr;
    retro_input_poll_t input_poll_cb = nullptr;
    retro_input_state_t input_state_cb = nullptr;
    retro_log_printf_t log_cb = nullptr;

    // The N-Gage screen. Symbian devices vary and the real geometry has to come
    // from the emulated device once one is loaded; this is what the frontend is
    // told until then.
    constexpr unsigned DEFAULT_WIDTH = 176;
    constexpr unsigned DEFAULT_HEIGHT = 208;

    eka2l1::libretro::emulator emu;

    retro_hw_render_callback hw_render{};
    bool emulator_started = false;
    std::string content_path;

    // Everything the emulator keeps - configuration, the devices installed from
    // firmware dumps, the virtual drives - under the directory the frontend
    // hands out for exactly that.
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
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

RETRO_API void retro_init(void) {
    if (log_cb)
        log_cb(RETRO_LOG_INFO, "EKA2L1 " CURRENT_EKA2L1_VERSION_STRING " libretro core\n");

    const std::string root = data_root();
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
    info->library_version = CURRENT_EKA2L1_VERSION_STRING;
    // A package to install and launch, or a shortcut naming what is already
    // installed - see CONTENT_MODEL.md.
    info->valid_extensions = "sis|sisx|n-gage|eka2l1";
    info->need_fullpath = true;
    info->block_extract = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->geometry.base_width = DEFAULT_WIDTH;
    info->geometry.base_height = DEFAULT_HEIGHT;
    info->geometry.max_width = DEFAULT_WIDTH;
    info->geometry.max_height = DEFAULT_HEIGHT;
    info->geometry.aspect_ratio = static_cast<float>(DEFAULT_WIDTH) / static_cast<float>(DEFAULT_HEIGHT);
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

RETRO_API void retro_set_controller_port_device(unsigned, unsigned) {}

RETRO_API void retro_reset(void) {}

RETRO_API void retro_run(void) {
    if (input_poll_cb)
        input_poll_cb();

    if (!emulator_started) {
        // Nothing is running: hand the frontend a repeat so it has something
        // to pace on rather than a stall.
        if (video_cb)
            video_cb(nullptr, DEFAULT_WIDTH, DEFAULT_HEIGHT, 0);
        return;
    }

    // The emulator is not frame-stepped, so this waits for it rather than
    // driving it. A title that draws nothing still returns - the frontend then
    // repeats the last frame, which is better than a frozen frontend.
    const bool got_frame = emu.wait_for_frame();

    if (video_cb)
        video_cb(got_frame ? RETRO_HW_FRAME_BUFFER_VALID : nullptr, DEFAULT_WIDTH, DEFAULT_HEIGHT, 0);
}

namespace {
    // The frontend's context exists from here until context_destroy, and this
    // is the thread it is current on - so this is where the emulator's own
    // threads may start, and where they have to stop.
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

        emulator_started = emu.start([]() -> unsigned int {
            // Per frame, never cached: the frontend is entitled to hand over a
            // different framebuffer each time.
            return static_cast<unsigned int>(hw_render.get_current_framebuffer());
        });

        if (emulator_started && !content_path.empty()) {
            if (!emu.load_content(content_path)) {
                if (log_cb)
                    log_cb(RETRO_LOG_ERROR, "Could not start %s\n", content_path.c_str());
            }
        }
    }

    void context_destroy() {
        emu.shut_down();
        emulator_started = false;
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
    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t) { return false; }

RETRO_API void retro_unload_game(void) {
    emu.shut_down();
    emulator_started = false;
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
