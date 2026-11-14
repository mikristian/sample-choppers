#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include "SamplerEngine.h"
#include "MpcFxEngine.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

class VintageSamplerProcessor final : public juce::AudioProcessor,
                                      private juce::AsyncUpdater,
                                      private juce::Timer
{
public:
    VintageSamplerProcessor();
    ~VintageSamplerProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // All file loading and bank publication happen outside the audio callback.
    // The audio thread reads a stable, atomic, immutable bank pointer.
    bool loadSampleForPad(int pad, const juce::File& file);
    bool loadKitDirectory(const juce::File& directory);
    // Import an entire Music REA A-H bank export in one reviewed, atomic publish.
    // All audio decoding happens on the message thread, outside processBlock.
    bool loadMultiBankDirectory(const juce::File& directory);
    juce::String getPadName(int pad) const;
    void triggerPad(int pad) noexcept;
    void triggerPadVelocity(int pad, float velocity) noexcept;
    std::array<float, 256> getPadWaveformPeaks(int pad) const noexcept;
    void releasePad(int pad) noexcept;
    float getPadPlaybackProgress(int pad) const noexcept;
    bool isPadPlaying(int pad) const noexcept;
    juce::String getLastError() const;

    // A-H, zero-based. Select on the UI/message thread; no disk IO on switch.
    int getSelectedBank() const noexcept { return activeBank.load(std::memory_order_acquire); }
    void selectBank(int bank) noexcept;
    bool isPadMuted(int pad) const noexcept;
    void setPadMuted(int pad, bool muted) noexcept;
    void unmuteAllPads() noexcept;
    std::pair<float, float> getPadChop(int pad) const noexcept;
    void setPadChop(int pad, float startPercent, float endPercent) noexcept;
    bool chopPadTo16(int sourcePad);
    void setTempo(float bpm) noexcept;
    float getTempo() const noexcept;
    void setTempoSyncEnabled(bool enabled) noexcept;
    bool isTempoSyncEnabled() const noexcept;
    void setPadSourceBpm(int pad, float bpm) noexcept;
    float getPadSourceBpm(int pad) const noexcept;
    void setPadWarpAnchor(int pad, float percent) noexcept;
    float getPadWarpAnchor(int pad) const noexcept;
    void startSequence(bool recording) noexcept;
    void stopSequence() noexcept;
    bool isSequencePlaying() const noexcept;
    bool isSequenceRecording() const noexcept;
    void clearSequence() noexcept;
    void setStepEnabled(int pad, int step, bool enabled) noexcept;
    bool isStepEnabled(int pad, int step) const noexcept;
    void setNoteRepeatEnabled(bool enabled) noexcept;
    bool isNoteRepeatEnabled() const noexcept;
    void setPadHeld(int pad, bool held) noexcept;

    // Four independent serial stereo master effects, types 0..4 (0 = off).
    void setFxSlotType(int slot, int type) noexcept;
    int getFxSlotType(int slot) const noexcept;
    void setFxSlotAmount(int slot, float amount) noexcept;
    float getFxSlotAmount(int slot) const noexcept;
    void setFxSlotWet(int slot, float wet) noexcept;
    float getFxSlotWet(int slot) const noexcept;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void publishPad(int bank, int pad, std::shared_ptr<const vsl::Sample> sample);
    std::shared_ptr<const vsl::Sample> decodeSample(const juce::File& file);

    vsl::Engine engine;
    static constexpr int numberOfFxSlots = 4;
    struct FxSlot
    {
        std::atomic<int> type{0};
        std::atomic<float> amount{0.5f};
        std::atomic<float> wet{1.0f};
        vsl::MpcFxEngine dsp;
        int lastRenderedType = 0; // audio thread only
    };
    std::array<FxSlot, numberOfFxSlots> fxSlots{};

    // Lifetime model: never free an immutable bank while processBlock could
    // reference it. Banks hold shared_ptrs to their corresponding immutable
    // PCM clips. Retained banks are destroyed only when this processor stops.
    std::vector<std::unique_ptr<vsl::Bank>> retainedBanks;
    std::atomic<const vsl::Bank*> publishedBank { nullptr };
    static constexpr size_t maximumBankRevisions = 512;
    static constexpr int numberOfBanks = 8;
    std::atomic<int> activeBank{0};
    std::array<std::atomic<const vsl::Bank*>, numberOfBanks> bankSnapshots{};
    int lastRenderedBank = -1; // audio thread only; release old voices on bank changes

    // Ordered, bounded UI->audio SPSC ring. A bool per pad lost rapid taps
    // and processed simultaneous GUI presses in pad-number rather than
    // performance order. Mouse/keyboard callbacks run on the message thread.
    struct UiTrigger { int pad = 0; float velocity = 1.0f; };
    static constexpr std::uint32_t uiQueueCapacity = 256;
    std::array<UiTrigger, uiQueueCapacity> uiTriggerQueue{};
    std::atomic<std::uint32_t> uiTriggerWrite{0};
    std::atomic<std::uint32_t> uiTriggerRead{0};
    std::array<std::atomic<bool>, 16> uiReleaseFlags;
    mutable juce::CriticalSection stateLock; // non-audio-thread GUI/state only
    std::array<std::array<juce::String, 16>, numberOfBanks> samplePaths;
    // Human-readable pad labels from trusted local kit manifests. Unlike the
    // required pad_01.wav filenames, these tell performers which drum/vocal
    // chop each pad contains. Persisted in host projects alongside paths.
    std::array<std::array<juce::String, 16>, numberOfBanks> sampleDisplayNames;
    struct PadControls
    {
        std::array<std::atomic<bool>, 16> muted{};
        std::array<std::atomic<float>, 16> start{}, end{};
        std::array<std::atomic<float>, 16> sourceBpm{}, warpAnchor{};
        std::array<std::atomic<std::uint32_t>, 16> steps{};
    };
    std::array<PadControls, numberOfBanks> bankControls{};
    std::atomic<float> tempo{120.0f};
    std::atomic<bool> tempoSyncEnabled{false};
    std::atomic<bool> sequencePlaying{false}, sequenceRecording{false};
    std::atomic<bool> sequenceReset{false};
    std::atomic<int> sequenceCurrentStep{0};
    double sequenceSamplesUntilStep = 0.0; // audio thread only
    int sequenceNextStep = 0; // audio thread only
    std::atomic<bool> noteRepeatEnabled{false};
    std::array<std::atomic<bool>, 16> padHeld{};
    double repeatSamplesUntilHit = 0.0; // audio thread only
    bool repeatWasActive = false; // audio thread only
    vsl::PlaybackOptions optionsForPad(int bank, int pad, const vsl::PlaybackOptions& global) const noexcept;
    bool isPadMutedInBank(int bank, int pad) const noexcept;
    bool isStepEnabledInBank(int bank, int pad, int step) const noexcept;
    juce::String lastError;
    juce::String startupProjectPath; // message thread only, single use

    std::atomic<float>* gainParameter = nullptr;
    std::atomic<float>* pitchParameter = nullptr;
    std::atomic<float>* attackParameter = nullptr;
    std::atomic<float>* modeParameter = nullptr;
    std::atomic<float>* keyboardPadParameter = nullptr;
    std::atomic<float>* rootNoteParameter = nullptr;
    std::atomic<float>* lengthParameter = nullptr;
    std::atomic<float>* sampleStartParameter = nullptr;
    std::atomic<float>* sampleEndParameter = nullptr;
    std::atomic<float>* loopParameter = nullptr;
    std::atomic<float>* gateParameter = nullptr;
    std::atomic<float>* releaseParameter = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageSamplerProcessor)
};
