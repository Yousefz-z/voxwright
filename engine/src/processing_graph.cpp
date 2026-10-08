#include "vox/engine/processing_graph.hpp"

#include <vox/dsp/math.hpp>

#include <algorithm>
#include <cmath>

namespace vox::engine {
namespace {

constexpr float kCrossfadeMs = 20.0F;
constexpr float kLevelRampMs = 30.0F;
constexpr float kInputGainRampMs = 20.0F;
constexpr float kLimiterCeilingDb = -1.0F;
constexpr float kDefaultGateThresholdDb = -50.0F;
constexpr float kGateRangeDb = -40.0F;

/// Moves `value` towards `target` by at most `step`.
[[nodiscard]] float approach(float value, float target, float step) noexcept {
    return value + std::clamp(target - value, -step, step);
}

} // namespace

ProcessingGraph::ProcessingGraph(const EngineConfig& config)
    : config_(config)
    , maxBlock_(std::max<std::uint32_t>(64, config.maxBlock)) {
    const double fs = kEngineRate;
    inputGain_.prepare(fs, kInputGainRampMs);
    inputGain_.setImmediate(1.0F);
    dcBlocker_.prepare(fs);
    suppressor_.prepare(fs);
    gate_.prepare(fs);
    gate_.setThresholdDb(kDefaultGateThresholdDb);
    gate_.setRangeDb(kGateRangeDb);
    transmit_.prepare(fs);
    feedback_.prepare(fs);
    crossfadeStep_ = 1000.0F / (kCrossfadeMs * static_cast<float>(fs));
    sounds_.prepare(fs);
    const MixLevels defaults;
    voiceLevel_.prepare(fs, kLevelRampMs);
    voiceLevel_.setImmediate(dsp::dbToGain(defaults.voiceDb));
    soundsLevel_.prepare(fs, kLevelRampMs);
    soundsLevel_.setImmediate(dsp::dbToGain(defaults.soundsDb));
    speechLevel_.prepare(fs, kLevelRampMs);
    speechLevel_.setImmediate(dsp::dbToGain(defaults.speechDb));
    monitorLevel_.prepare(fs, kLevelRampMs);
    monitorLevel_.setImmediate(dsp::dbToGain(defaults.monitorDb));
    micLimiter_.prepare(fs);
    micLimiter_.setCeilingDb(kLimiterCeilingDb);
    monitorLimiter_.prepare(fs);
    monitorLimiter_.setCeilingDb(kLimiterCeilingDb);
    for (std::vector<float>* buffer :
         {&in_, &dry_, &voiceA_, &voiceB_, &soundsAll_, &soundsMonitor_, &speechBuffer_}) {
        buffer->assign(maxBlock_, 0.0F);
    }
    inputMeter_.prepare(fs);
    outputMeter_.prepare(fs);
}

ProcessingGraph::~ProcessingGraph() {
    // No thread runs process() any more, so whatever the audio side still
    // holds is freed here.
    const std::unique_ptr<plugins::VoiceChain> current(current_);
    const std::unique_ptr<plugins::VoiceChain> next(next_);
    for (plugins::VoiceChain* chain : pendingChains_) {
        const std::unique_ptr<plugins::VoiceChain> pending(chain);
    }
    const std::unique_ptr<SpeechClip> speech(speechNow_);
    for (std::uint32_t id = 0; id < SoundboardPlayer::kMaxClips; ++id) {
        const std::unique_ptr<const Clip> clip(sounds_.remove(id));
    }
}

// ---------------------------------------------------------------- control

std::unique_ptr<plugins::VoiceChain>
ProcessingGraph::setVoiceChain(std::unique_ptr<plugins::VoiceChain> chain) {
    return chains_.send(std::move(chain));
}

bool ProcessingGraph::send(const Command& command) noexcept {
    return commands_.tryPush(command);
}

std::unique_ptr<ClipInstall> ProcessingGraph::installClip(std::unique_ptr<ClipInstall> install) {
    return clips_.send(std::move(install));
}

std::unique_ptr<SpeechClip> ProcessingGraph::playSpeech(std::unique_ptr<SpeechClip> speech) {
    return speechQueue_.send(std::move(speech));
}

void ProcessingGraph::collectGarbage() {
    chains_.collectGarbage();
    clips_.collectGarbage();
    speechQueue_.collectGarbage();
}

void ProcessingGraph::applyWhileStopped() noexcept {
    adoptObjects();
    applyCommands();
    // With no audio running, a pending crossfade would never finish.
    if (next_ != nullptr) {
        retire(current_);
        current_ = next_;
        next_ = nullptr;
        crossfade_ = 0.0F;
    }
}

// ---------------------------------------------------------------- any thread

bool ProcessingGraph::popEvent(EngineEvent& event) noexcept {
    const auto pending = events_.tryPop();
    if (!pending) {
        return false;
    }
    event.kind = pending->kind;
    event.value = pending->value;
    event.detail.clear();
    return true;
}

EngineStats ProcessingGraph::meters() const noexcept {
    EngineStats s;
    s.inputPeak = inputPeak_.load(std::memory_order_relaxed);
    s.inputRms = inputRms_.load(std::memory_order_relaxed);
    s.outputPeak = outputPeak_.load(std::memory_order_relaxed);
    s.gateOpen = gateOpen_.load(std::memory_order_relaxed);
    s.transmitting = transmitting_.load(std::memory_order_relaxed);
    return s;
}

std::size_t ProcessingGraph::suppressorLatencySamples() const noexcept {
    return suppressorActive_.load(std::memory_order_relaxed)
               ? dsp::NoiseSuppressor::latencySamples()
               : 0;
}

std::size_t ProcessingGraph::fixedLatencySamples() const noexcept {
    return suppressorLatencySamples() + limiterLatencySamples();
}

// ---------------------------------------------------------------- audio thread

void ProcessingGraph::pushEvent(EngineEventKind kind, std::uint32_t value) noexcept {
    // A full event queue means the control thread is not polling; dropping
    // the event is the only real-time-safe option.
    static_cast<void>(events_.tryPush(PendingEvent{kind, value}));
}

void ProcessingGraph::retire(plugins::VoiceChain* chain) noexcept {
    if (chain == nullptr || chains_.retire(chain)) {
        return;
    }
    for (plugins::VoiceChain*& slot : pendingChains_) {
        if (slot == nullptr) {
            slot = chain;
            return;
        }
    }
    // Every fallback slot is taken because the control thread has stopped
    // collecting. Keeping the chain alive is safe; it is a bounded leak that
    // the destructor reclaims.
}

void ProcessingGraph::adoptChain(plugins::VoiceChain* chain) noexcept {
    // A chain that arrives during a crossfade replaces the incoming one; the
    // outgoing chain keeps fading out from where it is.
    retire(next_);
    next_ = chain;
    crossfade_ = 0.0F;
    voiceLatency_.store(chain->latencySamples(), std::memory_order_relaxed);
}

void ProcessingGraph::adoptObjects() noexcept {
    for (plugins::VoiceChain*& slot : pendingChains_) {
        if (slot != nullptr && chains_.retire(slot)) {
            slot = nullptr;
        }
    }
    while (plugins::VoiceChain* chain = chains_.receive()) {
        adoptChain(chain);
    }
    while (ClipInstall* install = clips_.receive()) {
        install->replaced.reset(sounds_.install(install->id, install->clip.release()));
        if (!clips_.retire(install)) {
            // Cannot happen: the return queue is as large as the send queue
            // and every record is retired at most once per receive.
            break;
        }
    }
    if (speechNow_ == nullptr) {
        speechNow_ = speechQueue_.receive();
    }
}

void ProcessingGraph::apply(const Command& c) noexcept {
    using Type = Command::Type;
    switch (c.type) {
    case Type::VoiceParameter:
        for (plugins::VoiceChain* chain : {current_, next_}) {
            if (chain != nullptr) {
                chain->setParameter(c.a, c.b, c.x);
            }
        }
        break;
    case Type::VoiceTone:
        for (plugins::VoiceChain* chain : {current_, next_}) {
            if (chain != nullptr) {
                chain->setTone(c.x, c.y);
            }
        }
        break;
    case Type::VoiceEnabled:
        voiceEnabled_ = c.a != 0;
        break;
    case Type::Background:
        for (plugins::VoiceChain* chain : {current_, next_}) {
            if (chain != nullptr) {
                chain->setBackgroundEnabled(c.a != 0);
            }
        }
        break;
    case Type::BlockBypass:
        for (plugins::VoiceChain* chain : {current_, next_}) {
            if (chain != nullptr) {
                chain->setBlockBypassed(c.a, c.b != 0);
            }
        }
        break;
    case Type::InputGain:
        inputGain_.setTarget(dsp::dbToGain(c.x));
        break;
    case Type::NoiseSuppression:
        suppressorOn_ = c.a != 0;
        suppressorStrength_ = std::clamp(c.x, 0.0F, 1.0F);
        break;
    case Type::Gate:
        gateOn_ = c.a != 0;
        gate_.setThresholdDb(c.x);
        break;
    case Type::HearMyself:
        hearMyself_ = c.a != 0;
        feedback_.acknowledge();
        break;
    case Type::FeedbackGuard:
        feedbackGuard_ = c.a != 0;
        break;
    case Type::Mix:
        voiceLevel_.setTarget(dsp::dbToGain(c.x));
        soundsLevel_.setTarget(dsp::dbToGain(c.y));
        speechLevel_.setTarget(dsp::dbToGain(c.z));
        monitorLevel_.setTarget(dsp::dbToGain(c.w));
        break;
    case Type::TriggerSound:
        sounds_.trigger(c.a, c.options, c.b != 0);
        break;
    case Type::StopSound:
        sounds_.stop(c.a);
        break;
    case Type::StopAllSounds:
        sounds_.stopAll();
        break;
    case Type::StopSpeech:
        if (speechNow_ != nullptr) {
            speechNow_->position = speechNow_->samples.size();
        }
        break;
    }
}

void ProcessingGraph::applyCommands() noexcept {
    while (const auto command = commands_.tryPop()) {
        apply(*command);
    }
}

void ProcessingGraph::process(std::span<const float> input, std::span<float> virtualMic,
                              std::span<float> monitor) noexcept {
    // Objects first: a sound loaded and then triggered from the control
    // thread must be in its slot when the trigger command runs.
    adoptObjects();
    applyCommands();
    for (std::size_t pos = 0; pos < virtualMic.size(); pos += maxBlock_) {
        const std::size_t n = std::min<std::size_t>(maxBlock_, virtualMic.size() - pos);
        const auto in = input.empty() ? std::span<const float>{} : input.subspan(pos, n);
        processBlock(in, virtualMic.subspan(pos, n), monitor.subspan(pos, n));
    }
    for (const std::uint32_t id : sounds_.takeFinished()) {
        pushEvent(EngineEventKind::SoundFinished, id);
    }
    publishMeters();
}

void ProcessingGraph::conditionInput(std::span<const float> input, std::span<float> in) noexcept {
    if (input.empty()) {
        std::fill(in.begin(), in.end(), 0.0F);
    } else {
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] = dcBlocker_.processSample(input[i] * inputGain_.next());
        }
    }
    inputMeter_.process(in);

    // RNNoise runs all the time so its frame state is current when it is
    // switched on. Off means the undelayed input (no added latency); the
    // switch crossfades between the two over 20 ms.
    auto dry = std::span<float>(dry_).first(in.size());
    std::copy(in.begin(), in.end(), dry.begin());
    suppressor_.setStrength(suppressorStrength_);
    suppressor_.process(in);
    const float target = suppressorOn_ ? 1.0F : 0.0F;
    if (target < 1.0F || suppressorMix_ < 1.0F) {
        for (std::size_t i = 0; i < in.size(); ++i) {
            suppressorMix_ = approach(suppressorMix_, target, crossfadeStep_);
            in[i] = dry[i] + suppressorMix_ * (in[i] - dry[i]);
        }
    }
    suppressorActive_.store(suppressorMix_ > 0.5F, std::memory_order_relaxed);

    if (gateOn_) {
        gate_.process(in);
    }
    if (hearMyself_ && feedbackGuard_) {
        feedback_.process(in);
        if (feedback_.feedbackDetected()) {
            hearMyself_ = false;
            feedback_.acknowledge();
            pushEvent(EngineEventKind::FeedbackDetected,
                      static_cast<std::uint32_t>(feedback_.peakFrequencyHz()));
        }
    }
    transmit_.process(in);
}

void ProcessingGraph::renderSpeech(std::span<float> in, std::span<float> speech) noexcept {
    std::fill(speech.begin(), speech.end(), 0.0F);
    if (speechNow_ == nullptr) {
        return;
    }
    const std::size_t size = speechNow_->samples.size();
    const std::size_t from = std::min(speechNow_->position, size);
    const std::size_t take = std::min(size - from, speech.size());
    std::copy_n(speechNow_->samples.begin() + static_cast<std::ptrdiff_t>(from), take,
                speech.begin());
    speechNow_->position = from + take;
    for (float& s : speech) {
        s *= speechLevel_.next();
    }
    if (speechNow_->throughVoice) {
        // Speech replaces nothing: it is mixed into the voice input, after
        // push-to-talk so that a muted microphone does not silence it.
        for (std::size_t i = 0; i < in.size(); ++i) {
            in[i] += speech[i];
        }
        std::fill(speech.begin(), speech.end(), 0.0F);
    }
    if (speechNow_->position >= size && speechQueue_.retire(speechNow_)) {
        speechNow_ = speechQueue_.receive();
        pushEvent(EngineEventKind::SpeechFinished, 0);
    }
}

void ProcessingGraph::renderVoice(std::span<const float> in, std::span<float> voice) noexcept {
    std::copy(in.begin(), in.end(), voice.begin());
    if (current_ != nullptr) {
        current_->process(voice);
    }
    if (next_ != nullptr) {
        auto incoming = std::span<float>(voiceB_).first(in.size());
        std::copy(in.begin(), in.end(), incoming.begin());
        next_->process(incoming);
        // Constant-gain (linear) crossfade: both voices come from the same
        // microphone and are strongly correlated, so an equal-power fade
        // would swell by up to 3 dB halfway through.
        for (std::size_t i = 0; i < voice.size(); ++i) {
            crossfade_ = std::min(1.0F, crossfade_ + crossfadeStep_);
            voice[i] += crossfade_ * (incoming[i] - voice[i]);
        }
        if (crossfade_ >= 1.0F) {
            retire(current_);
            current_ = next_;
            next_ = nullptr;
        }
    }
    const float wetTarget = voiceEnabled_ ? 1.0F : 0.0F;
    if (voiceWet_ < 1.0F || wetTarget < 1.0F) {
        for (std::size_t i = 0; i < voice.size(); ++i) {
            voiceWet_ = approach(voiceWet_, wetTarget, crossfadeStep_);
            voice[i] = in[i] + voiceWet_ * (voice[i] - in[i]);
        }
    }
}

void ProcessingGraph::processBlock(std::span<const float> input, std::span<float> virtualMic,
                                   std::span<float> monitor) noexcept {
    const std::size_t n = virtualMic.size();
    auto in = std::span<float>(in_).first(n);
    conditionInput(input, in);

    auto speech = std::span<float>(speechBuffer_).first(n);
    renderSpeech(in, speech);

    auto voice = std::span<float>(voiceA_).first(n);
    renderVoice(in, voice);

    auto soundsAll = std::span<float>(soundsAll_).first(n);
    auto soundsMonitor = std::span<float>(soundsMonitor_).first(n);
    sounds_.process(soundsAll, soundsMonitor);
    // The player updates its duck once per block; ramp across the block.
    const float duckStep = (sounds_.voiceDuck() - duck_) / static_cast<float>(n);
    const float hearTarget = hearMyself_ ? 1.0F : 0.0F;
    for (std::size_t i = 0; i < n; ++i) {
        duck_ += duckStep;
        const float v = voice[i] * voiceLevel_.next() * duck_;
        const float s = soundsLevel_.next();
        hearGain_ = approach(hearGain_, hearTarget, crossfadeStep_);
        virtualMic[i] = v + soundsAll[i] * s + speech[i];
        monitor[i] = (v * hearGain_ + soundsMonitor[i] * s + speech[i]) * monitorLevel_.next();
    }
    micLimiter_.process(virtualMic);
    monitorLimiter_.process(monitor);
    outputMeter_.process(virtualMic);
}

void ProcessingGraph::publishMeters() noexcept {
    inputPeak_.store(inputMeter_.peak(), std::memory_order_relaxed);
    inputRms_.store(inputMeter_.rms(), std::memory_order_relaxed);
    outputPeak_.store(outputMeter_.peak(), std::memory_order_relaxed);
    gateOpen_.store(!gateOn_ || gate_.isOpen(), std::memory_order_relaxed);
    transmitting_.store(transmit_.transmitting(), std::memory_order_relaxed);
}

} // namespace vox::engine
