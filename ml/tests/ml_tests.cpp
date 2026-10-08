#include <vox/ml/neural_model.hpp>
#include <vox/ml/neural_voice.hpp>
#include <vox/plugins/voice_chain.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace vox::ml;
using Catch::Approx;

namespace {

std::string model(const char* name) {
    return std::string(VOX_ML_TEST_MODELS) + "/" + name;
}

std::shared_ptr<ModelHost> hostWith(const char* name) {
    auto host = std::make_shared<ModelHost>();
    if (name != nullptr) {
        auto loaded = NeuralModel::load(model(name));
        REQUIRE(loaded);
        host->set(std::move(loaded).value());
    }
    return host;
}

vox::plugins::PrepareContext context() {
    vox::plugins::PrepareContext c;
    c.sampleRate = 48000.0;
    c.maxBlockSize = 128;
    return c;
}

/// Runs `input` through the node in 128-sample blocks, pumping the worker
/// side after each block as a punctual worker thread would.
std::vector<float> stream(NeuralVoiceNode& node, const std::vector<float>& input,
                          bool pumpEachBlock = true) {
    std::vector<float> out(input);
    for (std::size_t pos = 0; pos + 128 <= out.size(); pos += 128) {
        node.process(std::span<float>(out).subspan(pos, 128));
        if (pumpEachBlock) {
            node.pump();
        }
    }
    return out;
}

/// Largest difference between out[n] and gain * in[n - delay] from `from` on.
float worstError(const std::vector<float>& out, const std::vector<float>& in, std::size_t delay,
                 std::size_t from, float gain = 1.0F) {
    float worst = 0.0F;
    for (std::size_t n = std::max(from, delay); n < out.size() - 128; ++n) {
        worst = std::max(worst, std::abs(out[n] - gain * in[n - delay]));
    }
    return worst;
}

std::vector<float> testSignal() {
    auto x = vox::testing::sine(220.0, 2.0, 48000.0, 0.3F);
    const auto noise = vox::testing::whiteNoise(2.0, 48000.0, 3);
    for (std::size_t i = 0; i < x.size(); ++i) {
        x[i] += 0.05F * noise[i];
    }
    return x;
}

} // namespace

TEST_CASE("Voice models load with their metadata, and unusable ones say why", "[ml]") {
    auto identity = NeuralModel::load(model("identity_48k.onnx"));
    REQUIRE(identity);
    CHECK(identity.value()->info().name == "Identity");
    CHECK(identity.value()->info().inputRate == 48000.0);
    CHECK_FALSE(identity.value()->info().takesPitch);
    auto half = NeuralModel::load(model("half_16k.onnx"));
    REQUIRE(half);
    CHECK(half.value()->info().outputRate == 16000.0);
    auto pitch = NeuralModel::load(model("pitch_gain_48k.onnx"));
    REQUIRE(pitch);
    CHECK(pitch.value()->info().takesPitch);

    const auto wrong = NeuralModel::load(model("wrong_names.onnx"));
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().code == vox::ErrorCode::ModelIncompatible);
    CHECK(wrong.error().message.find("\"x\"") != std::string::npos);

    const auto missing = NeuralModel::load(model("missing.onnx"));
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == vox::ErrorCode::FileNotFound);

    const auto garbagePath = std::filesystem::temp_directory_path() / "vox_not_a_model.onnx";
    {
        std::ofstream garbage(garbagePath);
        garbage << "this is not a model";
    }
    const auto garbage = NeuralModel::load(garbagePath.string());
    std::filesystem::remove(garbagePath);
    REQUIRE_FALSE(garbage);
    CHECK(garbage.error().code == vox::ErrorCode::ModelLoadFailed);
    CHECK(garbage.error().message.find("vox_not_a_model.onnx") != std::string::npos);
}

TEST_CASE("A model run returns the converted audio", "[ml]") {
    auto half = NeuralModel::load(model("half_16k.onnx"));
    REQUIRE(half);
    const std::vector<float> in{1.0F, 2.0F, -4.0F};
    const auto out = half.value()->run(in, 0.0F);
    REQUIRE(out);
    CHECK(out.value() == std::vector<float>{0.5F, 1.0F, -2.0F});

    auto pitch = NeuralModel::load(model("pitch_gain_48k.onnx"));
    REQUIRE(pitch);
    const auto doubled = pitch.value()->run(in, 12.0F);
    REQUIRE(doubled);
    CHECK(doubled.value()[1] == Approx(4.0F));
}

TEST_CASE("Streaming through an identity model is sample exact at a fixed latency", "[ml]") {
    NeuralVoiceNode node(hostWith("identity_48k.onnx"), {}, false);
    node.prepare(context());
    // hop 100 + crossfade 10 + lookahead 10 + budget 60 ms.
    CHECK(node.latencySamples() == 8640);
    const auto in = testSignal();
    const auto out = stream(node, in);
    CHECK(worstError(out, in, node.latencySamples(), 0) < 1e-5F);
    CHECK(node.lateSamples() == 0);
}

TEST_CASE("Resampling models keep level and timing", "[ml]") {
    NeuralVoiceNode node(hostWith("half_16k.onnx"), {}, false);
    node.prepare(context());
    const auto in = vox::testing::sine(1000.0, 2.0, 48000.0, 0.5F);
    const auto out = stream(node, in);
    // After the start-up fade: half level, same phase, at the stated latency.
    double error = 0.0;
    double signal = 0.0;
    for (std::size_t n = 24000; n < out.size() - 128; ++n) {
        const double expected = 0.5 * static_cast<double>(in[n - node.latencySamples()]);
        const double difference = static_cast<double>(out[n]) - expected;
        error += difference * difference;
        signal += expected * expected;
    }
    const double snrDb = 10.0 * std::log10(signal / error);
    INFO("SNR " << snrDb << " dB");
    CHECK(snrDb > 40.0);
}

TEST_CASE("The pitch setting reaches the model", "[ml]") {
    NeuralVoiceNode node(hostWith("pitch_gain_48k.onnx"), {}, false);
    node.prepare(context());
    node.setParameter(NeuralVoiceNode::kPitch, 12.0F); // this model doubles the level
    const auto in = testSignal();
    const auto out = stream(node, in);
    CHECK(worstError(out, in, node.latencySamples(), 48000, 2.0F) < 1e-4F);
}

TEST_CASE("Without a model, or when the model is late, the voice passes through", "[ml][errors]") {
    const auto in = testSignal();
    SECTION("No model loaded") {
        NeuralVoiceNode node(hostWith(nullptr), {}, false);
        node.prepare(context());
        const auto out = stream(node, in);
        CHECK(worstError(out, in, node.latencySamples(), 0) < 1e-5F);
        CHECK(node.lateSamples() == 0);
    }
    SECTION("The model never answers") {
        NeuralVoiceNode node(hostWith("half_16k.onnx"), {}, false);
        node.prepare(context());
        const auto out = stream(node, in, false);
        // Plain voice at the same latency, never half level.
        CHECK(worstError(out, in, node.latencySamples(), 0) < 1e-5F);
        CHECK(node.lateSamples() > 0);
    }
}

TEST_CASE("The audio side of the neural block does not allocate", "[ml][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this build.");
    }
    NeuralVoiceNode node(hostWith("identity_48k.onnx"), {}, false);
    node.prepare(context());
    std::vector<float> block(128, 0.1F);
    std::size_t allocations = 0;
    for (int i = 0; i < 200; ++i) {
        {
            const vox::testing::AllocationTrap trap;
            node.process(block);
            allocations += trap.allocations() + trap.deallocations();
        }
        node.pump(); // the worker side may allocate
    }
    CHECK(allocations == 0);
}

TEST_CASE("Neural blocks go into voices and run on their own thread", "[ml]") {
    auto registry = vox::plugins::EffectRegistry::builtin();
    registerNeuralEffect(registry, hostWith("identity_48k.onnx"));
    REQUIRE(registry.find("neural") != nullptr);
    vox::plugins::VoicePreset voice;
    voice.id = "custom-neural";
    voice.name = "Neural";
    voice.category = "Character";
    voice.blocks.push_back({"neural", {}, false});
    auto chain = vox::plugins::VoiceChain::build(voice, {}, registry, context());
    REQUIRE(chain);
    CHECK(chain.value()->latencySamples() >= 8640);

    // Feed blocks at about the real rate; the worker thread keeps up.
    const auto in = testSignal();
    std::vector<float> out(in);
    for (std::size_t pos = 0; pos + 128 <= out.size(); pos += 128) {
        chain.value()->process(std::span<float>(out).subspan(pos, 128));
        std::this_thread::sleep_for(std::chrono::microseconds(2000));
    }
    CHECK(vox::testing::allFinite(out));
    CHECK(vox::testing::peakAbs(std::span<const float>(out).last(24000)) > 0.2F);
}
