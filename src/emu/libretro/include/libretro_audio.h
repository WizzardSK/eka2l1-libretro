// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <drivers/audio/audio.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace eka2l1::libretro {
    class audio_driver_libretro;

    // An output stream of the emulator's. It has no device behind it: the
    // driver pulls its data when the frontend asks for a frame of audio.
    struct output_stream_libretro : public drivers::audio_output_stream {
        explicit output_stream_libretro(audio_driver_libretro *driver, const std::uint32_t sample_rate,
            const std::uint8_t channels, drivers::data_callback callback);
        ~output_stream_libretro() override;

        bool start() override;
        bool stop() override;
        void pause() override;

        bool is_playing() override;
        bool is_pausing() override;

        bool set_volume(const float volume) override;
        float get_volume() const override;

        bool current_frame_position(std::uint64_t *pos) override;

        // Adds `frames` frames of this stream to `out`, which is stereo at
        // `rate`. Called by the driver with its lock held.
        void mix_into(std::int32_t *out, const std::size_t frames, const std::uint32_t rate);

    private:
        audio_driver_libretro *owner_;
        drivers::data_callback callback_;

        bool playing_ = false;
        bool pausing_ = false;
        // The callback gave less than it was asked for: the stream ends once
        // what it gave has been played
        bool ended_ = false;
        float volume_ = 1.0f;

        // Frames taken from the callback and not played yet, in the stream's
        // own rate and channel count, and where in the first of them the
        // next output frame falls
        std::vector<std::int16_t> pending_;
        double position_ = 0.0;
        std::uint64_t played_frames_ = 0;
    };

    // The stream the emulator records from. A core has no microphone: it
    // never starts.
    struct input_stream_libretro : public drivers::audio_input_stream {
        explicit input_stream_libretro(drivers::audio_driver *driver, const std::uint32_t sample_rate,
            const std::uint8_t channels)
            : drivers::audio_input_stream(driver, sample_rate, channels) {
        }

        bool start() override { return false; }
        bool stop() override { return true; }
        bool is_recording() override { return false; }
        bool current_frame_position(std::uint64_t *pos) override {
            *pos = 0;
            return true;
        }
    };

    // The emulator's audio, handed to the frontend a frame at a time: every
    // stream that is playing is brought to the frontend's rate and mixed.
    class audio_driver_libretro : public drivers::audio_driver {
    public:
        // What the core declares to the frontend (retro_get_system_av_info)
        static constexpr std::uint32_t OUTPUT_RATE = 44100;

        explicit audio_driver_libretro(const std::uint32_t initial_master_volume, const drivers::player_type preferred_midi_backend)
            : drivers::audio_driver(initial_master_volume, preferred_midi_backend) {
        }

        std::unique_ptr<drivers::audio_output_stream> new_output_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, drivers::data_callback callback) override;

        std::unique_ptr<drivers::audio_input_stream> new_input_stream(const std::uint32_t sample_rate,
            const std::uint8_t channels, drivers::data_callback callback) override;

        std::uint32_t native_sample_rate() override {
            return OUTPUT_RATE;
        }

        // `frames` stereo frames at OUTPUT_RATE into `out`; silence where
        // nothing plays. On the frontend's thread, once per retro_run.
        void mix(std::int16_t *out, const std::size_t frames);

        // The streams' own calls and the mix exclude each other. Recursive,
        // as a stream's data callback may start or stop a stream.
        std::recursive_mutex &stream_lock() { return stream_lock_; }

        void add_stream(output_stream_libretro *stream);
        void remove_stream(output_stream_libretro *stream);

    private:
        std::recursive_mutex stream_lock_;
        std::vector<output_stream_libretro *> streams_;
        std::vector<std::int32_t> accumulator_;
    };
}
