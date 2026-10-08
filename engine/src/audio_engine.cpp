#include "vox/engine/audio_engine.hpp"

#include "vox/engine/output_stage.hpp"

#include <vox/core/denormals.hpp>
#include <vox/dsp/resampler.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace vox::engine {
namespace {

constexpr std::uint32_t kMinDeviceRate = 8000;
constexpr std::uint32_t kMaxDeviceRate = 384000;
constexpr std::uint32_t kMaxDeviceChannels = 64;
/// Scheduling jitter allowance in the ring buffers.
constexpr double kSafetyMs = 2.0;
/// Engine frames rendered per step in pull mode.
constexpr std::size_t kPullChunk = 128;
constexpr float kLoadSmoothing = 0.05F;

const char* roleName(DeviceRole role) {
    switch (role) {
    case DeviceRole::Input:
        return "Microphone";
    case DeviceRole::VirtualMic:
        return "Virtual microphone output";
    case DeviceRole::Monitor:
        return "Headphones";
    }
    return "Device";
}

Error withRole(Error error, DeviceRole role) {
    error.message = std::string(roleName(role)) + ": " + error.message;
    return error;
}

Status validateFormat(const devices::StreamInfo& info, DeviceRole role) {
    if (info.sampleRate < kMinDeviceRate || info.sampleRate > kMaxDeviceRate) {
        return withRole(makeError(ErrorCode::UnsupportedSampleRate,
                                  "\"" + info.deviceName + "\" runs at " +
                                      std::to_string(info.sampleRate) +
                                      " Hz, which is outside the supported 8 to 384 kHz. "
                                      "Set it to 48000 Hz in the system sound settings."),
                        role);
    }
    if (info.channels == 0 || info.channels > kMaxDeviceChannels) {
        return withRole(makeError(ErrorCode::UnsupportedChannelCount,
                                  "\"" + info.deviceName + "\" reports " +
                                      std::to_string(info.channels) +
                                      " channels. Choose a mono or stereo device."),
                        role);
    }
    return {};
}

/// Frames after converting `frames` by `ratio` (output rate / input rate), rounded up.
std::size_t convertedFrames(double frames, double ratio) {
    return static_cast<std::size_t>(std::ceil(frames * ratio));
}

Error queueFull() {
    return makeError(ErrorCode::CommandQueueFull,
                     "The audio engine is not keeping up with changes, so this one was "
                     "dropped. If this repeats, the audio device has probably stopped; "
                     "reselect it in Settings > Audio.");
}

} // namespace

// ---------------------------------------------------------------- ports

class AudioEngine::InputPort final : public devices::CaptureHandler {
public:
    explicit InputPort(AudioEngine& engine)
        : engine_(engine) {}
    InputPort(const InputPort&) = delete;
    InputPort& operator=(const InputPort&) = delete;
    InputPort(InputPort&&) = delete;
    InputPort& operator=(InputPort&&) = delete;
    ~InputPort() override = default;

    void onCapture(const float* interleaved, std::size_t frames,
                   std::uint32_t channels) noexcept override {
        engine_.processCapture(interleaved, frames, channels);
    }

    dsp::StreamResampler resampler;
    bool resample = false;
    std::size_t resamplerDelay = 0; ///< Engine frames.
    std::vector<float> mono;
    // Declared last so it is destroyed (and stopped) first.
    std::unique_ptr<devices::Stream> stream;

private:
    AudioEngine& engine_;
};

class AudioEngine::OutputPort final : public devices::PlaybackHandler {
public:
    explicit OutputPort(AudioEngine& engine)
        : engine_(engine) {}
    OutputPort(const OutputPort&) = delete;
    OutputPort& operator=(const OutputPort&) = delete;
    OutputPort(OutputPort&&) = delete;
    OutputPort& operator=(OutputPort&&) = delete;
    ~OutputPort() override = default;

    void onPlayback(float* interleaved, std::size_t frames,
                    std::uint32_t channels) noexcept override {
        const double now = engine_.backend_.now();
        if (driving) {
            engine_.renderForPull(*this, frames, now);
        }
        stage.pull(interleaved, frames, channels, now);
    }

    OutputStage stage;
    bool driving = false; ///< Set before the stream starts, constant while it runs.
    std::unique_ptr<devices::Stream> stream;

private:
    AudioEngine& engine_;
};

// ---------------------------------------------------------------- lifetime

AudioEngine::AudioEngine(devices::AudioBackend& backend, const EngineConfig& config)
    : backend_(backend)
    , config_(config)
    , graph_(config) {
    backend_.setEventCallback([this](const devices::DeviceEvent& event) { onDeviceEvent(event); });
}

AudioEngine::~AudioEngine() {
    backend_.setEventCallback({});
    stop();
}

Result<std::vector<devices::DeviceInfo>> AudioEngine::devices(devices::DeviceKind kind) const {
    return backend_.enumerate(kind);
}

Status AudioEngine::start(const DeviceSelection& selection) {
    stop();
    selection_ = selection;
    lost_ = {};
    if (auto opened = openStreams(selection_); !opened) {
        return opened;
    }
    running_ = true;
    return {};
}

void AudioEngine::stop() noexcept {
    closeStreams();
    running_ = false;
    lost_ = {};
    // Apply whatever was queued for the stopped audio thread.
    graph_.applyWhileStopped();
}

AudioEngine::ActiveDevices AudioEngine::activeDevices() const {
    ActiveDevices active;
    if (input_ && input_->stream) {
        active.input = input_->stream->info();
    }
    if (virtualMic_ && virtualMic_->stream) {
        active.virtualMic = virtualMic_->stream->info();
    }
    if (monitor_ && monitor_->stream) {
        active.monitor = monitor_->stream->info();
    }
    active.pullMode = pullDriver_ != nullptr;
    return active;
}

DeviceSelection AudioEngine::effectiveSelection() const {
    DeviceSelection s = selection_;
    s.useInput = s.useInput && !lost_.input;
    s.useVirtualMic = s.useVirtualMic && !lost_.virtualMic;
    s.useMonitor = s.useMonitor && !lost_.monitor;
    return s;
}

// ---------------------------------------------------------------- streams

Status AudioEngine::openStreams(const DeviceSelection& selection) {
    closeStreams();
    const devices::StreamConfig base{{}, 0, 0, config_.periodFrames, config_.exclusive};

    if (selection.useInput) {
        input_ = std::make_unique<InputPort>(*this);
        devices::StreamConfig c = base;
        c.deviceId = selection.inputId;
        auto stream = backend_.openCapture(c, *input_);
        if (!stream) {
            closeStreams();
            return withRole(stream.error(), DeviceRole::Input);
        }
        input_->stream = std::move(stream).value();
        if (auto ok = validateFormat(input_->stream->info(), DeviceRole::Input); !ok) {
            closeStreams();
            return ok;
        }
    }
    const auto openOutput = [&](const std::string& id, DeviceRole role,
                                std::unique_ptr<OutputPort>& port) -> Status {
        port = std::make_unique<OutputPort>(*this);
        devices::StreamConfig c = base;
        c.deviceId = id;
        auto stream = backend_.openPlayback(c, *port);
        if (!stream) {
            return withRole(stream.error(), role);
        }
        port->stream = std::move(stream).value();
        return validateFormat(port->stream->info(), role);
    };
    if (selection.useVirtualMic) {
        if (auto ok = openOutput(selection.virtualMicId, DeviceRole::VirtualMic, virtualMic_);
            !ok) {
            closeStreams();
            return ok;
        }
    }
    if (selection.useMonitor) {
        if (auto ok = openOutput(selection.monitorId, DeviceRole::Monitor, monitor_); !ok) {
            closeStreams();
            return ok;
        }
    }

    // Size buffers and ring targets from what the devices actually gave us.
    const std::size_t maxBlock = graph_.maxBlock();
    if (input_) {
        const auto& info = input_->stream->info();
        const double ratio = kEngineRate / static_cast<double>(info.sampleRate);
        input_->resample = info.sampleRate != static_cast<std::uint32_t>(kEngineRate);
        input_->resampler.prepare(dsp::ResamplerQuality::Medium, ratio);
        input_->resamplerDelay = input_->resample ? dsp::measureHoldBack(input_->resampler) : 0;
        input_->mono.assign(maxBlock, 0.0F);
        engineChunk_ = convertedFrames(static_cast<double>(maxBlock), ratio) + 64;
    } else {
        pullDriver_ = virtualMic_ ? virtualMic_.get() : monitor_.get();
        engineChunk_ = std::max(maxBlock, kPullChunk);
    }
    engineIn_.assign(engineChunk_, 0.0F);
    micOut_.assign(engineChunk_, 0.0F);
    monitorOut_.assign(engineChunk_, 0.0F);
    for (OutputPort* port : {virtualMic_.get(), monitor_.get()}) {
        if (port == nullptr) {
            continue;
        }
        // The stage measures its fill as if the device read continuously,
        // just before each producer callback writes; at that point only
        // scheduling jitter needs covering (see OutputStage).
        const auto rate = static_cast<double>(port->stream->info().sampleRate);
        const auto target = static_cast<std::size_t>(std::ceil(kSafetyMs * rate / 1000.0));
        port->driving = port == pullDriver_;
        port->stage.prepare(kEngineRate, rate, target, engineChunk_);
        port->stage.setSynchronous(port->driving);
    }

    // Outputs first, so the input never pushes into a stopped device.
    for (const auto& [port, role] : {std::pair{virtualMic_.get(), DeviceRole::VirtualMic},
                                     std::pair{monitor_.get(), DeviceRole::Monitor}}) {
        if (port == nullptr) {
            continue;
        }
        if (auto ok = port->stream->start(); !ok) {
            closeStreams();
            return withRole(ok.error(), role);
        }
    }
    if (input_) {
        if (auto ok = input_->stream->start(); !ok) {
            closeStreams();
            return withRole(ok.error(), DeviceRole::Input);
        }
    }
    return {};
}

void AudioEngine::closeStreams() noexcept {
    // Stop every callback before destroying anything a callback can reach.
    if (input_ && input_->stream) {
        input_->stream->stop();
    }
    for (OutputPort* port : {virtualMic_.get(), monitor_.get()}) {
        if (port != nullptr && port->stream) {
            port->stream->stop();
        }
    }
    input_.reset();
    virtualMic_.reset();
    monitor_.reset();
    pullDriver_ = nullptr;
}

// ---------------------------------------------------------------- audio threads

void AudioEngine::runGraph(std::span<const float> input, std::size_t frames, double now) noexcept {
    if (frames == 0) {
        return;
    }
    const auto mic = std::span<float>(micOut_).first(frames);
    const auto monitor = std::span<float>(monitorOut_).first(frames);
    graph_.process(input, mic, monitor);
    if (virtualMic_) {
        virtualMic_->stage.push(mic, now);
    }
    if (monitor_) {
        monitor_->stage.push(monitor, now);
    }
}

void AudioEngine::processCapture(const float* interleaved, std::size_t frames,
                                 std::uint32_t channels) noexcept {
    const ScopedNoDenormals noDenormals;
    const auto started = std::chrono::steady_clock::now();
    const double now = backend_.now();
    InputPort& port = *input_;
    const std::uint32_t ch = std::max<std::uint32_t>(1, channels);
    const float scale = 1.0F / static_cast<float>(ch);
    std::size_t engineFrames = 0;
    for (std::size_t done = 0; done < frames;) {
        const std::size_t n = std::min(frames - done, port.mono.size());
        // Mono by averaging: a stereo microphone with identical channels
        // keeps its level, and one silent channel costs 6 dB, not a dropout.
        for (std::size_t i = 0; i < n; ++i) {
            float sum = 0.0F;
            for (std::uint32_t c = 0; c < ch; ++c) {
                sum += interleaved[(done + i) * ch + c];
            }
            port.mono[i] = sum * scale;
        }
        const auto mono = std::span<const float>(port.mono).first(n);
        if (!port.resample) {
            runGraph(mono, n, now);
            engineFrames += n;
        } else {
            for (std::size_t offset = 0; offset < n;) {
                const auto counts = port.resampler.process(mono.subspan(offset), engineIn_);
                runGraph(std::span<const float>(engineIn_).first(counts.produced), counts.produced,
                         now);
                engineFrames += counts.produced;
                offset += counts.consumed;
                if (counts.consumed == 0 && counts.produced == 0) {
                    break;
                }
            }
        }
        done += n;
    }
    if (engineFrames > 0) {
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const auto instant =
            static_cast<float>(elapsed * kEngineRate / static_cast<double>(engineFrames));
        const float previous = load_.load(std::memory_order_relaxed);
        load_.store(previous + kLoadSmoothing * (instant - previous), std::memory_order_relaxed);
    }
}

void AudioEngine::renderForPull(OutputPort& driver, std::size_t frames, double now) noexcept {
    const ScopedNoDenormals noDenormals;
    const auto started = std::chrono::steady_clock::now();
    const std::size_t want = driver.stage.targetFrames() + frames;
    std::size_t engineFrames = 0;
    // Bounded: each step adds about kPullChunk device frames.
    for (int guard = 0; driver.stage.fill() < want && guard < 256; ++guard) {
        runGraph({}, kPullChunk, now);
        engineFrames += kPullChunk;
    }
    if (engineFrames > 0) {
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        const auto instant =
            static_cast<float>(elapsed * kEngineRate / static_cast<double>(engineFrames));
        const float previous = load_.load(std::memory_order_relaxed);
        load_.store(previous + kLoadSmoothing * (instant - previous), std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------- commands

Status AudioEngine::send(const Command& command) {
    if (!graph_.send(command)) {
        return queueFull();
    }
    afterSend();
    return {};
}

void AudioEngine::afterSend() noexcept {
    const bool driven = running_ && (input_ || virtualMic_ || monitor_);
    if (!driven) {
        graph_.applyWhileStopped();
    }
}

Status AudioEngine::setVoice(const plugins::VoicePreset& preset,
                             const plugins::VoiceSettings& settings,
                             const plugins::EffectRegistry& registry) {
    plugins::PrepareContext context;
    context.sampleRate = kEngineRate;
    context.maxBlockSize = graph_.maxBlock();
    context.minVoiceHz = config_.minVoiceHz;
    auto chain = plugins::VoiceChain::build(preset, settings, registry, context);
    if (!chain) {
        return chain.error();
    }
    if (graph_.setVoiceChain(std::move(chain).value()) != nullptr) {
        return queueFull();
    }
    afterSend();
    return {};
}

Status AudioEngine::setVoiceParameter(std::size_t block, std::size_t param, float value) {
    Command c;
    c.type = Command::Type::VoiceParameter;
    c.a = static_cast<std::uint32_t>(block);
    c.b = static_cast<std::uint32_t>(param);
    c.x = value;
    return send(c);
}

Status AudioEngine::setVoiceTone(float bassDb, float trebleDb) {
    Command c;
    c.type = Command::Type::VoiceTone;
    c.x = bassDb;
    c.y = trebleDb;
    return send(c);
}

Status AudioEngine::setVoiceEnabled(bool enabled) {
    Command c;
    c.type = Command::Type::VoiceEnabled;
    c.a = enabled ? 1U : 0U;
    return send(c);
}

Status AudioEngine::setBackgroundEnabled(bool enabled) {
    Command c;
    c.type = Command::Type::Background;
    c.a = enabled ? 1U : 0U;
    return send(c);
}

Status AudioEngine::setBlockBypassed(std::size_t block, bool bypassed) {
    Command c;
    c.type = Command::Type::BlockBypass;
    c.a = static_cast<std::uint32_t>(block);
    c.b = bypassed ? 1U : 0U;
    return send(c);
}

Status AudioEngine::setInputGainDb(float db) {
    Command c;
    c.type = Command::Type::InputGain;
    c.x = std::clamp(db, -24.0F, 24.0F);
    return send(c);
}

Status AudioEngine::setNoiseSuppression(bool enabled, float strength) {
    Command c;
    c.type = Command::Type::NoiseSuppression;
    c.a = enabled ? 1U : 0U;
    c.x = strength;
    return send(c);
}

Status AudioEngine::setGate(bool enabled, float thresholdDb) {
    Command c;
    c.type = Command::Type::Gate;
    c.a = enabled ? 1U : 0U;
    c.x = std::clamp(thresholdDb, -90.0F, 0.0F);
    return send(c);
}

Status AudioEngine::setHearMyself(bool enabled) {
    Command c;
    c.type = Command::Type::HearMyself;
    c.a = enabled ? 1U : 0U;
    return send(c);
}

Status AudioEngine::setFeedbackGuard(bool enabled) {
    Command c;
    c.type = Command::Type::FeedbackGuard;
    c.a = enabled ? 1U : 0U;
    return send(c);
}

Status AudioEngine::setMixLevels(const MixLevels& levels) {
    Command c;
    c.type = Command::Type::Mix;
    c.x = std::clamp(levels.voiceDb, -60.0F, 12.0F);
    c.y = std::clamp(levels.soundsDb, -60.0F, 12.0F);
    c.z = std::clamp(levels.speechDb, -60.0F, 12.0F);
    c.w = std::clamp(levels.monitorDb, -60.0F, 12.0F);
    return send(c);
}

// ---------------------------------------------------------------- sounds and speech

namespace {

Status checkSoundId(std::uint32_t id) {
    if (id >= SoundboardPlayer::kMaxClips) {
        return makeError(ErrorCode::InvalidArgument,
                         "Sound slot " + std::to_string(id) + " does not exist (slots are 0 to " +
                             std::to_string(SoundboardPlayer::kMaxClips - 1) + ").");
    }
    return {};
}

} // namespace

Status AudioEngine::loadSound(std::uint32_t id, std::vector<float> samples) {
    if (auto ok = checkSoundId(id); !ok) {
        return ok;
    }
    if (samples.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "The sound contains no audio, so there is nothing to play.");
    }
    auto install = std::make_unique<ClipInstall>();
    install->id = id;
    install->clip = std::make_unique<Clip>(Clip{std::move(samples)});
    if (graph_.installClip(std::move(install)) != nullptr) {
        return queueFull();
    }
    afterSend();
    return {};
}

Status AudioEngine::unloadSound(std::uint32_t id) {
    if (auto ok = checkSoundId(id); !ok) {
        return ok;
    }
    auto install = std::make_unique<ClipInstall>();
    install->id = id;
    if (graph_.installClip(std::move(install)) != nullptr) {
        return queueFull();
    }
    afterSend();
    return {};
}

Status AudioEngine::triggerSound(std::uint32_t id, const SoundOptions& options, bool keyDown) {
    if (auto ok = checkSoundId(id); !ok) {
        return ok;
    }
    Command c;
    c.type = Command::Type::TriggerSound;
    c.a = id;
    c.b = keyDown ? 1U : 0U;
    c.options = options;
    return send(c);
}

Status AudioEngine::stopSound(std::uint32_t id) {
    if (auto ok = checkSoundId(id); !ok) {
        return ok;
    }
    Command c;
    c.type = Command::Type::StopSound;
    c.a = id;
    return send(c);
}

Status AudioEngine::stopAllSounds() {
    Command c;
    c.type = Command::Type::StopAllSounds;
    return send(c);
}

Status AudioEngine::playSpeech(std::vector<float> samples, bool throughVoice) {
    if (samples.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         "The speech synthesizer returned no audio for this text.");
    }
    auto speech = std::make_unique<SpeechClip>();
    speech->samples = std::move(samples);
    speech->throughVoice = throughVoice;
    if (graph_.playSpeech(std::move(speech)) != nullptr) {
        return queueFull();
    }
    afterSend();
    return {};
}

Status AudioEngine::stopSpeech() {
    Command c;
    c.type = Command::Type::StopSpeech;
    return send(c);
}

// ---------------------------------------------------------------- housekeeping

void AudioEngine::onDeviceEvent(const devices::DeviceEvent& event) {
    // Any thread (the backend's notification thread). Only queue it.
    const std::scoped_lock lock(deviceEventsMutex_);
    deviceEvents_.push_back(event);
}

std::vector<EngineEvent> AudioEngine::poll() {
    std::vector<EngineEvent> out;
    graph_.collectGarbage();
    EngineEvent event;
    while (graph_.popEvent(event)) {
        out.push_back(event);
    }
    handleDeviceEvents(out);
    return out;
}

void AudioEngine::handleDeviceEvents(std::vector<EngineEvent>& out) {
    std::vector<devices::DeviceEvent> events;
    {
        const std::scoped_lock lock(deviceEventsMutex_);
        events.swap(deviceEvents_);
    }
    if (!running_ || events.empty()) {
        return;
    }
    const auto matches = [](const auto& port, const std::string& id) {
        return port && port->stream && port->stream->info().deviceId == id;
    };
    bool reopen = false;
    bool listChanged = false;
    for (const devices::DeviceEvent& e : events) {
        switch (e.kind) {
        case devices::DeviceEventKind::StreamStopped: {
            const auto lose = [&](bool& lost, DeviceRole role, const std::string& name) {
                lost = true;
                reopen = true;
                out.push_back(
                    {EngineEventKind::DeviceLost, static_cast<std::uint32_t>(role), name});
            };
            if (matches(input_, e.deviceId)) {
                lose(lost_.input, DeviceRole::Input, input_->stream->info().deviceName);
            }
            if (matches(virtualMic_, e.deviceId)) {
                lose(lost_.virtualMic, DeviceRole::VirtualMic,
                     virtualMic_->stream->info().deviceName);
            }
            if (matches(monitor_, e.deviceId)) {
                lose(lost_.monitor, DeviceRole::Monitor, monitor_->stream->info().deviceName);
            }
            break;
        }
        case devices::DeviceEventKind::StreamRerouted:
            // The OS moved a default-device stream; its format may differ now.
            reopen = reopen || matches(input_, e.deviceId) || matches(virtualMic_, e.deviceId) ||
                     matches(monitor_, e.deviceId);
            break;
        case devices::DeviceEventKind::DeviceListChanged:
            listChanged = true;
            break;
        }
    }
    const Lost before = lost_;
    if (listChanged && lost_.any()) {
        tryRecover(out);
        reopen = true;
    }
    if (!reopen) {
        return;
    }
    auto ok = openStreams(effectiveSelection());
    if (!ok && (before.input != lost_.input || before.virtualMic != lost_.virtualMic ||
                before.monitor != lost_.monitor)) {
        // A device that reappeared may not be ready yet; carry on without it
        // and try again on the next device change.
        std::erase_if(
            out, [](const EngineEvent& e) { return e.kind == EngineEventKind::DeviceRestored; });
        lost_ = before;
        ok = openStreams(effectiveSelection());
    }
    if (!ok) {
        closeStreams();
        running_ = false;
        out.push_back({EngineEventKind::RestartFailed, 0, ok.error().message});
    }
}

void AudioEngine::tryRecover(std::vector<EngineEvent>& out) {
    const auto present = [this](devices::DeviceKind kind, const std::string& id) {
        const auto list = backend_.enumerate(kind);
        if (!list) {
            return false;
        }
        return std::any_of(
            list.value().begin(), list.value().end(),
            [&id](const devices::DeviceInfo& d) { return id.empty() ? d.isDefault : d.id == id; });
    };
    const auto restore = [&](bool& lost, devices::DeviceKind kind, const std::string& id,
                             DeviceRole role) {
        if (lost && present(kind, id)) {
            lost = false;
            out.push_back({EngineEventKind::DeviceRestored, static_cast<std::uint32_t>(role), id});
        }
    };
    restore(lost_.input, devices::DeviceKind::Capture, selection_.inputId, DeviceRole::Input);
    restore(lost_.virtualMic, devices::DeviceKind::Playback, selection_.virtualMicId,
            DeviceRole::VirtualMic);
    restore(lost_.monitor, devices::DeviceKind::Playback, selection_.monitorId,
            DeviceRole::Monitor);
}

EngineStats AudioEngine::stats() const {
    EngineStats s = graph_.meters();
    s.processingLoad = static_cast<double>(load_.load(std::memory_order_relaxed));
    const auto ms = [](double frames, double rate) {
        return 1000.0 * frames / rate;
    };
    LatencyBreakdown common;
    if (input_ && input_->stream) {
        const auto& info = input_->stream->info();
        common.captureMs = ms(static_cast<double>(info.periodFrames), info.sampleRate);
        if (input_->resample) {
            common.inputResamplerMs = ms(static_cast<double>(input_->resamplerDelay), kEngineRate);
        }
    }
    common.noiseSuppressionMs =
        ms(static_cast<double>(graph_.suppressorLatencySamples()), kEngineRate);
    common.voiceMs = ms(static_cast<double>(graph_.voiceLatencySamples()), kEngineRate);
    common.limiterMs = ms(static_cast<double>(graph_.limiterLatencySamples()), kEngineRate);
    const auto path = [&](const std::unique_ptr<OutputPort>& port) {
        LatencyBreakdown b;
        if (!port || !port->stream) {
            return b;
        }
        b = common;
        const auto& info = port->stream->info();
        const auto rate = static_cast<double>(info.sampleRate);
        b.bufferMs = ms(static_cast<double>(port->stage.targetFrames()), rate);
        b.outputResamplerMs = ms(static_cast<double>(port->stage.resamplerDelayFrames()), rate);
        b.playbackMs = ms(static_cast<double>(info.periodFrames), rate);
        return b;
    };
    s.virtualMicLatency = path(virtualMic_);
    s.monitorLatency = path(monitor_);
    s.estimatedLatencyMs = s.virtualMicLatency.totalMs();
    s.monitorLatencyMs = s.monitorLatency.totalMs();
    for (const auto* port : {virtualMic_.get(), monitor_.get()}) {
        if (port != nullptr) {
            s.underruns += port->stage.underruns();
            s.overruns += port->stage.overruns();
        }
    }
    return s;
}

} // namespace vox::engine
