#include "../Source/SamplerEngine.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

static bool near(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}

int main() {
    using namespace vsl;
    auto s = std::make_shared<Sample>();
    s->sampleRate = 48000;
    s->left.assign(2048, 0.5f);
    s->right.assign(2048, -0.25f);
    assert(s->valid());
    Bank bank;
    bank.pads[0] = s;
    Engine engine;
    engine.setBank(&bank);
    engine.setOutputRate(48000);
    float l[128]{}, r[128]{};

    engine.noteOn(35, 1.0f);
    engine.noteOn(52, 1.0f);
    engine.render(l, r, 128, 1.0f);
    assert(near(l[127], 0) && near(r[127], 0));

    engine.noteOn(36, 1.0f);
    engine.render(l, r, 128, 1.0f, 0.004f);
    assert(near(l[0], 0) && near(r[0], 0));
    assert(l[127] > 0.3f && r[127] < -0.15f);
    assert(near(r[127], -0.5f * l[127]));

    engine.allNotesOff();
    float fadeL[64]{}, fadeR[64]{};
    engine.render(fadeL, fadeR, 64, 1.0f);
    assert(fadeL[0] > fadeL[20] && near(fadeL[63], 0));

    Engine pitched;
    pitched.setBank(&bank);
    pitched.setOutputRate(48000);
    float pl[128]{}, pr[128]{};
    pitched.noteOn(72, 1.0f, 0.0f, true, 0, 60);
    pitched.render(pl, pr, 128, 1, 0);
    assert(near(pl[0], 0.5f));
    assert(near(pr[0], -0.25f));
    pitched.allNotesOff();

    Engine many;
    many.setBank(&bank);
    many.setOutputRate(48000);
    for (int i = 0; i < 48; ++i) many.noteOn(36, 0.3f);
    float ml[256]{}, mr[256]{};
    many.render(ml, mr, 256, 0.5f);
    for (int i = 0; i < 256; ++i)
        assert(std::isfinite(ml[i]) && std::isfinite(mr[i]));

    Engine guard;
    guard.setBank(&bank);
    guard.setOutputRate(0);
    guard.noteOn(36, std::numeric_limits<float>::quiet_NaN());
    guard.noteOn(36, 1.0f, std::numeric_limits<float>::infinity());
    guard.noteOn(128, 1.0f);
    float gl[64]{}, gr[64]{};
    guard.render(gl, gr, 64, 1.0f);
    assert(near(gl[63], 0));
    Sample bad;
    bad.left = {1,2};
    bad.right = {1};
    assert(!bad.valid());

    // A 50-ms source does not magically become long unless looping is on.
    // With Loop ON it must sustain audibly to the requested maximum time.
    auto tiny = std::make_shared<Sample>();
    tiny->sampleRate = 48000;
    tiny->left.assign(2400, 0.42f);
    tiny->right.assign(2400, 0.42f);
    Bank shortBank;
    shortBank.pads[0] = tiny;
    Engine sustain;
    sustain.setBank(&shortBank);
    sustain.setOutputRate(48000);
    PlaybackOptions held;
    held.loop = true;
    held.maxLengthSeconds = 0.5f;
    sustain.noteOn(36, 1.0f, 0, false, 0, 60, held);
    std::vector<float> longL(4800), longR(4800);
    sustain.render(longL.data(), longR.data(), 4800, 1.0f);
    assert(longL.back() > 0.1f);
    assert(sustain.isPadPlaying(0));
    assert(sustain.getPadPlaybackProgress(0) >= 0.0f);
    std::vector<float> cutoffL(24000), cutoffR(24000);
    sustain.render(cutoffL.data(), cutoffR.data(), 24000, 1.0f);
    assert(!sustain.isPadPlaying(0));
    assert(near(cutoffL.back(), 0.0f));

    // Gate mode must respond to a specific MIDI note off with smooth release,
    // unlike the one-shot mode that ignores note off events.
    Engine gates;
    gates.setBank(&shortBank);
    gates.setOutputRate(48000);
    held.maxLengthSeconds = 2.0f;
    held.gated = true;
    held.releaseSeconds = 0.05f;
    gates.noteOn(36, 1.0f, 0.0f, false, 0, 60, held);
    float preL[256]{}, preR[256]{};
    gates.render(preL, preR, 256, 1.0f);
    gates.noteOff(36);
    std::vector<float> postL(3000), postR(3000);
    gates.render(postL.data(), postR.data(), 3000, 1.0f);
    assert(postL[0] > postL[1000]);
    assert(near(postL.back(), 0.0f));
    assert(!gates.isPadPlaying(0));

    Engine oneShot;
    oneShot.setBank(&shortBank);
    oneShot.setOutputRate(48000);
    held.gated = false;
    oneShot.noteOn(36, 1, 0, false, 0, 60, held);
    oneShot.noteOff(36);
    float oneShotL[500]{}, oneShotR[500]{};
    oneShot.render(oneShotL, oneShotR, 500, 1.0f);
    assert(oneShotL[499] > 0.05f);

    // Start/end trims are applied at new voice boundaries with no missing
    // sample overruns, including high varispeed keyboard playback.
    Engine trimming;
    trimming.setBank(&shortBank);
    trimming.setOutputRate(48000);
    PlaybackOptions selection;
    selection.sampleStartPercent = 25.0f;
    selection.sampleEndPercent = 50.0f;
    selection.loop = true;
    selection.maxLengthSeconds = 0.2f;
    trimming.noteOn(72, 0.8f, 8.0f, true, 0, 60, selection);
    std::vector<float> trimL(5000), trimR(5000);
    trimming.render(trimL.data(), trimR.data(), 5000, 0.5f);
    for (float value : trimL) assert(std::isfinite(value));
    for (float value : trimR) assert(std::isfinite(value));

    // Non-synced voices must retain exact interpolated source positions.
    auto ramp = std::make_shared<Sample>();
    ramp->sampleRate = 48000;
    ramp->left.resize(48000);
    for (int i = 0; i < 48000; ++i) ramp->left[i] = static_cast<float>(i) / 48000.0f;
    Bank rampBank;
    rampBank.pads[0] = ramp;
    Engine unchanged;
    unchanged.setBank(&rampBank);
    unchanged.setOutputRate(48000);
    unchanged.setTempoBpm(180);
    unchanged.noteOn(36, 1, 0, false, 0, 60, {});
    float checkL[128]{}, checkR[128]{};
    unchanged.render(checkL, checkR, 128, 1, 0);
    for (int i = 0; i < 128; ++i)
        assert(std::fabs(checkL[i] - i / 48000.0f) < 0.000001f);

    // A 440-Hz sinusoid should retain its frequency while playback duration
    // follows the inverse of target/source BPM. A deterministic central
    // window avoids attacks and grain discontinuities in the pitch estimate.
    auto tone = std::make_shared<Sample>();
    tone->sampleRate = 48000;
    tone->left.resize(48000);
    for (int i = 0; i < 48000; ++i)
        tone->left[i] = 0.65f * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / 48000.0);
    Bank toneBank;
    toneBank.pads[0] = tone;
    for (const float ratio : {0.75f, 1.5f}) {
        Engine stretched;
        stretched.setBank(&toneBank);
        stretched.setOutputRate(48000);
        stretched.setTempoBpm(120.0f * ratio);
        PlaybackOptions sync;
        sync.tempoSync = true;
        sync.sourceBpm = 120;
        sync.maxLengthSeconds = 4;
        stretched.noteOn(36, 1, 0, false, 0, 60, sync);
        const int expectedFrames = static_cast<int>(48000 / ratio);
        std::vector<float> out(expectedFrames + 256, 0.0f), silent(out.size(), 0.0f);
        int frames = 0;
        while (stretched.isPadPlaying(0) || frames == 0) {
            const int n = std::min(128, static_cast<int>(out.size()) - frames);
            if (n <= 0) break;
            stretched.render(out.data() + frames, silent.data() + frames, n, 1, 0);
            frames += n;
        }
        assert(std::abs(frames - expectedFrames) <= 128);
        assert(!stretched.isPadPlaying(0));
        int crossings = 0;
        const int begin = 12000, finish = 24000;
        for (int i = begin + 1; i < finish; ++i)
            if (out[i - 1] <= 0.0f && out[i] > 0.0f) ++crossings;
        const float frequency = crossings * 48000.0f / (finish - begin);
        assert(frequency > 405.0f && frequency < 475.0f);
        for (int i = begin + 1; i < finish; ++i)
            assert(std::fabs(out[i] - out[i - 1]) < 0.22f);
        for (float value : out) assert(std::isfinite(value));
    }

    // Warp marker selects the initial source origin within current trim.
    PlaybackOptions anchored;
    anchored.tempoSync = true;
    anchored.sampleStartPercent = 10;
    anchored.sampleEndPercent = 90;
    anchored.warpAnchorPercent = 50;
    Engine anchorEngine;
    anchorEngine.setBank(&rampBank);
    anchorEngine.setOutputRate(48000);
    anchorEngine.noteOn(36, 1, 0, false, 0, 60, anchored);
    float anchoredL[32]{}, anchoredR[32]{};
    anchorEngine.render(anchoredL, anchoredR, 32, 1, 0);
    assert(near(anchoredL[0], 0.5f, 0.0001f));
    assert(anchorEngine.getPadPlaybackProgress(0) > 0.49f);

    // A changing target tempo affects source phase on the next render block.
    Engine changing;
    changing.setBank(&rampBank);
    changing.setOutputRate(48000);
    changing.setTempoBpm(120);
    changing.noteOn(36, 1, 0, false, 0, 60, anchored);
    float beforeL[1024]{}, beforeR[1024]{};
    changing.render(beforeL, beforeR, 1024, 1, 0);
    const float beforeProgress = changing.getPadPlaybackProgress(0);
    changing.setTempoBpm(180);
    changing.render(beforeL, beforeR, 1024, 1, 0);
    const float progressDelta = changing.getPadPlaybackProgress(0) - beforeProgress;
    assert(progressDelta > 0.039f && progressDelta < 0.041f);

    // Looping and 32 overlapping synced voices stay finite and bounded.
    Engine crowded;
    crowded.setBank(&toneBank);
    crowded.setOutputRate(48000);
    crowded.setTempoBpm(180);
    PlaybackOptions loops;
    loops.tempoSync = true;
    loops.loop = true;
    loops.sourceBpm = 120;
    loops.maxLengthSeconds = 0.3f;
    for (int i = 0; i < 32; ++i) crowded.noteOn(36, 0.02f, 0, false, 0, 60, loops);
    std::vector<float> busyL(16000), busyR(16000);
    crowded.render(busyL.data(), busyR.data(), static_cast<int>(busyL.size()), 1, 0);
    for (float value : busyL) assert(std::isfinite(value) && std::fabs(value) < 1.0f);
    assert(!crowded.isPadPlaying(0));

    // Exclusive sample-chop mode must cut the old source at a new press and
    // retain just the short declick tail. Test rapid retrigger and A->B->A.
    auto positive = std::make_shared<Sample>();
    auto negative = std::make_shared<Sample>();
    positive->sampleRate = negative->sampleRate = 48000;
    positive->left.assign(48000, 0.65f);
    negative->left.assign(48000, -0.65f);
    Bank flipBank;
    flipBank.pads[0] = positive;
    flipBank.pads[1] = negative;
    Engine exclusive;
    exclusive.setBank(&flipBank);
    exclusive.setOutputRate(48000);
    PlaybackOptions cut;
    cut.chokePrevious = true;
    exclusive.noteOn(36, 1, 0, false, 0, 60, cut);
    float firstL[256]{}, firstR[256]{};
    exclusive.render(firstL, firstR, 256, 1, 0);
    assert(firstL[255] > 0.6f);
    exclusive.noteOn(37, 1, 0, false, 0, 60, cut);
    assert(!exclusive.isPadPlaying(0));
    assert(exclusive.isPadPlaying(1));
    float nextL[256]{}, nextR[256]{};
    exclusive.render(nextL, nextR, 256, 1, 0.003f);
    assert(nextL[0] > 0.5f); // initial 32-frame declick from old sound
    assert(nextL[48] < 0.0f); // old source fully absent after the 32-frame declick
    assert(nextL[200] < -0.6f); // new source fully audible after its attack
    for (int i = 0; i < 128; ++i) {
        const int pad = i & 1;
        exclusive.noteOn(36 + pad, 1, 0, false, 0, 60, cut);
        float tempL[5]{}, tempR[5]{};
        exclusive.render(tempL, tempR, 5, 1, 0);
        assert(!exclusive.isPadPlaying(1 - pad));
        assert(exclusive.isPadPlaying(pad));
        for (float value : tempL) assert(std::isfinite(value) && std::abs(value) <= 1.001f);
    }
    float tailL[128]{}, tailR[128]{};
    exclusive.render(tailL, tailR, 128, 1, 0);
    assert(tailL[64] < -0.6f);

    std::cout << "SamplerEngine C++17 tests passed\n";
}
