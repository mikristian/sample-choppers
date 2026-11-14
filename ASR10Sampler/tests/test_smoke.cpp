#ifdef NDEBUG
#undef NDEBUG // Keep assertions active in the Release-built test executable.
#endif
#include "../Source/PluginProcessor.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
struct StopDispatchTimer final : juce::Timer
{
    void timerCallback() override
    {
        stopTimer();
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }
};

float render(ASR10Processor& processor, int note)
{
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.95f), 0);
    float peak = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        buffer.clear();
        processor.processBlock(buffer, midi);
        midi.clear();
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto l = buffer.getSample(0, i);
            const auto r = buffer.getSample(1, i);
            assert(std::isfinite(l) && std::isfinite(r));
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
        }
    }
    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    processor.processBlock(buffer, midi);
    return peak;
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc >= 3 && juce::String::fromUTF8(argv[1]) == "--stress-banks")
    {
        const juce::File library(juce::String::fromUTF8(argv[2]));
        const auto parsed = juce::JSON::parse(library.getChildFile("bank-index.json"));
        const auto banks = parsed.getProperty("banks", {});
        if (!banks.isArray() || banks.getArray()->isEmpty())
        {
            std::cerr << "No indexed ASR-10 banks found\n";
            return 2;
        }
        const int available = banks.getArray()->size();
        const int limit = argc >= 4
            ? juce::jlimit(1, available, juce::String::fromUTF8(argv[3]).getIntValue())
            : available;
        ASR10Processor sampler;
        sampler.prepareToPlay(48000.0, 512);
        for (int i = 0; i < limit; ++i)
        {
            const auto entry = banks[i];
            const juce::File bankPath(entry.getProperty("path", "").toString());
            juce::String error;
            if (!sampler.loadBank(bankPath, error))
            {
                std::cerr << "BANK STRESS FAILED [" << i + 1 << "/"
                          << limit << "]: " << error.toStdString() << "\n";
                return 3;
            }
            for (int slot = 0; slot < 8; ++slot)
                assert(sampler.hasSample(slot));
            sampler.selectInstrument(i % 8);
            const float peak = render(sampler, sampler.getChopMode() ? 53 : 60);
            assert(std::isfinite(peak));
            sampler.stopSequence();
            sampler.allNotesOff();
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midi;
            sampler.processBlock(buffer, midi);  // clear the previous bank's voices
            std::cout << "PASS BANK " << (i + 1) << "/" << limit << " "
                      << sampler.getBankName().toStdString() << "\n";
        }
        std::cout << "ASR10 sequential bank-cache stress: PASS (" << limit << " banks)\n";
        return 0;
    }
    if (argc <= 1)
    {
    ASR10Processor processor;
    processor.prepareToPlay(48000.0, 512);
    assert(processor.hasSample(0));  // original generated starter sound
    assert(render(processor, 60) > 0.02f);

    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("asr10-smoke-tone", ".wav", false);
    {
        auto stream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream());
        assert(stream);
        juce::WavAudioFormat format;
        auto* rawWriter = format.createWriterFor(stream.get(), 44100.0, 2, 16, {}, 0);
        assert(rawWriter);
        stream.release();
        auto writer = std::unique_ptr<juce::AudioFormatWriter>(rawWriter);
        juce::AudioBuffer<float> tone(2, 44100);
        for (int i = 0; i < tone.getNumSamples(); ++i)
        {
            const auto amplitude = std::sin(2.0 * juce::MathConstants<double>::pi * 330.0
                                            * i / 44100.0) * 0.4;
            tone.setSample(0, i, static_cast<float>(amplitude));
            tone.setSample(1, i, static_cast<float>(amplitude * 0.8));
        }
        assert(writer->writeFromAudioSampleBuffer(tone, 0, tone.getNumSamples()));
    }

    assert(processor.loadSample(3, file));
    processor.selectInstrument(3);
    processor.setRootNote(60);
    processor.setLoopEnabled(true);
    processor.setSampleStart(0.1f);
    processor.setSampleEnd(0.9f);
    processor.setVolume(0.78f);
    processor.setFilter(0.6f);
    processor.setFxType(3);
    processor.setFxAmount(0.3f);
    processor.setTempo(138.0f);
    assert(processor.hasSample(3));
    assert(processor.getSelectedWaveformPeaks()[0] > 0.0f);
    assert(render(processor, 67) > 0.02f);

    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    assert(saved.getSize() > 100);
    ASR10Processor reopened;
    reopened.prepareToPlay(48000.0, 512);
    reopened.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    StopDispatchTimer timer;
    timer.startTimer(200);
    juce::MessageManager::getInstance()->runDispatchLoop();
    assert(reopened.hasSample(3));
    assert(reopened.getSelectedInstrument() == 3);
    assert(reopened.getRootNote() == 60);
    assert(reopened.getLoopEnabled());
    assert(reopened.getFxType() == 3);
    assert(std::abs(reopened.getTempo() - 138.0f) < 0.01f);
    assert(render(reopened, 67) > 0.02f);

    file.deleteFile();
    std::cout << "ASR10 offline audio, sample import and project restore: PASS\n";
    }
    if (argc > 1)
    {
        const juce::File bankFile(juce::String::fromUTF8(argv[1]));
        ASR10Processor bank;
        bank.prepareToPlay(48000.0, 512);
        juce::String error;
        if (!bank.loadBank(bankFile, error))
        {
            std::cerr << "BANK LOAD FAILED: " << error.toStdString() << '\n';
            return 2;
        }
        assert(bank.getBankName().isNotEmpty());
        for (int slot = 0; slot < 8; ++slot)
            assert(bank.hasSample(slot));
        const auto bankDocument = juce::JSON::parse(bankFile);
        const auto instrumentSettings = bankDocument.getProperty("instruments", {});
        for (int slot = 0; slot < 8; ++slot)
        {
            bank.selectInstrument(slot);
            const auto current = instrumentSettings[slot];
            const auto start = static_cast<float>(current.getProperty("sampleStart", 0.0));
            const auto end = static_cast<float>(current.getProperty("sampleEnd", 1.0));
            assert(std::abs(bank.getSampleStart() - start) < 0.0001f);
            assert(std::abs(bank.getSampleEnd() - end) < 0.0001f);
            const auto points = current.getProperty("chopPoints", {});
            if (points.isArray())
                for (int boundary = 0; boundary <= 16; ++boundary)
                    assert(std::abs(bank.getChopPoint(slot, boundary)
                         - static_cast<float>(points[boundary])) < 0.0001f);
        }
        assert(bank.getTempo() >= 30.0f && bank.getTempo() <= 300.0f);
        bank.selectInstrument(0);
        bank.setChopMode(true);
        float totalPeak = 0.0f;
        for (int note = 48; note <= 63; ++note)
            totalPeak = std::max(totalPeak, render(bank, note));
        assert(bank.getLastSlice() == 15);
        assert(totalPeak > 0.001f);

        // A loaded practice bank should play its own original 16-step phrase.
        juce::String repeatError;
        assert(bank.loadBank(bankFile, repeatError));
        bank.allNotesOff();
        juce::AudioBuffer<float> beatBuffer(2, 512);
        juce::MidiBuffer emptyMidi;
        bank.processBlock(beatBuffer, emptyMidi);
        bank.startSequence(false);
        float beatPeak = 0.0f;
        for (int block = 0; block < 16; ++block)
        {
            beatBuffer.clear();
            bank.processBlock(beatBuffer, emptyMidi);
            for (int sample = 0; sample < beatBuffer.getNumSamples(); ++sample)
            {
                const float left = beatBuffer.getSample(0, sample);
                const float right = beatBuffer.getSample(1, sample);
                assert(std::isfinite(left) && std::isfinite(right));
                beatPeak = std::max(beatPeak, std::max(std::abs(left), std::abs(right)));
            }
        }
        bank.stopSequence();
        // Owned-song banks intentionally start with a silent note sequence,
        // ready for the player to record; official practice banks include
        // their own original demo notes and should produce a pattern.
        bool hasProgrammedSteps = false;
        const auto importedSequence = bankDocument.getProperty("sequence", {});
        if (importedSequence.isArray())
            for (const auto& step : *importedSequence.getArray())
                if (step.isArray() && !step.getArray()->isEmpty())
                    hasProgrammedSteps = true;
        if (hasProgrammedSteps)
            assert(beatPeak > 0.001f);

        // A user-adjusted chop boundary must be editable without crossing
        // its neighbours and must survive host project save/reopen.
        const float oldBoundary = bank.getChopPoint(0, 1);
        assert(oldBoundary > 0.0001f);
        const float editedBoundary = oldBoundary * 0.5f;
        assert(bank.setChopPoint(0, 1, editedBoundary));
        assert(std::abs(bank.getChopPoint(0, 1) - editedBoundary) < 0.0001f);
        assert(!bank.setChopPoint(0, 1, 2.0f));
        assert(std::abs(bank.getChopPoint(0, 1) - editedBoundary) < 0.0001f);

        juce::MemoryBlock bankSaved;
        bank.getStateInformation(bankSaved);
        ASR10Processor bankReopened;
        bankReopened.prepareToPlay(48000.0, 512);
        bankReopened.setStateInformation(bankSaved.getData(), static_cast<int>(bankSaved.getSize()));
        assert(bankReopened.getBankName() == bank.getBankName());
        assert(bankReopened.getChopMode() == bank.getChopMode());
        for (int slot = 0; slot < 8; ++slot)
        {
            assert(bankReopened.hasSample(slot));
            for (int boundary = 0; boundary <= 16; ++boundary)
                assert(std::abs(bankReopened.getChopPoint(slot, boundary)
                               - bank.getChopPoint(slot, boundary)) < 0.0001f);
        }
        std::cout << "ASR10 8 instruments, 16 chops, practice sequence and state restore: PASS\n";
    }
}
