#include "vox/ml/neural_model.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>

namespace vox::ml {
namespace {

constexpr const char* kAudioIn = "audio";
constexpr const char* kPitchIn = "pitch_shift";
constexpr const char* kAudioOut = "audio_out";

Ort::Env& environment() {
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "voxwright");
    return env;
}

Error modelError(ErrorCode code, const std::string& path, const std::string& what,
                 const std::string& detail = {}) {
    return makeError(
        code, "The voice model \"" + std::filesystem::path(path).filename().string() + "\" " + what,
        detail);
}

std::optional<double> rateFrom(const Ort::ModelMetadata& meta, const char* key,
                               Ort::AllocatorWithDefaultOptions& allocator) {
    const Ort::AllocatedStringPtr value = meta.LookupCustomMetadataMapAllocated(key, allocator);
    if (!value) {
        return std::nullopt;
    }
    const std::string text(value.get());
    double rate = 0.0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), rate);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return -1.0;
    }
    return rate;
}

bool isFloatMatrix(const Ort::TypeInfo& type) {
    if (type.GetONNXType() != ONNX_TYPE_TENSOR) {
        return false;
    }
    const auto tensor = type.GetTensorTypeAndShapeInfo();
    return tensor.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
           tensor.GetDimensionsCount() == 2;
}

} // namespace

struct NeuralModel::Impl {
    std::string path;
    ModelInfo info;
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
};

NeuralModel::NeuralModel(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

NeuralModel::~NeuralModel() = default;

const ModelInfo& NeuralModel::info() const noexcept {
    return impl_->info;
}

const std::string& NeuralModel::path() const noexcept {
    return impl_->path;
}

std::string NeuralModel::runtimeVersion() {
    return Ort::GetVersionString();
}

Result<std::unique_ptr<NeuralModel>> NeuralModel::load(const std::string& path, int threads) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return modelError(ErrorCode::FileNotFound, path,
                          "was not found. Choose the .onnx file again.");
    }
    auto impl = std::make_unique<Impl>();
    impl->path = path;
    try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::max(1, threads));
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // The model runs on its own worker thread; spinning would steal the
        // core the audio thread needs.
        options.AddConfigEntry("session.intra_op.allow_spinning", "0");
#ifdef _WIN32
        const std::wstring widePath = std::filesystem::path(path).wstring();
        impl->session = Ort::Session(environment(), widePath.c_str(), options);
#else
        impl->session = Ort::Session(environment(), path.c_str(), options);
#endif
    } catch (const Ort::Exception& e) {
        return modelError(ErrorCode::ModelLoadFailed, path,
                          "could not be opened. It may not be an ONNX model, or it needs a "
                          "newer ONNX Runtime than Voxwright includes (" +
                              runtimeVersion() + ").",
                          e.what());
    }

    Ort::AllocatorWithDefaultOptions allocator;
    bool hasAudio = false;
    for (std::size_t i = 0; i < impl->session.GetInputCount(); ++i) {
        const std::string name = impl->session.GetInputNameAllocated(i, allocator).get();
        if (name == kAudioIn) {
            hasAudio = isFloatMatrix(impl->session.GetInputTypeInfo(i));
        } else if (name == kPitchIn) {
            impl->info.takesPitch = true;
        } else {
            return modelError(ErrorCode::ModelIncompatible, path,
                              "has an input called \"" + name +
                                  "\". Voxwright models take \"audio\" (and optionally "
                                  "\"pitch_shift\"); see docs/ml.md.");
        }
    }
    if (!hasAudio) {
        return modelError(ErrorCode::ModelIncompatible, path,
                          "has no float input called \"audio\" shaped [1, samples]; see "
                          "docs/ml.md for the format Voxwright expects.");
    }
    bool hasOutput = false;
    for (std::size_t i = 0; i < impl->session.GetOutputCount(); ++i) {
        const std::string name = impl->session.GetOutputNameAllocated(i, allocator).get();
        if (name == kAudioOut && isFloatMatrix(impl->session.GetOutputTypeInfo(i))) {
            hasOutput = true;
        }
    }
    if (!hasOutput) {
        return modelError(ErrorCode::ModelIncompatible, path,
                          "has no float output called \"audio_out\" shaped [1, samples].");
    }

    const Ort::ModelMetadata meta = impl->session.GetModelMetadata();
    if (const Ort::AllocatedStringPtr name =
            meta.LookupCustomMetadataMapAllocated("vox.name", allocator)) {
        impl->info.name = name.get();
    }
    if (impl->info.name.empty()) {
        impl->info.name = std::filesystem::path(path).stem().string();
    }
    for (const auto& [key, rate] : {std::pair{"vox.input_rate", &impl->info.inputRate},
                                    std::pair{"vox.output_rate", &impl->info.outputRate}}) {
        const auto value = rateFrom(meta, key, allocator);
        if (!value) {
            continue; // keeps the 16 kHz default
        }
        if (*value < 8000.0 || *value > 96000.0) {
            return modelError(ErrorCode::ModelIncompatible, path,
                              std::string("gives ") + key +
                                  " outside 8000 to 96000 Hz; it must be a sample rate.");
        }
        *rate = *value;
    }
    return std::unique_ptr<NeuralModel>(new NeuralModel(std::move(impl)));
}

Result<std::vector<float>> NeuralModel::run(std::span<const float> input, float pitchSemitones) {
    try {
        std::vector<float> samples(input.begin(), input.end());
        const std::array<std::int64_t, 2> audioShape{1, static_cast<std::int64_t>(samples.size())};
        std::array<float, 1> pitch{pitchSemitones};
        const std::array<std::int64_t, 1> pitchShape{1};

        std::vector<Ort::Value> inputs;
        std::vector<const char*> names;
        inputs.push_back(Ort::Value::CreateTensor<float>(
            impl_->memory, samples.data(), samples.size(), audioShape.data(), audioShape.size()));
        names.push_back(kAudioIn);
        if (impl_->info.takesPitch) {
            inputs.push_back(Ort::Value::CreateTensor<float>(impl_->memory, pitch.data(), 1,
                                                             pitchShape.data(), pitchShape.size()));
            names.push_back(kPitchIn);
        }
        const char* outputName = kAudioOut;
        auto outputs = impl_->session.Run(Ort::RunOptions{nullptr}, names.data(), inputs.data(),
                                          inputs.size(), &outputName, 1);
        const auto shape = outputs.front().GetTensorTypeAndShapeInfo();
        const auto count = shape.GetElementCount();
        const auto* data = outputs.front().GetTensorData<float>();
        return std::vector<float>(data, data + count);
    } catch (const Ort::Exception& e) {
        return modelError(ErrorCode::ModelRunFailed, impl_->path,
                          "failed while converting your voice. Choose another model or turn "
                          "the neural voice off.",
                          e.what());
    }
}

} // namespace vox::ml
