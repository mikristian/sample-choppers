#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class ASR10Processor final : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    ASR10Processor();
    ~ASR10Processor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool loadSample(int instrument, const juce::File&);
    bool loadBank(const juce::File& manifest, juce::String& error);
    juce::String getLastSampleLoadError() const;
    juce::String getBankName() const;
    void setChopMode(bool) noexcept;
    bool getChopMode() const noexcept;
    int getLastSlice() const noexcept;
    bool setChopPoint(int instrument, int boundary, float normalized) noexcept;
    float getChopPoint(int instrument, int boundary) const noexcept;
    juce::String getInstrumentName(int) const;
    bool hasSample(int) const noexcept;
    void selectInstrument(int) noexcept;
    int getSelectedInstrument() const noexcept;
    void triggerNote(int midiNote, float velocity = 1.0f) noexcept;
    void releaseNote(int midiNote) noexcept;
    void allNotesOff() noexcept;
    void setRootNote(int) noexcept;
    int getRootNote() const noexcept;
    void setLoopEnabled(bool) noexcept;
    bool getLoopEnabled() const noexcept;
    void setSampleStart(float) noexcept;
    float getSampleStart() const noexcept;
    void setSampleEnd(float) noexcept;
    float getSampleEnd() const noexcept;
    void setVolume(float) noexcept;
    float getVolume() const noexcept;
    void setAttack(float) noexcept;
    float getAttack() const noexcept;
    void setRelease(float) noexcept;
    float getRelease() const noexcept;
    void setPitch(float) noexcept;
    float getPitch() const noexcept;
    void setFilter(float) noexcept;
    float getFilter() const noexcept;
    void setFxType(int) noexcept;
    int getFxType() const noexcept;
    void setFxAmount(float) noexcept;
    float getFxAmount() const noexcept;
    std::array<float, 128> getSelectedWaveformPeaks() const noexcept;
    void startSequence(bool recording) noexcept;
    void stopSequence() noexcept;
    bool isSequencePlaying() const noexcept;
    bool isSequenceRecording() const noexcept;
    float getTempo() const noexcept;
    void setTempo(float) noexcept;

private:
    struct Clip {
        std::vector<float> left, right;
        std::array<float, 128> peaks{};
        double rate = 44100.0;
        juce::String name, path;
    };
    struct Voice {
        const Clip* clip = nullptr;
        double position = 0.0, increment = 1.0;
        float velocity = 0.0f, envelope = 0.0f, filterL = 0.0f, filterR = 0.0f;
        int note = -1;
        double regionBegin = 0.0, regionEnd = 0.0;
        bool chopped = false;
        bool released = false, active = false;
        uint64_t age = 0;
        float attackStep = 1.0f, releaseCoeff = 0.0f, filterAlpha = 1.0f, volume = 0.8f;
        bool looping = false;
    };
    struct InstrumentSettings {
        std::atomic<int> rootNote{60}, fxType{0};
        std::atomic<bool> loop{false};
        std::atomic<float> sampleStart{0.0f}, sampleEnd{1.0f}, gain{0.8f};
        std::atomic<float> attack{0.005f}, release{0.25f}, pitch{0.0f};
        std::atomic<float> filter{1.0f}, fxAmount{0.3f};
        std::array<std::atomic<float>, 17> chopPoints{};
        // Odd during a marker update; audio reads never wait for a writer.
        std::atomic<uint32_t> chopRevision{0};
    };
    struct Step {
        std::atomic<uint64_t> notesLow{0}, notesHigh{0};
    };
    static constexpr int instruments = 8, maxVoices = 32;
    void handleAsyncUpdate() override;
    void beginVoice(int note, float velocity) noexcept;
    void endVoice(int note) noexcept;
    void emitSequenceStep(int step) noexcept;
    void replaceChopPoints(int instrument, const std::array<float, 17>& points) noexcept;
    static std::unique_ptr<Clip> makeFactoryClip();
    std::array<std::atomic<const Clip*>, instruments> clips{};
    // Immutable decoded clips. Metadata/ownership changes only on the message
    // thread; the callback accesses published raw pointers without locking.
    std::vector<std::unique_ptr<Clip>> retainedClips;
    static constexpr size_t maxCachedClips = 40;
    static constexpr size_t maxDecodedBytes = 256u * 1024u * 1024u;
    size_t cacheBytes = 0;
    // The audio thread never blocks; eviction only runs outside processBlock.
    // 0 = idle, 1 = in callback, 2 = writer reclaiming. A contended callback
    // returns silent output without trying to access clips.
    std::atomic<uint8_t> callbackGate{0};
    bool reclaimCache(size_t incomingBytes = 0);
    std::unique_ptr<Clip> decodeSample(const juce::File& file, juce::String& error);
    bool installBankClips(std::array<std::unique_ptr<Clip>, instruments>& staged,
                          const std::array<const Clip*, instruments>& reused,
                          juce::String& error);
    juce::String sampleLoadError;
    std::array<juce::String, instruments> paths{}, names{};
    juce::String bankName { "INIT BANK" };
    std::array<Voice, maxVoices> voices{};
    std::array<InstrumentSettings, instruments> instrumentSettings{};
    std::array<std::atomic<uint8_t>, 128> noteEvents{}; // 1 = on, 2 = off
    std::array<std::atomic<float>, 128> queuedVelocity{};
    std::atomic<bool> panic{false};
    std::atomic<int> currentInstrument{0};
    std::atomic<bool> playing{false}, recording{false}, seqReset{false};
    std::atomic<bool> chopMode{false};
    std::atomic<int> lastSlice{-1};
    std::atomic<float> tempo{120.0f};
    std::array<Step, 16> steps{};
    std::atomic<int> seqStep{0};
    double seqCountdown = 0.0, samplerate = 44100.0;
    uint64_t noteAge = 0;
    float crushHoldL = 0.0f, crushHoldR = 0.0f;
    int crushCounter = 0;
    float fxFilterL = 0.0f, fxFilterR = 0.0f; // audio thread only
    int lastFxType = -1; // audio thread only
    juce::AudioFormatManager formats;
    mutable juce::CriticalSection nonAudioStateLock;
    juce::String pendingState; // setStateInformation -> AsyncUpdater (message thread)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ASR10Processor)
};
