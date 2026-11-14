#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../Source/PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

// Regression: browsing more than the historical 128-clip limit must reclaim
// unused decoded audio without touching clips still read by the audio thread.
// All test sounds are original, locally generated and deleted afterward.
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ASR10Processor processor;
    processor.prepareToPlay(48000.0, 512);

    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getNonexistentChildFile("asr10-clip-rotation", {}, false);
    assert(directory.createDirectory());
    const auto source = directory.getChildFile("original-test-tone.wav");
    {
        auto stream = std::unique_ptr<juce::FileOutputStream>(source.createOutputStream());
        assert(stream);
        juce::WavAudioFormat wav;
        auto* rawWriter = wav.createWriterFor(stream.get(), 48000.0, 1, 16, {}, 0);
        assert(rawWriter);
        stream.release();
        std::unique_ptr<juce::AudioFormatWriter> writer(rawWriter);
        juce::AudioBuffer<float> sound(1, 2048);
        for (int frame = 0; frame < sound.getNumSamples(); ++frame)
            sound.setSample(0, frame, 0.4f *
                std::sin(2.0 * juce::MathConstants<double>::pi * 330.0 * frame / 48000.0));
        assert(writer->writeFromAudioSampleBuffer(sound, 0, sound.getNumSamples()));
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> nonfinite{false};
    std::atomic<int> blocks{0};
    std::thread callback([&]
    {
        juce::AudioBuffer<float> buffer(2, 512);
        juce::MidiBuffer midi;
        while (!stop.load(std::memory_order_acquire))
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
            for (int index = 0; index < buffer.getNumSamples(); ++index)
                if (!std::isfinite(buffer.getSample(0, index))
                    || !std::isfinite(buffer.getSample(1, index)))
                    nonfinite.store(true, std::memory_order_release);
            blocks.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    int loads = 0;
    constexpr int uniqueClips = 192;
    for (int index = 0; index < uniqueClips; ++index)
    {
        const auto file = directory.getChildFile("tone_" + juce::String(index) + ".wav");
        assert(source.copyFileTo(file));
        bool loaded = false;
        // The audio callback has priority: if clip reclamation yields to it,
        // a GUI load can be retried on the next message-thread opportunity.
        for (int attempt = 0; attempt < 1000 && !loaded; ++attempt)
        {
            loaded = processor.loadSample(index % 8, file);
            if (!loaded)
                std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        if (!loaded)
        {
            std::cerr << "FAILED clip " << index << " after " << loads
                      << " successfully imported distinct clips\n";
            break;
        }
        ++loads;
        processor.selectInstrument(index % 8);
        processor.triggerNote(60, 0.6f);
        processor.releaseNote(60);
    }

    stop.store(true, std::memory_order_release);
    callback.join();
    assert(loads == uniqueClips);
    assert(blocks.load() > 0);
    assert(!nonfinite.load());
    assert(processor.hasSample(0));
    assert(processor.hasSample(7));
    assert(directory.deleteRecursively());
    std::cout << "PASS " << loads << " distinct sample loads, "
              << blocks.load() << " concurrent audio blocks, finite output\n";
}
