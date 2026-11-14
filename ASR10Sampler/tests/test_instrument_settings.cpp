#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Source/PluginProcessor.h"
#include <cassert>
#include <cmath>
#include <memory>

namespace {
void near(float actual, float expected)
{
    assert(std::abs(actual - expected) < 0.0001f);
}

void checkSlot(ASR10Processor& p, int index, int root, float gain, float pitch)
{
    p.selectInstrument(index);
    assert(p.getSelectedInstrument() == index);
    assert(p.getRootNote() == root);
    near(p.getVolume(), gain);
    near(p.getPitch(), pitch);
}
}

// Standalone targeted test; the prime agent can link against processor sources.
int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    ASR10Processor p;
    p.selectInstrument(0);
    p.setRootNote(48);
    p.setVolume(0.2f);
    p.setPitch(-7.0f);
    p.setFilter(0.4f);
    p.setAttack(0.15f);
    p.setRelease(0.8f);
    p.setFxType(4);
    p.setFxAmount(0.65f);
    p.setLoopEnabled(true);
    p.setSampleStart(0.1f);
    p.setSampleEnd(0.85f);

    p.selectInstrument(4);
    p.setRootNote(72);
    p.setVolume(0.9f);
    p.setPitch(5.0f);
    p.setFilter(0.95f);
    p.setFxType(1);
    p.setLoopEnabled(false);
    p.setSampleStart(0.25f);
    p.setSampleEnd(0.75f);

    checkSlot(p, 0, 48, 0.2f, -7.0f);
    assert(p.getLoopEnabled());
    near(p.getAttack(), 0.15f);
    near(p.getRelease(), 0.8f);
    near(p.getFilter(), 0.4f);
    near(p.getFxAmount(), 0.65f);
    assert(p.getFxType() == 4);
    near(p.getSampleStart(), 0.1f);
    near(p.getSampleEnd(), 0.85f);
    checkSlot(p, 4, 72, 0.9f, 5.0f);
    assert(!p.getLoopEnabled());
    assert(p.getFxType() == 1);

    juce::MemoryBlock saved;
    p.getStateInformation(saved);
    ASR10Processor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    checkSlot(restored, 0, 48, 0.2f, -7.0f);
    assert(restored.getLoopEnabled());
    near(restored.getAttack(), 0.15f);
    near(restored.getRelease(), 0.8f);
    near(restored.getFilter(), 0.4f);
    assert(restored.getFxType() == 4);
    near(restored.getFxAmount(), 0.65f);
    near(restored.getSampleStart(), 0.1f);
    near(restored.getSampleEnd(), 0.85f);
    checkSlot(restored, 4, 72, 0.9f, 5.0f);
    assert(!restored.getLoopEnabled());
    assert(restored.getFxType() == 1);
    near(restored.getSampleStart(), 0.25f);
    near(restored.getSampleEnd(), 0.75f);

    // Churn through more than the previous hard 128-clip lifetime cap.
    // Each distinct path is a small generated WAV, so test storage stays low.
    juce::WavAudioFormat wav;
    auto base = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("asr10-cache-test", "", false);
    assert(base.createDirectory());
    juce::AudioBuffer<float> tone(2, 256);
    for (int sample = 0; sample < 256; ++sample) {
        const float v = 0.25f * std::sin(sample * 0.23f);
        tone.setSample(0, sample, v);
        tone.setSample(1, sample, v);
    }
    ASR10Processor churn;
    churn.prepareToPlay(48000.0, 256);
    juce::AudioBuffer<float> out(2, 256);
    juce::MidiBuffer midi;
    for (int index = 0; index < 144; ++index) {
        auto path = base.getChildFile("clip-" + juce::String(index) + ".wav");
        auto stream = std::unique_ptr<juce::FileOutputStream>(path.createOutputStream());
        assert(stream);
        auto* raw = wav.createWriterFor(stream.get(), 44100, 2, 16, {}, 0);
        assert(raw);
        stream.release();
        std::unique_ptr<juce::AudioFormatWriter> writer(raw);
        assert(writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples()));
        writer.reset();
        assert(churn.loadSample(index % 8, path));
        churn.selectInstrument(index % 8);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 0);
        churn.processBlock(out, midi);
        for (int frame = 0; frame < out.getNumSamples(); ++frame)
            assert(std::isfinite(out.getSample(0, frame)));
    }
    juce::MemoryBlock churnState;
    churn.getStateInformation(churnState);
    ASR10Processor churnRestored;
    churnRestored.setStateInformation(churnState.getData(), static_cast<int>(churnState.getSize()));
    for (int slot = 0; slot < 8; ++slot)
        assert(churnRestored.hasSample(slot));
    assert(base.deleteRecursively());
    return 0;
}
