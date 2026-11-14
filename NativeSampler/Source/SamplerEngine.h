#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vsl {

struct Sample {
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::array<float, 256> waveformPeaks{}; // computed once during decoding, immutable thereafter

    bool valid() const noexcept {
        return !left.empty() && (right.empty() || right.size() == left.size())
            && std::isfinite(sampleRate) && sampleRate > 0.0;
    }
};

struct Bank {
    std::array<std::shared_ptr<const Sample>, 16> pads{};
};

// Captured per voice at Note On, so parameter changes never mutate the
// playback bounds of an already-running real-time voice.
struct PlaybackOptions {
    float sampleStartPercent = 0.0f;
    float sampleEndPercent = 100.0f;
    float maxLengthSeconds = 16.0f;
    bool loop = false;
    bool gated = false;
    // One voice group: a fresh pad press ends the previous sound. A tiny
    // de-click ramp is carried into the new voice, without layering phrases.
    // Off by default at the reusable Engine level for legacy polyphonic use.
    bool chokePrevious = false;
    float releaseSeconds = 0.120f;
    bool tempoSync = false;
    float sourceBpm = 120.0f;
    // Playback origin within the trimmed interval: 0 starts at the trim's
    // beginning, 50 at its midpoint. Values are clamped to [0, 99].
    float warpAnchorPercent = 0.0f;
};

// Call noteOn(), render(), and allNotesOff() on the same audio thread.
// setBank() and setOutputRate() may be called from another thread.
// The processor must retain published Banks AND their Samples until no voice
// can reference them; no ownership or reference-count releases occur in audio.
class Engine {
public:
    void setBank(const Bank* bank) noexcept {
        bank_.store(bank, std::memory_order_release);
    }

    void setOutputRate(double rate) noexcept {
        if (std::isfinite(rate) && rate > 0.0)
            outputRate_.store(rate, std::memory_order_relaxed);
    }

    void setTempoBpm(float bpm) noexcept {
        if (std::isfinite(bpm) && bpm > 0.0f)
            tempoBpm_.store(std::max(20.0f, std::min(300.0f, bpm)),
                            std::memory_order_relaxed);
    }

    void noteOn(int midiNote, float velocity, float pitchSemitones = 0,
                bool keyboardMode = false, int keyboardPad = 0,
                int rootNote = 60,
                const PlaybackOptions& options = {}) noexcept {
        if (midiNote < 0 || midiNote > 127 || !std::isfinite(velocity)
            || velocity <= 0.0f) return;
        const int pad = keyboardMode ? keyboardPad : midiNote - 36;
        if (pad < 0 || pad >= 16) return;
        const Bank* bank = bank_.load(std::memory_order_acquire);
        if (!bank) return;
        const Sample* sample = bank->pads[static_cast<std::size_t>(pad)].get();
        if (!sample || !sample->valid()) return;

        const double rate = outputRate_.load(std::memory_order_relaxed);
        if (!(rate > 0.0) || !std::isfinite(rate)) return;
        const double semitones = static_cast<double>(pitchSemitones)
            + (keyboardMode ? static_cast<double>(midiNote - rootNote) : 0.0);
        if (!std::isfinite(semitones)) return;
        const double shift = std::max(-96.0, std::min(96.0, semitones));
        const double step = sample->sampleRate / rate * std::exp2(shift / 12.0);
        if (!(step > 0.0) || !std::isfinite(step)) return;
        const double total = static_cast<double>(sample->left.size());
        const double startRatio = clampPercent(options.sampleStartPercent, 0.0, 99.0) * 0.01;
        const double endRatio = clampPercent(options.sampleEndPercent, 1.0, 100.0) * 0.01;
        const double start = std::floor(total * startRatio);
        const double end = std::max(start + 1.0, std::ceil(total * endRatio));
        if (end - start < 1.0) return;
        const double length = std::isfinite(options.maxLengthSeconds)
            ? std::max(0.1, std::min(32.0, static_cast<double>(options.maxLengthSeconds))) : 16.0;
        const double safeRelease = std::isfinite(options.releaseSeconds)
            ? std::max(0.005, std::min(5.0, static_cast<double>(options.releaseSeconds))) : 0.12;

        float previousLeft = 0.0f, previousRight = 0.0f;
        if (options.chokePrevious) {
            // Exclusive pad behavior for chopping: preserve only the last
            // rendered sample of the old sound for a 32-frame click-safe
            // crossfade. No old voice may continue underneath the new chop.
            for (auto& voice : voices_) {
                if (voice.active) {
                    previousLeft += voice.lastL;
                    previousRight += voice.lastR;
                } else if (voice.tailFrames > 0) {
                    const float remaining = static_cast<float>(voice.tailFrames)
                                          / static_cast<float>(kFadeFrames);
                    previousLeft += voice.tailL * remaining;
                    previousRight += voice.tailR * remaining;
                }
                voice.active = false;
                voice.tailFrames = 0;
                voice.releaseFrames = 0;
                voice.level = 0.0f;
            }
            for (auto& progress : padProgress_)
                progress.store(-1.0f, std::memory_order_relaxed);
        }

        Voice* target = nullptr;
        for (auto& voice : voices_) {
            if (!voice.active && voice.tailFrames == 0) { target = &voice; break; }
        }
        if (!target) {
            // Prefer the quietest/oldest voice so stealing is less audible.
            target = &voices_[0];
            for (auto& voice : voices_) {
                if (!voice.active) { target = &voice; break; }
                if (voice.level < target->level
                    || (voice.level == target->level && voice.age < target->age))
                    target = &voice;
            }
        }
        if (target->active) {
            target->tailL = target->lastL;
            target->tailR = target->lastR;
            target->tailFrames = kFadeFrames;
        }
        if (options.chokePrevious) {
            target->tailL = std::max(-1.0f, std::min(1.0f, previousLeft));
            target->tailR = std::max(-1.0f, std::min(1.0f, previousRight));
            target->tailFrames = std::abs(target->tailL) + std::abs(target->tailR) > 1.0e-6f
                ? kFadeFrames : 0;
        }
        target->sample = sample;
        target->position = start;
        target->start = start;
        target->end = std::min(end, total);
        target->step = step;
        target->gain = std::min(1.0f, velocity);
        target->level = target->gain;
        target->age = ++counter_;
        target->playedFrames = 0;
        target->releaseFrames = 0;
        target->releaseTotal = 0;
        target->maxFrames = static_cast<std::uint64_t>(std::ceil(length * rate));
        target->configuredRelease = std::max(32, static_cast<int>(std::ceil(safeRelease * rate)));
        target->midiNote = midiNote;
        target->padIndex = pad;
        target->loop = options.loop;
        target->gated = options.gated;
        target->tempoSync = options.tempoSync;
        target->sourceBpm = std::isfinite(options.sourceBpm) && options.sourceBpm > 0.0f
            ? std::max(20.0f, std::min(300.0f, options.sourceBpm)) : 120.0f;
        if (target->tempoSync) {
            // Anchor becomes the selected beat-marker playback origin. Using
            // a position within the trim makes moving the marker audible.
            const double anchor = clampPercent(options.warpAnchorPercent, 0.0, 99.0) * 0.01;
            target->position = start + (target->end - start) * anchor;
            target->syncOrigin = target->position;
            target->grainCurrent = target->position;
            target->grainPrevious = target->position;
            target->grainFrame = 0;
            target->grainNumber = 0;
        }
        target->active = true;
        target->lastL = target->lastR = 0.0f;
        if (options.chokePrevious)
            padProgress_[static_cast<std::size_t>(pad)].store(0.0f, std::memory_order_relaxed);
    }

    void noteOff(int midiNote, bool keyboardMode = false,
                 int keyboardPad = 0) noexcept {
        const int pad = keyboardMode ? keyboardPad : midiNote - 36;
        if (pad < 0 || pad >= 16) return;
        for (auto& voice : voices_) {
            if (voice.active && voice.gated && voice.padIndex == pad
                && voice.midiNote == midiNote)
                startRelease(voice, voice.configuredRelease);
        }
    }

    void releasePad(int pad) noexcept {
        if (pad < 0 || pad >= 16) return;
        for (auto& voice : voices_)
            if (voice.active && voice.gated && voice.padIndex == pad)
                startRelease(voice, voice.configuredRelease);
    }

    float getPadPlaybackProgress(int pad) const noexcept {
        if (pad < 0 || pad >= 16) return -1.0f;
        return padProgress_[static_cast<std::size_t>(pad)].load(std::memory_order_relaxed);
    }

    bool isPadPlaying(int pad) const noexcept {
        return getPadPlaybackProgress(pad) >= 0.0f;
    }

    void render(float* left, float* right, int count, float masterGain,
                float attackSeconds = 0.004f) noexcept {
        if (!left || !right || count <= 0) return;
        const double outputRate = outputRate_.load(std::memory_order_relaxed);
        const double safeAttack = std::isfinite(attackSeconds)
            ? std::max(0.0, static_cast<double>(attackSeconds)) : 0.004;
        const double attackFrames = std::min(10000000.0, safeAttack * outputRate);
        const float gain = std::isfinite(masterGain) ? masterGain : 0.0f;
        const double bpm = static_cast<double>(tempoBpm_.load(std::memory_order_relaxed));
        for (int i = 0; i < count; ++i) {
            float l = 0.0f, r = 0.0f;
            for (auto& voice : voices_) {
                if (voice.tailFrames > 0) {
                    const float envelope = static_cast<float>(voice.tailFrames)
                        / static_cast<float>(kFadeFrames);
                    l += voice.tailL * envelope;
                    r += voice.tailR * envelope;
                    --voice.tailFrames;
                }
                if (!voice.active) continue;
                const Sample* s = voice.sample;
                const std::size_t length = s->left.size();
                if (voice.playedFrames >= voice.maxFrames) {
                    voice.active = false;
                    continue;
                }
                if (voice.position >= voice.end) {
                    if (voice.loop && voice.end > voice.start + 1.0) {
                        voice.position = voice.start + std::fmod(voice.position - voice.start,
                                                                 voice.end - voice.start);
                    } else {
                        voice.active = false;
                        continue;
                    }
                }
                if (voice.tempoSync) {
                    renderSyncVoice(voice, l, r, attackFrames, bpm);
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(voice.position);
                const std::size_t next = index + 1 < static_cast<std::size_t>(voice.end)
                    ? index + 1 : index;
                const float fraction = static_cast<float>(voice.position - static_cast<double>(index));
                const float sl = s->left[index] + (s->left[next] - s->left[index]) * fraction;
                const auto& channelR = s->right.empty() ? s->left : s->right;
                const float sr = channelR[index] + (channelR[next] - channelR[index]) * fraction;
                const float attack = attackFrames < 1.0 ? 1.0f
                    : static_cast<float>(std::min(1.0, static_cast<double>(voice.playedFrames) / attackFrames));
                // Looping sustain uses a short crossfade between the section's
                // closing and opening windows to avoid hard loop clicks.
                const double remaining = (voice.end - voice.position) / voice.step;
                float end = 1.0f;
                if (voice.loop && voice.end - voice.start >= 256.0 * voice.step
                    && remaining < kFadeFrames) {
                    const double phase = std::max(0.0, remaining / kFadeFrames);
                    const double offset = (1.0 - phase) * kFadeFrames * voice.step;
                    const double position2 = voice.start + offset;
                    const auto idx2 = static_cast<std::size_t>(position2);
                    if (idx2 + 1 < length) {
                        const float f2 = static_cast<float>(position2 - static_cast<double>(idx2));
                        const float ls = s->left[idx2] + (s->left[idx2+1] - s->left[idx2]) * f2;
                        const float rs = channelR[idx2] + (channelR[idx2+1] - channelR[idx2]) * f2;
                        const float mix = static_cast<float>(1.0 - phase);
                        voice.loopL = ls * mix;
                        voice.loopR = rs * mix;
                        end = 1.0f - mix;
                    }
                } else {
                    voice.loopL = voice.loopR = 0.0f;
                    if (!voice.loop) end = static_cast<float>(std::min(1.0, remaining / kFadeFrames));
                }
                const auto maxRemaining = static_cast<double>(voice.maxFrames - voice.playedFrames);
                const float durationFade = static_cast<float>(std::min(1.0,
                                                               maxRemaining / kFadeFrames));
                const float release = voice.releaseFrames > 0
                    ? static_cast<float>(voice.releaseFrames)
                        / static_cast<float>(std::max(1, voice.releaseTotal)) : 1.0f;
                const float volume = voice.gain * attack * release * durationFade;
                // Crossfade *each* side individually before multiplying by
                // the envelope. Applying the closing-window fade to the
                // already-mixed signal accidentally muted every loop seam.
                voice.lastL = (sl * end + voice.loopL) * volume;
                voice.lastR = (sr * end + voice.loopR) * volume;
                l += voice.lastL;
                r += voice.lastR;
                voice.level = std::abs(voice.lastL) + std::abs(voice.lastR);
                voice.position += voice.step;
                ++voice.playedFrames;
                if (voice.releaseFrames > 0 && --voice.releaseFrames == 0)
                    voice.active = false;
                if (!voice.loop && voice.position >= voice.end)
                    voice.active = false;
            }
            left[i] += l * gain;
            right[i] += r * gain;
        }
        // GUI reads are atomic snapshots; never lock the audio callback.
        for (auto& progress : padProgress_)
            progress.store(-1.0f, std::memory_order_relaxed);
        for (const auto& voice : voices_) {
            if (!voice.active || voice.padIndex < 0 || voice.padIndex >= 16) continue;
            const double range = std::max(1.0, voice.end - voice.start);
            const auto value = static_cast<float>(std::max(0.0, std::min(1.0,
                               (voice.position - voice.start) / range)));
            padProgress_[static_cast<std::size_t>(voice.padIndex)].store(value,
                                                                          std::memory_order_relaxed);
        }
    }

    void allNotesOff() noexcept {
        for (auto& voice : voices_)
            if (voice.active) startRelease(voice, kFadeFrames);
    }

private:
    static constexpr int kFadeFrames = 32;
    static constexpr int kGrainHop = 1024;
    static double clampPercent(float value, double minimum, double maximum) noexcept {
        return std::isfinite(value) ? std::max(minimum, std::min(maximum,
            static_cast<double>(value))) : minimum;
    }
    struct Voice {
        const Sample* sample = nullptr;
        double position = 0, step = 1, start = 0, end = 0;
        float gain = 0, level = 0, lastL = 0, lastR = 0;
        float tailL = 0, tailR = 0, loopL = 0, loopR = 0;
        std::uint64_t age = 0, playedFrames = 0;
        std::uint64_t maxFrames = 0;
        int tailFrames = 0, releaseFrames = 0, releaseTotal = 0;
        int configuredRelease = 64, midiNote = -1, padIndex = -1;
        bool active = false, loop = false, gated = false;
        bool tempoSync = false;
        float sourceBpm = 120.0f;
        double syncOrigin = 0.0, grainCurrent = 0.0, grainPrevious = 0.0;
        int grainFrame = 0;
        std::uint64_t grainNumber = 0;
    };
    static float interpolate(const std::vector<float>& data, double pos,
                             double start, double end) noexcept {
        const double bounded = std::max(start, std::min(end - 1.0, pos));
        const auto idx = static_cast<std::size_t>(bounded);
        const auto next = std::min(idx + 1, static_cast<std::size_t>(end - 1.0));
        const float fraction = static_cast<float>(bounded - static_cast<double>(idx));
        return data[idx] + (data[next] - data[idx]) * fraction;
    }
    static void renderSyncVoice(Voice& v, float& l, float& r,
                                double attackFrames, double bpm) noexcept {
        // Two simultaneous 2048-frame grains, launched every 1024 output
        // frames. Their read heads move at the original pitch step; their
        // launch points advance at the independent tempo ratio. Linear
        // overlap-add windows sum to one after the first grain. Constant
        // work, no allocations, and no FFT or expensive per-sample trig.
        const auto& left = v.sample->left;
        const auto& right = v.sample->right.empty() ? left : v.sample->right;
        const double offset = static_cast<double>(v.grainFrame) * v.step;
        const float alpha = static_cast<float>(v.grainFrame) / kGrainHop;
        const double currentPos = v.grainCurrent + offset;
        const float cl = interpolate(left, currentPos, v.start, v.end);
        const float cr = interpolate(right, currentPos, v.start, v.end);
        float sl = cl, sr = cr;
        if (v.grainNumber > 0) {
            const double previousPos = v.grainPrevious + (kGrainHop + v.grainFrame) * v.step;
            sl = interpolate(left, previousPos, v.start, v.end) * (1.0f - alpha) + cl * alpha;
            sr = interpolate(right, previousPos, v.start, v.end) * (1.0f - alpha) + cr * alpha;
        }
        const double tempoRatio = std::max(0.125, std::min(8.0, bpm / v.sourceBpm));
        const double sourceStep = v.step * tempoRatio;
        const double remaining = (v.end - v.position) / sourceStep;
        const float endFade = v.loop ? 1.0f
            : static_cast<float>(std::min(1.0, std::max(0.0, remaining / kFadeFrames)));
        const float attack = attackFrames < 1.0 ? 1.0f
            : static_cast<float>(std::min(1.0, static_cast<double>(v.playedFrames) / attackFrames));
        const float durationFade = static_cast<float>(std::min(1.0,
            static_cast<double>(v.maxFrames - v.playedFrames) / kFadeFrames));
        const float release = v.releaseFrames > 0
            ? static_cast<float>(v.releaseFrames) / std::max(1, v.releaseTotal) : 1.0f;
        const float volume = v.gain * attack * durationFade * release * endFade;
        v.lastL = sl * volume;
        v.lastR = sr * volume;
        l += v.lastL;
        r += v.lastR;
        v.level = std::abs(v.lastL) + std::abs(v.lastR);
        v.position += sourceStep;
        ++v.playedFrames;
        if (++v.grainFrame == kGrainHop) {
            v.grainFrame = 0;
            ++v.grainNumber;
            v.grainPrevious = v.grainCurrent;
            v.grainCurrent = v.position;
        }
        if (v.releaseFrames > 0 && --v.releaseFrames == 0) v.active = false;
        if (!v.loop && v.position >= v.end) v.active = false;
    }
    static void startRelease(Voice& voice, int frames) noexcept {
        if (!voice.active) return;
        const int time = std::max(1, frames);
        if (voice.releaseFrames == 0 || time < voice.releaseFrames) {
            voice.releaseFrames = time;
            voice.releaseTotal = time;
        }
    }
    std::array<Voice, 32> voices_{};
    std::array<std::atomic<float>, 16> padProgress_{};
    std::atomic<const Bank*> bank_{nullptr};
    std::atomic<double> outputRate_{44100.0};
    std::atomic<float> tempoBpm_{120.0f};
    std::uint64_t counter_ = 0;
};

} // namespace vsl
