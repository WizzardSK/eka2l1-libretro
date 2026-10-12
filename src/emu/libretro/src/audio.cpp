// Copyright (c) 2026 EKA2L1 Team.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <libretro_audio.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eka2l1::libretro {
    output_stream_libretro::output_stream_libretro(audio_driver_libretro *driver, const std::uint32_t sample_rate,
        const std::uint8_t channels, drivers::data_callback callback)
        : drivers::audio_output_stream(driver, sample_rate, channels)
        , owner_(driver)
        , callback_(std::move(callback)) {
        owner_->add_stream(this);
    }

    output_stream_libretro::~output_stream_libretro() {
        owner_->remove_stream(this);
    }

    bool output_stream_libretro::start() {
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());

        if (pausing_) {
            pausing_ = false;
            return true;
        }

        if (!playing_) {
            playing_ = true;
            ended_ = false;
            pending_.clear();
            position_ = 0.0;
            played_frames_ = 0;
        }

        return true;
    }

    bool output_stream_libretro::stop() {
        // With the lock held no data or drained callback is running, and
        // none starts after it: the mix skips a stream that is not playing
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());

        playing_ = false;
        pausing_ = false;
        ended_ = false;
        pending_.clear();
        position_ = 0.0;

        return true;
    }

    void output_stream_libretro::pause() {
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());
        pausing_ = true;
    }

    bool output_stream_libretro::is_playing() {
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());
        return playing_;
    }

    bool output_stream_libretro::is_pausing() {
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());
        return pausing_;
    }

    bool output_stream_libretro::set_volume(const float volume) {
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());
        volume_ = volume;
        return true;
    }

    float output_stream_libretro::get_volume() const {
        return volume_;
    }

    bool output_stream_libretro::current_frame_position(std::uint64_t *pos) {
        const std::lock_guard<std::recursive_mutex> guard(owner_->stream_lock());
        *pos = played_frames_;
        return true;
    }

    void output_stream_libretro::mix_into(std::int32_t *out, const std::size_t frames, const std::uint32_t rate) {
        if (!frames || !playing_ || pausing_ || driver_->suspending() || !sample_rate || !channels) {
            return;
        }

        const double step = static_cast<double>(sample_rate) / static_cast<double>(rate);

        // The last output frame is interpolated between two input frames:
        // the one its position falls in and the one after it
        const std::size_t needed = static_cast<std::size_t>(std::floor(position_ + step * static_cast<double>(frames - 1))) + 2;
        std::size_t have = pending_.size() / channels;

        if (!ended_ && (have < needed)) {
            const std::size_t wanted = needed - have;
            pending_.resize((have + wanted) * channels);

            const std::size_t got = std::min(wanted, callback_(pending_.data() + have * channels, wanted));
            if (got < wanted) {
                // The stream's data ends here. What is missing plays as
                // silence, and the stream is over after this frame.
                std::memset(pending_.data() + (have + got) * channels, 0,
                    (wanted - got) * channels * sizeof(std::int16_t));
                ended_ = true;
            }

            have += wanted;
        }

        const float gain = volume_ * (static_cast<float>(driver_->master_volume()) / 100.0f);

        for (std::size_t i = 0; i < frames; i++) {
            const double where = position_ + step * static_cast<double>(i);
            const std::size_t index = static_cast<std::size_t>(where);
            if (index + 1 >= have) {
                break;
            }

            const float fraction = static_cast<float>(where - static_cast<double>(index));
            const std::int16_t *now = pending_.data() + index * channels;
            const std::int16_t *next = now + channels;

            const float left = static_cast<float>(now[0]) + (static_cast<float>(next[0]) - static_cast<float>(now[0])) * fraction;
            float right = left;
            if (channels > 1) {
                right = static_cast<float>(now[1]) + (static_cast<float>(next[1]) - static_cast<float>(now[1])) * fraction;
            }

            out[i * 2] += static_cast<std::int32_t>(left * gain);
            out[i * 2 + 1] += static_cast<std::int32_t>(right * gain);
        }

        // The input frames this frame went past are played
        const double end = position_ + step * static_cast<double>(frames);
        const std::size_t consumed = std::min(static_cast<std::size_t>(end), have);

        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed * channels));
        position_ = end - static_cast<double>(consumed);
        if (position_ < 0.0) {
            position_ = 0.0;
        }
        played_frames_ += consumed;

        if (ended_) {
            playing_ = false;
            ended_ = false;
            pending_.clear();
            position_ = 0.0;

            if (drained_callback_) {
                drained_callback_();
            }
        }
    }

    std::unique_ptr<drivers::audio_output_stream> audio_driver_libretro::new_output_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, drivers::data_callback callback) {
        return std::make_unique<output_stream_libretro>(this, sample_rate, channels, std::move(callback));
    }

    std::unique_ptr<drivers::audio_input_stream> audio_driver_libretro::new_input_stream(const std::uint32_t sample_rate,
        const std::uint8_t channels, drivers::data_callback) {
        return std::make_unique<input_stream_libretro>(this, sample_rate, channels);
    }

    void audio_driver_libretro::add_stream(output_stream_libretro *stream) {
        const std::lock_guard<std::recursive_mutex> guard(stream_lock_);
        streams_.push_back(stream);
    }

    void audio_driver_libretro::remove_stream(output_stream_libretro *stream) {
        const std::lock_guard<std::recursive_mutex> guard(stream_lock_);
        streams_.erase(std::remove(streams_.begin(), streams_.end(), stream), streams_.end());
    }

    void audio_driver_libretro::mix(std::int16_t *out, const std::size_t frames) {
        accumulator_.assign(frames * 2, 0);

        {
            const std::lock_guard<std::recursive_mutex> guard(stream_lock_);

            // By index, over a copy of the count: a callback may make or end
            // a stream while the mix runs
            for (std::size_t i = 0; i < streams_.size(); i++) {
                streams_[i]->mix_into(accumulator_.data(), frames, OUTPUT_RATE);
            }
        }

        for (std::size_t i = 0; i < frames * 2; i++) {
            out[i] = static_cast<std::int16_t>(std::clamp<std::int32_t>(accumulator_[i], INT16_MIN, INT16_MAX));
        }
    }
}
