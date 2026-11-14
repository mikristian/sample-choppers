#pragma once

#include <algorithm>
#include <cmath>

namespace vsl {

// Original, allocation-free stereo insert effect. Own one instance per insert slot.
// prepare() and reset() run outside the audio callback; process() runs on audio thread.
// Process successive blocks with the same instance to preserve filter/crusher state.
class MpcFxEngine {
public:
    enum Type : int { bypass = 0, lowpass = 1, highpass = 2, drive = 3, bitcrush = 4 };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = std::isfinite(sampleRate) && sampleRate >= 8000.0 && sampleRate <= 384000.0
            ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept {
        for (auto& channel : channels_) channel = {};
        lastType_ = bypass;
    }

    // amount and wet: normalized 0..1. Invalid types bypass; invalid controls
    // default to zero. Input/output are interleaved in time, planar in channels.
    // Non-finite input is treated as silence to prevent contaminated DSP state.
    void process(float* left, float* right, int frames, int type,
                 float amount, float wet) noexcept {
        if (!left || !right || frames <= 0) return;
        const int selected = type >= lowpass && type <= bitcrush ? type : bypass;
        const float a = finite01(amount);
        const float w = finite01(wet);
        if (selected != lastType_) {
            for (auto& channel : channels_) channel = {};
            lastType_ = selected;
        }
        if (selected == bypass || w <= 0.0f) {
            for (int i = 0; i < frames; ++i) {
                left[i] = sanitize(left[i]);
                if (right != left) right[i] = sanitize(right[i]);
            }
            return;
        }

        // Exponential perceptual sweep from near-Nyquist down to bass cutoff.
        const double cutoff = selected == highpass
            ? 25.0 * std::pow(650.0, static_cast<double>(a))
            : 18000.0 * std::pow(1.0 / 600.0, static_cast<double>(a));
        const float coefficient = static_cast<float>(1.0 - std::exp(
            -6.283185307179586 * std::min(cutoff, sampleRate_ * 0.45) / sampleRate_));
        const int holdFrames = 1 + static_cast<int>(std::floor(a * a * 63.0f));
        const int levels = 1 << (16 - static_cast<int>(std::floor(a * 12.0f)));
        const float gain = 1.0f + a * 19.0f;
        const float normalize = 1.0f / std::tanh(gain);

        for (int i = 0; i < frames; ++i) {
            // Supports mono processing by passing the same buffer for L/R.
            if (left == right) {
                left[i] = sample(left[i], channels_[0], selected, w,
                                 coefficient, gain, normalize, holdFrames, levels);
            } else {
                left[i] = sample(left[i], channels_[0], selected, w,
                                 coefficient, gain, normalize, holdFrames, levels);
                right[i] = sample(right[i], channels_[1], selected, w,
                                  coefficient, gain, normalize, holdFrames, levels);
            }
        }
    }

private:
    struct Channel { float lp = 0.0f, held = 0.0f; int remaining = 0; };

    static float finite01(float value) noexcept {
        return std::isfinite(value) ? std::max(0.0f, std::min(1.0f, value)) : 0.0f;
    }

    static float sanitize(float value) noexcept {
        return std::isfinite(value) ? std::max(-4.0f, std::min(4.0f, value)) : 0.0f;
    }

    static float sample(float input, Channel& state, int type,
                        float wet, float coefficient, float gain, float normalize,
                        int holdFrames, int levels) noexcept {
        const float dry = sanitize(input);
        float effected = dry;
        if (type == lowpass || type == highpass) {
            state.lp += coefficient * (dry - state.lp);
            if (std::fabs(state.lp) < 1.0e-20f) state.lp = 0.0f;
            effected = type == lowpass ? state.lp : dry - state.lp;
        } else if (type == drive) {
            effected = std::tanh(dry * gain) * normalize;
        } else if (type == bitcrush) {
            if (state.remaining <= 0) {
                const float quantized = std::round(dry * static_cast<float>(levels))
                    / static_cast<float>(levels);
                state.held = quantized;
                state.remaining = holdFrames;
            }
            --state.remaining;
            effected = state.held;
        }
        const float output = dry + wet * (effected - dry);
        return std::isfinite(output) ? std::max(-4.0f, std::min(4.0f, output)) : 0.0f;
    }

    double sampleRate_ = 44100.0;
    Channel channels_[2]{};
    int lastType_ = bypass;
};

} // namespace vsl
