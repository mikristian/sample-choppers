// Headless integration test. Requires a verified Music REA eight-bank
// exported folder as argv[1]. Performs no writes to source audio or project.
#include "../Source/PluginProcessor.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        std::cerr << "Usage: test <absolute folder with banks.json> [new-session.vsl]\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    const juce::File root(juce::String::fromUTF8(argv[1]));
    if (!root.getChildFile("banks.json").existsAsFile())
    {
        std::cerr << "Missing banks.json\n";
        return 2;
    }

    VintageSamplerProcessor processor;
    assert(processor.getSelectedBank() == 0);
    assert(!processor.isTempoSyncEnabled());
    const auto* startup = std::getenv("VSL_STARTUP_PROJECT");
    if (startup != nullptr && *startup != '\0')
    {
        std::unique_ptr<juce::AudioProcessorEditor> startupUi(processor.createEditor());
        // Standalone wrapper preference restore runs after construction;
        // startup sessions intentionally resolve on the message loop.
        auto* messages = juce::MessageManager::getInstance();
        juce::Timer::callAfterDelay(700, [messages] { messages->stopDispatchLoop(); });
        messages->runDispatchLoop();
        std::cerr << "Startup inspect: pad=" << processor.getPadName(0)
                  << " bpm=" << processor.getTempo()
                  << " source=" << processor.getPadSourceBpm(0)
                  << " step=" << processor.isStepEnabled(0, 0) << "\n";
        assert(processor.getPadName(0).contains("CHOP"));
        assert(processor.getTempo() > 70.0f && processor.getTempo() < 75.0f);
        assert(processor.getPadSourceBpm(0) > 140.0f);
        assert(processor.isStepEnabled(0, 0));
    }
    assert(processor.loadKitDirectory(root));
    assert(processor.getPadName(0).contains("CHOP"));
    assert(processor.getPadWaveformPeaks(0)[0] >= 0);

    processor.selectBank(7);
    assert(processor.getSelectedBank() == 7);
    assert(processor.getPadName(0).contains("CHOP"));
    assert(processor.getPadSourceBpm(0) > 60.0f);
    processor.setTempoSyncEnabled(true);
    processor.setTempo(125.0f);
    processor.setPadSourceBpm(0, 80.75f);
    processor.setPadWarpAnchor(0, 12.5f);
    assert(processor.isTempoSyncEnabled());
    assert(std::abs(processor.getTempo() - 125.0f) < 0.01f);
    assert(std::abs(processor.getPadSourceBpm(0) - 80.75f) < 0.01f);
    assert(std::abs(processor.getPadWarpAnchor(0) - 12.5f) < 0.01f);

    processor.prepareToPlay(44100.0, 512);
    processor.triggerPadVelocity(0, 1.0f);
    double energy = 0.0;
    juce::MidiBuffer emptyMidi;
    for (int frame = 0; frame < 32; ++frame)
    {
        juce::AudioBuffer<float> audio(2, 512);
        processor.processBlock(audio, emptyMidi);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 512; ++n)
            {
                const float value = audio.getSample(c, n);
                assert(std::isfinite(value));
                energy += std::abs(value);
            }
    }
    assert(energy > 0.1);

    // Burst 201 GUI pad presses into the queue before one audio callback.
    // The most recent pad must win, even if it has a lower pad number than
    // earlier hits. No merged/dropped per-pad flags or old voice overlap.
    processor.selectBank(0);
    for (int n = 0; n < 100; ++n)
    {
        processor.triggerPadVelocity(15, 0.7f);
        processor.triggerPadVelocity(0, 0.7f);
    }
    processor.triggerPadVelocity(2, 0.8f);
    {
        juce::AudioBuffer<float> rapid(2, 512);
        processor.processBlock(rapid, emptyMidi);
        assert(processor.isPadPlaying(2));
        assert(!processor.isPadPlaying(15));
        assert(!processor.isPadPlaying(0));
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < rapid.getNumSamples(); ++sample)
                assert(std::isfinite(rapid.getSample(channel, sample)));
    }
    // Physical FLX4 uses ordinary MIDI notes 36..51, including when several
    // arrive at different sample offsets within one audio callback.
    juce::MidiBuffer flx4Midi;
    flx4Midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 0);
    flx4Midi.addEvent(juce::MidiMessage::noteOn(1, 38, 0.8f), 96);
    flx4Midi.addEvent(juce::MidiMessage::noteOff(1, 36), 144);
    flx4Midi.addEvent(juce::MidiMessage::noteOn(1, 39, 0.8f), 192);
    {
        juce::AudioBuffer<float> rendered(2, 512);
        processor.processBlock(rendered, flx4Midi);
        assert(!processor.isPadPlaying(0));
        assert(!processor.isPadPlaying(2));
        assert(processor.isPadPlaying(3));
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < rendered.getNumSamples(); ++sample)
                assert(std::isfinite(rendered.getSample(channel, sample)));
    }
    processor.selectBank(7); // restore the bank for save/reopen assertions

    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    assert(saved.getSize() > 1024);
    VintageSamplerProcessor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    assert(restored.getSelectedBank() == 7);
    assert(restored.isTempoSyncEnabled());
    assert(std::abs(restored.getTempo() - 125.0f) < 0.01f);
    assert(std::abs(restored.getPadSourceBpm(0) - 80.75f) < 0.01f);
    assert(std::abs(restored.getPadWarpAnchor(0) - 12.5f) < 0.01f);
    if (argc == 3)
    {
        // Offer a ready-to-load 128-chop .vsl project in addition to the GUI
        // folder import. Save a clean, playable initial state in bank A, with
        // one original 16-step sample-flip demonstration (NOT reference notes).
        const auto bankIndex = juce::JSON::parse(root.getChildFile("banks.json"));
        const auto ranking = bankIndex.getProperty("ranking", {});
        const float sourceBpm = static_cast<float>(ranking.getProperty("tempo_bpm", 120.0f));
        const float validBpm = std::isfinite(sourceBpm) && sourceBpm > 40 && sourceBpm < 280
            ? sourceBpm : 120.0f;
        processor.stopSequence();
        processor.setTempoSyncEnabled(false); // Preserve source speed by default.
        processor.setTempo(validBpm / 2.0f);
        processor.selectBank(7);
        processor.setPadSourceBpm(0, validBpm);
        processor.setPadWarpAnchor(0, 0.0f);
        processor.selectBank(0);
        processor.clearSequence();
        const std::array<std::pair<int, int>, 10> originalFlip{{
            {0, 0}, {2, 2}, {1, 4}, {4, 6}, {0, 8},
            {7, 10}, {3, 11}, {9, 12}, {6, 14}, {3, 15}
        }};
        for (const auto& hit : originalFlip)
            processor.setStepEnabled(hit.first, hit.second, true);
        juce::MemoryBlock prepared;
        processor.getStateInformation(prepared);
        juce::File session(juce::String::fromUTF8(argv[2]));
        if (session.getFileExtension() != ".vsl" || session.existsAsFile()
            || !session.getParentDirectory().isDirectory()
            || !session.create().wasOk()
            || !session.replaceWithData(prepared.getData(), prepared.getSize()))
        {
            std::cerr << "Session output failed or already exists\n";
            return 3;
        }
        std::cout << "Session preset: " << session.getFullPathName() << "\n";
    }
    restored.releaseResources();
    processor.releaseResources();
    std::cout << "PASS: 128 Music REA chops; A-H bank switch; tempo sync; "
                 "warp marker; stereo audio; saved session restore\n";
    return 0;
}
