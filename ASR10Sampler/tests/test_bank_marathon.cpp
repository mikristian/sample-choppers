#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../Source/PluginProcessor.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Offline endurance check for a bank library intended to be browsed live.
// Every bank must import all 8 sample slots, survive a few audio callbacks,
// and keep finite stereo playback. Audio assets remain on Expansion.
int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: ASR10SmokeManual <bank-directory>\n";
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File folder(juce::String::fromUTF8(argv[1]));
    if (!folder.isDirectory()) return 2;
    juce::Array<juce::File> manifests;
    folder.findChildFiles(manifests, juce::File::findFiles, false, "*.json");
    manifests.sort();
    if (manifests.size() < 20) return 2;

    ASR10Processor p;
    p.prepareToPlay(44100.0, 256);
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    float maxPeak = 0.0f;
    int loaded = 0;
    for (const auto& bank : manifests)
    {
        juce::String error;
        if (!p.loadBank(bank, error))
        {
            std::cerr << "BANK LOAD FAILED (" << loaded << " loaded): "
                      << bank.getFileName().toStdString() << ": "
                      << error.toStdString() << "\n";
            return 1;
        }
        for (int slot = 0; slot < 8; ++slot)
            assert(p.hasSample(slot));
        assert(p.getBankName().isNotEmpty());
        p.selectInstrument(0);
        p.allNotesOff();
        p.triggerNote(p.getChopMode() ? 48 : p.getRootNote(), 0.85f);
        for (int block = 0; block < 6; ++block)
        {
            audio.clear();
            p.processBlock(audio, midi);
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const float l = audio.getSample(0, i);
                const float r = audio.getSample(1, i);
                assert(std::isfinite(l) && std::isfinite(r));
                maxPeak = std::max(maxPeak, std::max(std::abs(l), std::abs(r)));
            }
        }
        ++loaded;
        std::cout << "PASS " << loaded << " " << bank.getFileNameWithoutExtension()
                  << std::endl;
    }
    if (maxPeak < 0.001f)
    {
        std::cerr << "All bank audio was silent\n";
        return 1;
    }
    std::cout << "BANK MARATHON PASSED " << loaded
              << " banks, " << (loaded * 8) << " assigned sample slots, finite stereo output\n";
    return 0;
}
