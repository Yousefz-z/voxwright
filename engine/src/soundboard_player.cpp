#include "vox/engine/soundboard_player.hpp"

#include <vox/dsp/math.hpp>

#include <algorithm>

namespace vox::engine {
namespace {

constexpr float kFadeMs = 5.0F;
constexpr float kDuckMs = 30.0F;
constexpr float kDuckFloor = 0.1F; // -20 dB

} // namespace

void SoundboardPlayer::prepare(double sampleRate) {
    fadeStep_ = 1000.0F / (kFadeMs * static_cast<float>(sampleRate));
    duckStep_ = 1000.0F / (kDuckMs * static_cast<float>(sampleRate));
}

const Clip* SoundboardPlayer::install(std::uint32_t id, const Clip* clip) noexcept {
    if (id >= kMaxClips) {
        return clip; // reject: hand it straight back
    }
    const Clip* old = remove(id);
    clips_[id] = clip;
    return old;
}

const Clip* SoundboardPlayer::remove(std::uint32_t id) noexcept {
    if (id >= kMaxClips) {
        return nullptr;
    }
    for (Voice& v : voices_) {
        if (v.state != State::Idle && v.id == id) {
            v = Voice{}; // the clip is going away: no fade possible
        }
    }
    const Clip* old = clips_[id];
    clips_[id] = nullptr;
    return old;
}

SoundboardPlayer::Voice* SoundboardPlayer::findVoice(std::uint32_t id) noexcept {
    for (Voice& v : voices_) {
        if (v.state != State::Idle && v.state != State::Stopping && v.id == id) {
            return &v;
        }
    }
    return nullptr;
}

SoundboardPlayer::Voice* SoundboardPlayer::allocateVoice() noexcept {
    for (Voice& v : voices_) {
        if (v.state == State::Idle) {
            return &v;
        }
    }
    // Steal the voice closest to its end.
    Voice* best = nullptr;
    std::size_t bestRemaining = SIZE_MAX;
    for (Voice& v : voices_) {
        const std::size_t remaining = v.clip != nullptr ? v.clip->samples.size() - v.position : 0;
        if (remaining < bestRemaining) {
            bestRemaining = remaining;
            best = &v;
        }
    }
    return best;
}

void SoundboardPlayer::start(std::uint32_t id, const SoundOptions& options) noexcept {
    if (id >= kMaxClips || clips_[id] == nullptr || clips_[id]->samples.empty()) {
        return;
    }
    if (options.stopOthers) {
        for (Voice& v : voices_) {
            if (v.state == State::Playing || v.state == State::Paused) {
                v.state = State::Stopping;
            }
        }
    }
    Voice* v = allocateVoice();
    if (v == nullptr) {
        return;
    }
    *v = Voice{};
    v->state = State::Playing;
    v->id = id;
    v->clip = clips_[id];
    v->options = options;
    v->gain = dsp::dbToGain(options.gainDb);
    v->fade = 0.0F;
}

void SoundboardPlayer::trigger(std::uint32_t id, const SoundOptions& options,
                               bool keyDown) noexcept {
    Voice* existing = findVoice(id);
    if (options.mode == PlayMode::Hold) {
        if (keyDown && existing == nullptr) {
            SoundOptions looped = options;
            looped.loop = true;
            start(id, looped);
        } else if (!keyDown && existing != nullptr) {
            existing->state = State::Stopping;
        }
        return;
    }
    if (!keyDown) {
        return;
    }
    switch (options.mode) {
    case PlayMode::Restart:
        if (existing != nullptr) {
            existing->state = State::Stopping; // fade the old copy out while the new one starts
        }
        start(id, options);
        break;
    case PlayMode::Toggle:
        if (existing != nullptr) {
            existing->state = State::Stopping;
        } else {
            start(id, options);
        }
        break;
    case PlayMode::Pause:
        if (existing == nullptr) {
            start(id, options);
        } else {
            existing->state = existing->state == State::Paused ? State::Playing : State::Paused;
        }
        break;
    case PlayMode::Overlap:
        start(id, options);
        break;
    case PlayMode::Hold:
        break;
    }
}

void SoundboardPlayer::stop(std::uint32_t id) noexcept {
    for (Voice& v : voices_) {
        if (v.id == id && (v.state == State::Playing || v.state == State::Paused)) {
            v.state = State::Stopping;
        }
    }
}

void SoundboardPlayer::stopAll() noexcept {
    for (Voice& v : voices_) {
        if (v.state == State::Playing || v.state == State::Paused) {
            v.state = State::Stopping;
        }
    }
}

void SoundboardPlayer::finish(Voice& voice) noexcept {
    // Report the sound as finished only when no other copy of it (a restart
    // or an overlap) is still playing or paused.
    const bool otherCopy = std::any_of(voices_.begin(), voices_.end(), [&voice](const Voice& v) {
        return &v != &voice && v.id == voice.id &&
               (v.state == State::Playing || v.state == State::Paused);
    });
    if (!otherCopy && finishedCount_ < finished_.size()) {
        finished_[finishedCount_++] = voice.id;
    }
    voice = Voice{};
}

std::span<const std::uint32_t> SoundboardPlayer::takeFinished() noexcept {
    const std::size_t n = finishedCount_;
    finishedCount_ = 0;
    return std::span<const std::uint32_t>(finished_).first(n);
}

std::size_t SoundboardPlayer::activeVoices() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        voices_.begin(), voices_.end(), [](const Voice& v) { return v.state != State::Idle; }));
}

bool SoundboardPlayer::isPlaying(std::uint32_t id) const noexcept {
    return std::any_of(voices_.begin(), voices_.end(),
                       [id](const Voice& v) { return v.state == State::Playing && v.id == id; });
}

void SoundboardPlayer::process(std::span<float> all, std::span<float> monitor) noexcept {
    std::fill(all.begin(), all.end(), 0.0F);
    std::fill(monitor.begin(), monitor.end(), 0.0F);
    const bool anyMuteOthers = std::any_of(voices_.begin(), voices_.end(), [](const Voice& v) {
        return v.state == State::Playing && v.options.muteOthers;
    });
    const bool anyMuteVoice = std::any_of(voices_.begin(), voices_.end(), [](const Voice& v) {
        return (v.state == State::Playing || v.state == State::Paused) && v.options.muteVoice;
    });
    for (Voice& v : voices_) {
        if (v.state == State::Idle || v.clip == nullptr) {
            continue;
        }
        const std::vector<float>& samples = v.clip->samples;
        const float duckTarget = (anyMuteOthers && !v.options.muteOthers) ? 0.0F : 1.0F;
        for (std::size_t i = 0; i < all.size(); ++i) {
            if (v.state == State::Paused) {
                v.fade = std::max(0.0F, v.fade - fadeStep_);
                if (v.fade == 0.0F) {
                    break; // fully paused: keep position, emit nothing
                }
            } else if (v.state == State::Stopping) {
                v.fade -= fadeStep_;
                if (v.fade <= 0.0F) {
                    finish(v);
                    break;
                }
            } else {
                v.fade = std::min(1.0F, v.fade + fadeStep_);
            }
            if (v.position >= samples.size()) {
                if (v.options.loop) {
                    v.position = 0;
                } else {
                    finish(v);
                    break;
                }
            }
            v.duck += duckTarget > v.duck ? std::min(duckStep_, duckTarget - v.duck)
                                          : std::max(-duckStep_, duckTarget - v.duck);
            const float s = samples[v.position] * v.gain * v.fade * v.duck;
            if (v.state != State::Paused || v.fade > 0.0F) {
                ++v.position;
            }
            all[i] += s;
            if (!v.options.muteForMe) {
                monitor[i] += s;
            }
        }
    }
    const float duckTarget = anyMuteVoice ? kDuckFloor : 1.0F;
    const float steps = duckStep_ * static_cast<float>(all.size());
    voiceDuck_ += std::clamp(duckTarget - voiceDuck_, -steps, steps);
}

} // namespace vox::engine
