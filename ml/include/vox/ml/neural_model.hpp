#pragma once

#include <vox/core/result.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vox::ml {

/// What a voice model says about itself (ONNX metadata, see docs/ml.md).
struct ModelInfo {
    std::string name;
    double inputRate = 16000.0;  ///< "vox.input_rate"
    double outputRate = 16000.0; ///< "vox.output_rate"
    bool takesPitch = false;     ///< Has the optional "pitch_shift" input.
};

/// A neural voice conversion model run with ONNX Runtime on the CPU.
/// Contract: input "audio" float32 [1, N] at inputRate, optional input
/// "pitch_shift" float32 [1] in semitones, output "audio_out" float32
/// [1, M] at outputRate covering the same stretch of time.
class NeuralModel {
public:
    /// Loads and checks a model. `threads` is how many CPU threads one run
    /// may use. Each failure (missing file, not ONNX, wrong inputs or
    /// outputs, bad metadata) has its own message.
    [[nodiscard]] static Result<std::unique_ptr<NeuralModel>> load(const std::string& path,
                                                                   int threads = 1);

    NeuralModel(const NeuralModel&) = delete;
    NeuralModel& operator=(const NeuralModel&) = delete;
    NeuralModel(NeuralModel&&) = delete;
    NeuralModel& operator=(NeuralModel&&) = delete;
    ~NeuralModel();

    [[nodiscard]] const ModelInfo& info() const noexcept;
    [[nodiscard]] const std::string& path() const noexcept;

    /// Converts `input` (at inputRate). Not real-time safe: runs on a worker
    /// thread and allocates. One run at a time.
    [[nodiscard]] Result<std::vector<float>> run(std::span<const float> input,
                                                 float pitchSemitones);

    /// The ONNX Runtime version linked in, for diagnostics.
    [[nodiscard]] static std::string runtimeVersion();

private:
    struct Impl;
    explicit NeuralModel(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace vox::ml
