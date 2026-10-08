#include "virtual_mic_check.hpp"

#include <vox/engine/audio_engine.hpp>
#include <vox/engine/types.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace vox::app {
namespace {

constexpr double kRate = engine::kEngineRate;
constexpr std::size_t kMaxRecording = std::size_t{48000} * 4;
/// Above this the chirp is clearly there; noise and speech stay far below.
constexpr double kMatchThreshold = 0.5;
/// Below this (peak) nothing arrived at all.
constexpr float kSilence = 1e-4F;

/// Averages groups of four samples (48 to 12 kHz). The chirp stays below
/// 4 kHz, where the average loses less than 2 dB, and the correlation
/// below gets 16 times cheaper.
std::vector<float> decimate(const std::vector<float>& x) {
    std::vector<float> out(x.size() / 4);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = 0.25F * (x[4 * i] + x[4 * i + 1] + x[4 * i + 2] + x[4 * i + 3]);
    }
    return out;
}

} // namespace

VirtualMicCheck::VirtualMicCheck(devices::AudioBackend& backend, engine::AudioEngine& engine,
                                 QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , engine_(engine)
    , captured_(kMaxRecording) {
    listen_.setSingleShot(true);
    listen_.setInterval(2000);
    poll_.setInterval(50);
    connect(&poll_, &QTimer::timeout, this, &VirtualMicCheck::drain);
    connect(&listen_, &QTimer::timeout, this, &VirtualMicCheck::finish);
}

VirtualMicCheck::~VirtualMicCheck() {
    if (stream_) {
        stream_->stop();
    }
}

std::vector<float> VirtualMicCheck::probe() {
    // 0.3 s exponential sweep from 400 Hz to 4 kHz at -12 dBFS with 10 ms
    // fades: inside every telephone and voice codec band, and unlike speech.
    constexpr double kSeconds = 0.3;
    constexpr double kStart = 400.0;
    constexpr double kEnd = 4000.0;
    const auto n = static_cast<std::size_t>(kSeconds * kRate);
    const double k = std::log(kEnd / kStart);
    std::vector<float> out(n);
    const auto fade = static_cast<std::size_t>(0.01 * kRate);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        const double phase =
            2.0 * std::numbers::pi * kStart * kSeconds / k * (std::exp(t / kSeconds * k) - 1.0);
        double gain = 0.25;
        if (i < fade) {
            gain *= static_cast<double>(i) / static_cast<double>(fade);
        } else if (n - i < fade) {
            gain *= static_cast<double>(n - i) / static_cast<double>(fade);
        }
        out[i] = static_cast<float>(gain * std::sin(phase));
    }
    return out;
}

double VirtualMicCheck::matchStrength(const std::vector<float>& fullRecording,
                                      const std::vector<float>& fullProbe) {
    const std::vector<float> recording = decimate(fullRecording);
    const std::vector<float> probe = decimate(fullProbe);
    if (recording.size() < probe.size() || probe.empty()) {
        return 0.0;
    }
    const double probeEnergy = std::inner_product(
        probe.begin(), probe.end(), probe.begin(), 0.0, std::plus<>(),
        [](float a, float b) { return static_cast<double>(a) * static_cast<double>(b); });
    double windowEnergy = 0.0;
    for (std::size_t i = 0; i < probe.size(); ++i) {
        windowEnergy += static_cast<double>(recording[i]) * static_cast<double>(recording[i]);
    }
    double best = 0.0;
    const std::size_t last = recording.size() - probe.size();
    for (std::size_t lag = 0; lag <= last; ++lag) {
        if (lag > 0) {
            const auto out = static_cast<double>(recording[lag - 1]);
            const auto in = static_cast<double>(recording[lag + probe.size() - 1]);
            windowEnergy = std::max(0.0, windowEnergy - out * out + in * in);
        }
        if (windowEnergy <= 1e-12) {
            continue;
        }
        double dot = 0.0;
        for (std::size_t i = 0; i < probe.size(); ++i) {
            dot += static_cast<double>(recording[lag + i]) * static_cast<double>(probe[i]);
        }
        best = std::max(best, std::abs(dot) / std::sqrt(windowEnergy * probeEnergy));
    }
    return std::min(best, 1.0);
}

void VirtualMicCheck::start(const QString& recordingName) {
    cancel();
    deviceName_ = recordingName;
    auto devices = backend_.enumerate(devices::DeviceKind::Capture);
    if (!devices) {
        setState(State::Failed, QString::fromStdString(devices.error().message));
        return;
    }
    const auto& list = devices.value();
    const auto it = std::ranges::find_if(list, [&](const devices::DeviceInfo& d) {
        return QString::fromStdString(d.name) == recordingName;
    });
    if (it == list.end()) {
        setState(State::Failed,
                 tr("\"%1\" is not in the list of recording devices. Restart the computer "
                    "after installing the virtual cable, then try again.")
                     .arg(recordingName));
        return;
    }
    devices::StreamConfig config;
    config.deviceId = it->id;
    config.sampleRate = static_cast<std::uint32_t>(kRate);
    config.channels = 1;
    auto stream = backend_.openCapture(config, *this);
    if (!stream) {
        setState(State::Failed, QString::fromStdString(stream.error().message));
        return;
    }
    stream_ = std::move(stream).value();
    if (auto started = stream_->start(); !started) {
        stream_.reset();
        setState(State::Failed, QString::fromStdString(started.error().message));
        return;
    }
    if (auto played = engine_.playSpeech(probe(), false); !played) {
        cancel();
        setState(State::Failed, QString::fromStdString(played.error().message));
        return;
    }
    recording_.clear();
    setState(State::Running, tr("Playing a short test sound into the virtual microphone..."));
    poll_.start();
    listen_.start();
}

void VirtualMicCheck::cancel() {
    poll_.stop();
    listen_.stop();
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    std::vector<float> discard(captured_.availableToRead());
    static_cast<void>(captured_.read(discard));
    if (state_ == State::Running) {
        setState(State::Idle, {});
    }
}

void VirtualMicCheck::onCapture(const float* interleaved, std::size_t frames,
                                std::uint32_t channels) noexcept {
    // Mono was requested; take the first channel if the backend gave more.
    for (std::size_t f = 0; f < frames; ++f) {
        const float s = interleaved[f * channels];
        static_cast<void>(captured_.write(std::span<const float>(&s, 1)));
    }
}

void VirtualMicCheck::drain() {
    std::vector<float> chunk(captured_.availableToRead());
    const std::size_t n = captured_.read(chunk);
    recording_.insert(recording_.end(), chunk.begin(),
                      chunk.begin() + static_cast<std::ptrdiff_t>(n));
    if (recording_.size() > kMaxRecording) {
        finish();
    }
}

void VirtualMicCheck::finish() {
    drain();
    poll_.stop();
    listen_.stop();
    if (stream_) {
        stream_->stop();
        stream_.reset();
    }
    const std::vector<float> chirp = probe();
    const float peak = recording_.empty()
                           ? 0.0F
                           : std::ranges::max(recording_, {}, [](float v) { return std::abs(v); });
    const double match = matchStrength(recording_, chirp);
    if (std::abs(peak) < kSilence) {
        setState(State::Silent,
                 tr("Nothing arrived on \"%1\". Check that Voxwright plays into the virtual "
                    "cable on the Audio page, and restart the computer if you just installed "
                    "it.")
                     .arg(deviceName_));
    } else if (match < kMatchThreshold) {
        setState(State::Mismatch,
                 tr("Sound arrived on \"%1\", but not the test sound. Another app may be "
                    "playing into the virtual cable; close it and try again.")
                     .arg(deviceName_));
    } else {
        setState(State::Passed, tr("The test sound arrived on \"%1\". Choose it as the "
                                   "microphone in Discord, Zoom, or your game.")
                                    .arg(deviceName_));
    }
}

void VirtualMicCheck::setState(State state, const QString& message) {
    state_ = state;
    message_ = message;
    emit stateChanged();
}

} // namespace vox::app
