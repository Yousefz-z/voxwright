// Measures a voice model on this CPU: time per streaming window for 1, 2,
// and 4 threads, and how much audio arrives late when the neural block runs
// in real time.
//
//   vox_bench_ml <model.onnx> [seconds of real-time playback, default 10]

#include <vox/ml/neural_model.hpp>
#include <vox/ml/neural_voice.hpp>
#include <vox/testing/signals.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool windowCost(const std::string& path, const vox::ml::StreamingConfig& config) {
    std::printf("| Threads | Mean per window (ms) | 95th percentile (ms) | Real-time factor |\n"
                "|---|---|---|---|\n");
    for (const int threads : {1, 2, 4}) {
        auto model = vox::ml::NeuralModel::load(path, threads);
        if (!model) {
            static_cast<void>(std::fprintf(stderr, "%s\n", model.error().message.c_str()));
            return false;
        }
        const double rate = model.value()->info().inputRate;
        const auto window = vox::testing::synthPhrase(120.0, rate).audio;
        const auto length =
            static_cast<std::size_t>((config.contextMs + config.hopMs) * rate / 1000.0);
        const std::vector<float> input(window.begin(),
                                       window.begin() + static_cast<std::ptrdiff_t>(length));
        std::vector<double> times;
        for (int i = 0; i < 33; ++i) {
            const auto t0 = Clock::now();
            const auto out = model.value()->run(input, 0.0F);
            const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
            if (!out) {
                static_cast<void>(std::fprintf(stderr, "%s\n", out.error().message.c_str()));
                return false;
            }
            if (i >= 3) {
                times.push_back(ms); // the first runs warm caches up
            }
        }
        std::sort(times.begin(), times.end());
        double sum = 0.0;
        for (const double t : times) {
            sum += t;
        }
        const double mean = sum / static_cast<double>(times.size());
        const double p95 = times[times.size() * 95 / 100];
        std::printf("| %d | %.1f | %.1f | %.2f |\n", threads, mean, p95, mean / config.hopMs);
    }
    return true;
}

bool realTime(const std::string& path, const vox::ml::StreamingConfig& config, double seconds) {
    auto model = vox::ml::NeuralModel::load(path, 1);
    if (!model) {
        static_cast<void>(std::fprintf(stderr, "%s\n", model.error().message.c_str()));
        return false;
    }
    auto host = std::make_shared<vox::ml::ModelHost>();
    host->set(std::move(model).value());
    vox::ml::NeuralVoiceNode node(host, config);
    vox::plugins::PrepareContext context;
    context.maxBlockSize = 128;
    node.prepare(context);
    const auto phrase = vox::testing::synthPhrase(120.0).audio;
    std::vector<float> block(128);
    const auto blocks = static_cast<std::size_t>(seconds * 48000.0 / 128.0);
    const auto period = std::chrono::duration<double>(128.0 / 48000.0);
    auto next = Clock::now();
    for (std::size_t b = 0; b < blocks; ++b) {
        for (std::size_t i = 0; i < block.size(); ++i) {
            block[i] = phrase[(b * 128 + i) % phrase.size()];
        }
        node.process(block);
        next += std::chrono::duration_cast<Clock::duration>(period);
        std::this_thread::sleep_until(next);
    }
    const double late =
        100.0 * static_cast<double>(node.lateSamples()) / static_cast<double>(blocks * 128);
    std::printf("\nReal time, %.0f s, one model thread: latency %.1f ms, late samples %.2f %%\n",
                seconds, static_cast<double>(node.latencySamples()) / 48.0, late);
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        static_cast<void>(std::fprintf(stderr, "usage: vox_bench_ml <model.onnx> [seconds]\n"));
        return 2;
    }
    const std::string path = argv[1];
    char* end = nullptr;
    const double seconds = argc > 2 ? std::strtod(argv[2], &end) : 10.0;
    if (argc > 2 && (end == argv[2] || seconds <= 0.0)) {
        static_cast<void>(std::fprintf(stderr, "error: %s is not a number of seconds\n", argv[2]));
        return 2;
    }
    const vox::ml::StreamingConfig config;
    std::printf("ONNX Runtime %s, window %.0f ms (%.0f ms new audio)\n\n",
                vox::ml::NeuralModel::runtimeVersion().c_str(), config.contextMs + config.hopMs,
                config.hopMs);
    if (!windowCost(path, config) || !realTime(path, config, seconds)) {
        return 1;
    }
    return 0;
}
