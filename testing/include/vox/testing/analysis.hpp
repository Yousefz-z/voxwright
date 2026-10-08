#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace vox::testing {

[[nodiscard]] double rms(std::span<const float> x);
[[nodiscard]] float peakAbs(std::span<const float> x);
[[nodiscard]] double toDb(double linear);
[[nodiscard]] bool allFinite(std::span<const float> x);
[[nodiscard]] double centsBetween(double hz, double referenceHz);

struct F0Estimate {
    double hz = 0.0;         ///< 0 when no periodicity was found.
    double aperiodicity = 1; ///< YIN cumulative-mean-normalized minimum; lower = more periodic.
};

/// Offline high-accuracy f0 estimate over the whole span (YIN with parabolic
/// interpolation). Intended as a measurement reference, not for real time.
[[nodiscard]] F0Estimate estimateF0(std::span<const float> x, double sampleRate, double minHz,
                                    double maxHz);

/// Frame-wise f0 track; unvoiced frames are 0.
[[nodiscard]] std::vector<double> f0Track(std::span<const float> x, double sampleRate, double minHz,
                                          double maxHz, double frameSeconds = 0.04,
                                          double hopSeconds = 0.01);

/// Median of the non-zero entries (0 if none).
[[nodiscard]] double medianVoiced(const std::vector<double>& track);

/// Welch-averaged power spectrum in dB, fftSize/2 + 1 bins.
[[nodiscard]] std::vector<double> powerSpectrumDb(std::span<const float> x, std::size_t fftSize);

[[nodiscard]] double spectralCentroidHz(std::span<const float> x, double sampleRate);

/// Energy fraction (0..1) of the signal above `cutoffHz`.
[[nodiscard]] double energyAbove(std::span<const float> x, double sampleRate, double cutoffHz);

/// All-pole (LPC) spectral envelope in dB on `bins` points from 0 to Nyquist,
/// from autocorrelation averaged over 30 ms frames.
[[nodiscard]] std::vector<double> lpcEnvelopeDb(std::span<const float> x, double sampleRate,
                                                int order, std::size_t bins = 512);

/// Frequencies of local maxima of an envelope, lowest first.
[[nodiscard]] std::vector<double> envelopePeaksHz(const std::vector<double>& envelopeDb,
                                                  double sampleRate, std::size_t maxCount);

/// RMS difference in dB between two envelopes over [lowHz, highHz] after
/// removing each envelope's mean level in that band.
[[nodiscard]] double envelopeDistanceDb(const std::vector<double>& a, const std::vector<double>& b,
                                        double sampleRate, double lowHz, double highHz);

/// Lag (in samples, >= 0) that best aligns `delayed` to `reference`.
[[nodiscard]] std::size_t estimateLag(std::span<const float> reference,
                                      std::span<const float> delayed, std::size_t maxLag);

} // namespace vox::testing
