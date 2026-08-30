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
#include <config/config.h>
#include <system/devices.h>

#include <libretro.h>

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
}

extern "C" {

RETRO_API void retro_set_environment(retro_environment_t cb) {
    env_cb = cb;

    // Nothing to load yet, and no content model to load it with, so the core
    // starts without content for the moment.
    bool no_content = true;
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

    // Ask the emulator what devices are installed. Nothing is done with the
    // answer yet - a Symbian title runs on a device installed from a firmware
    // dump, and deciding where that lives and how the frontend points at one is
    // the next piece of work. It is here now because it is the first real call
    // into the emulator, and linking a static emulator into a shared object is
    // the thing this skeleton exists to prove.
    eka2l1::config::state conf;
    eka2l1::device_manager devices(&conf);

    if (log_cb)
        log_cb(RETRO_LOG_INFO, "Devices installed: %zu\n", devices.get_devices().size());
}

RETRO_API void retro_deinit(void) {}

RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->library_name = "EKA2L1";
    info->library_version = CURRENT_EKA2L1_VERSION_STRING;
    // A Symbian title is installed into a device rather than opened from a
    // path, so what belongs here is still an open question - see the note at
    // the top of this file.
    info->valid_extensions = "sis|sisx|n-gage";
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

    // No emulation yet: hand the frontend a repeat of nothing so it has a
    // frame to pace on.
    if (video_cb)
        video_cb(nullptr, DEFAULT_WIDTH, DEFAULT_HEIGHT, DEFAULT_WIDTH * sizeof(std::uint32_t));
}

RETRO_API bool retro_load_game(const struct retro_game_info *) { return true; }

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t) { return false; }

RETRO_API void retro_unload_game(void) {}

RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }

RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void *, size_t) { return false; }
RETRO_API bool retro_unserialize(const void *, size_t) { return false; }

RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned, bool, const char *) {}

RETRO_API void *retro_get_memory_data(unsigned) { return nullptr; }
RETRO_API size_t retro_get_memory_size(unsigned) { return 0; }

} // extern "C"
