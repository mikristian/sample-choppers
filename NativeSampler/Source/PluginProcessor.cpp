#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

VintageSamplerProcessor::VintageSamplerProcessor()
    : juce::AudioProcessor(juce::AudioProcessor::BusesProperties()
                               .withOutput("Stereo Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", makeParameterLayout())
{
    for (auto& held : padHeld)
        held.store(false, std::memory_order_relaxed);
    for (auto& flag : uiReleaseFlags)
        flag.store(false, std::memory_order_relaxed);
    for (int bank = 0; bank < numberOfBanks; ++bank)
    {
        for (int pad = 0; pad < 16; ++pad)
        {
            bankControls[bank].muted[pad].store(false);
            bankControls[bank].start[pad].store(0.0f);
            bankControls[bank].end[pad].store(100.0f);
            bankControls[bank].sourceBpm[pad].store(120.0f);
            bankControls[bank].warpAnchor[pad].store(0.0f);
            bankControls[bank].steps[pad].store(0);
        }

        auto emptyBank = std::make_unique<vsl::Bank>();
        bankSnapshots[bank].store(emptyBank.get(), std::memory_order_relaxed);
        retainedBanks.push_back(std::move(emptyBank));
    }

    gainParameter = parameters.getRawParameterValue("gain");
    pitchParameter = parameters.getRawParameterValue("pitch");
    attackParameter = parameters.getRawParameterValue("attack");
    modeParameter = parameters.getRawParameterValue("mode");
    keyboardPadParameter = parameters.getRawParameterValue("keyboardPad");
    rootNoteParameter = parameters.getRawParameterValue("rootNote");
    lengthParameter = parameters.getRawParameterValue("length");
    sampleStartParameter = parameters.getRawParameterValue("sampleStart");
    sampleEndParameter = parameters.getRawParameterValue("sampleEnd");
    loopParameter = parameters.getRawParameterValue("loop");
    gateParameter = parameters.getRawParameterValue("gate");
    releaseParameter = parameters.getRawParameterValue("release");

    publishedBank.store(bankSnapshots[0].load(std::memory_order_relaxed), std::memory_order_release);

    // Explicit opt-in for launching an isolated standalone study session.
    // The preview launcher supplies this env var for ONE new process only.
    // Hosts and ordinary launches retain their existing saved state unless
    // the caller explicitly opts in. Do not decode audio on the audio thread.
    const auto startup = juce::SystemStats::getEnvironmentVariable("VSL_STARTUP_PROJECT", {});
    if (startup.isNotEmpty())
    {
        const juce::File project(startup);
        if (juce::File::isAbsolutePath(startup)
            && project.hasFileExtension("vsl") && project.existsAsFile()
            && project.getSize() > 16 && project.getSize() <= 2 * 1024 * 1024)
        {
            // The JUCE standalone holder restores shared preferences *after*
            // constructing its processor. Defer the opt-in preset until the
            // message loop starts so that older Barbara preferences cannot
            // overwrite the chosen Emulations project.
            startupProjectPath = startup;
            startTimer(250);
        }
        else
        {
            const juce::ScopedLock guard(stateLock);
            lastError = "Startup .vsl project missing or invalid; choose PROJECT > OPEN.";
        }
    }
}

VintageSamplerProcessor::~VintageSamplerProcessor()
{
    stopTimer();
    cancelPendingUpdate();
    engine.allNotesOff();
    publishedBank.store(nullptr, std::memory_order_release);
}

juce::AudioProcessorValueTreeState::ParameterLayout VintageSamplerProcessor::makeParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"gain", 1}, "Master Gain",
        juce::NormalisableRange<float>{0.0f, 1.5f, 0.005f}, 0.82f));
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"pitch", 1}, "Global Pitch",
        juce::NormalisableRange<float>{-24.0f, 24.0f, 1.0f}, 0.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1}, "Attack (ms)",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.5f}, 3.0f));
    result.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"mode", 1}, "Trigger Mode",
        juce::StringArray{"16 Pads", "Keyboard"}, 0));
    result.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"keyboardPad", 1}, "Keyboard Sample Pad", 0, 15, 0));
    result.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{"rootNote", 1}, "Keyboard Root Note", 36, 96, 60));
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"length", 1}, "Maximum Playback Length (seconds)",
        juce::NormalisableRange<float>{0.1f, 32.0f, 0.1f, 0.6f}, 16.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sampleStart", 1}, "Sample Start (%)",
        juce::NormalisableRange<float>{0.0f, 99.0f, 0.5f}, 0.0f));
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"sampleEnd", 1}, "Sample End (%)",
        juce::NormalisableRange<float>{1.0f, 100.0f, 0.5f}, 100.0f));
    result.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"loop", 1}, "Loop Playback", juce::StringArray{"Off", "On"}, 0));
    result.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"gate", 1}, "Playback Gate", juce::StringArray{"One-shot", "Gated"}, 0));
    result.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"release", 1}, "Release (ms)",
        juce::NormalisableRange<float>{5.0f, 2000.0f, 5.0f, 0.55f}, 120.0f));
    return result;
}

void VintageSamplerProcessor::prepareToPlay(double sampleRate, int)
{
    engine.setOutputRate(sampleRate);
    engine.allNotesOff();
    for (auto& slot : fxSlots)
    {
        slot.dsp.prepare(sampleRate);
        slot.lastRenderedType = 0;
    }
    sequenceReset.store(true, std::memory_order_release);
}

void VintageSamplerProcessor::setFxSlotType(int slot, int type) noexcept
{
    if (juce::isPositiveAndBelow(slot, numberOfFxSlots))
        fxSlots[slot].type.store(juce::jlimit(0, 4, type), std::memory_order_release);
}

int VintageSamplerProcessor::getFxSlotType(int slot) const noexcept
{
    return juce::isPositiveAndBelow(slot, numberOfFxSlots)
        ? fxSlots[slot].type.load(std::memory_order_acquire) : 0;
}

void VintageSamplerProcessor::setFxSlotAmount(int slot, float amount) noexcept
{
    if (juce::isPositiveAndBelow(slot, numberOfFxSlots) && std::isfinite(amount))
        fxSlots[slot].amount.store(juce::jlimit(0.0f, 1.0f, amount), std::memory_order_release);
}

float VintageSamplerProcessor::getFxSlotAmount(int slot) const noexcept
{
    return juce::isPositiveAndBelow(slot, numberOfFxSlots)
        ? fxSlots[slot].amount.load(std::memory_order_acquire) : 0.5f;
}

void VintageSamplerProcessor::setFxSlotWet(int slot, float wet) noexcept
{
    if (juce::isPositiveAndBelow(slot, numberOfFxSlots) && std::isfinite(wet))
        fxSlots[slot].wet.store(juce::jlimit(0.0f, 1.0f, wet), std::memory_order_release);
}

float VintageSamplerProcessor::getFxSlotWet(int slot) const noexcept
{
    return juce::isPositiveAndBelow(slot, numberOfFxSlots)
        ? fxSlots[slot].wet.load(std::memory_order_acquire) : 1.0f;
}

bool VintageSamplerProcessor::isPadMuted(int pad) const noexcept
{
    return isPadMutedInBank(getSelectedBank(), pad);
}

bool VintageSamplerProcessor::isPadMutedInBank(int bank, int pad) const noexcept
{
    return juce::isPositiveAndBelow(bank, numberOfBanks) && juce::isPositiveAndBelow(pad, 16)
        && bankControls[bank].muted[pad].load(std::memory_order_relaxed);
}

void VintageSamplerProcessor::setPadMuted(int pad, bool value) noexcept
{
    if (juce::isPositiveAndBelow(pad, 16)) bankControls[getSelectedBank()].muted[pad].store(value, std::memory_order_relaxed);
}

void VintageSamplerProcessor::unmuteAllPads() noexcept
{
    for (auto& muted : bankControls[getSelectedBank()].muted) muted.store(false, std::memory_order_relaxed);
}

std::pair<float, float> VintageSamplerProcessor::getPadChop(int pad) const noexcept
{
    if (!juce::isPositiveAndBelow(pad, 16)) return {0.0f, 100.0f};
    const auto& controls = bankControls[getSelectedBank()];
    return {controls.start[pad].load(std::memory_order_relaxed), controls.end[pad].load(std::memory_order_relaxed)};
}

void VintageSamplerProcessor::setPadChop(int pad, float start, float end) noexcept
{
    if (!juce::isPositiveAndBelow(pad, 16) || !std::isfinite(start) || !std::isfinite(end)) return;
    start = juce::jlimit(0.0f, 99.9f, start);
    end = juce::jlimit(start + 0.1f, 100.0f, end);
    // Two atomics are independently visible; optionsForPad repairs an intermediate pair.
    auto& controls = bankControls[getSelectedBank()];
    controls.end[pad].store(end, std::memory_order_relaxed);
    controls.start[pad].store(start, std::memory_order_relaxed);
}

void VintageSamplerProcessor::setTempoSyncEnabled(bool enabled) noexcept
{
    tempoSyncEnabled.store(enabled, std::memory_order_release);
}

bool VintageSamplerProcessor::isTempoSyncEnabled() const noexcept
{
    return tempoSyncEnabled.load(std::memory_order_acquire);
}

void VintageSamplerProcessor::setPadSourceBpm(int pad, float bpm) noexcept
{
    if (!juce::isPositiveAndBelow(pad, 16) || !std::isfinite(bpm)) return;
    bankControls[getSelectedBank()].sourceBpm[pad].store(
        juce::jlimit(30.0f, 300.0f, bpm), std::memory_order_relaxed);
}

float VintageSamplerProcessor::getPadSourceBpm(int pad) const noexcept
{
    return juce::isPositiveAndBelow(pad, 16)
        ? bankControls[getSelectedBank()].sourceBpm[pad].load(std::memory_order_relaxed)
        : 120.0f;
}

void VintageSamplerProcessor::setPadWarpAnchor(int pad, float percent) noexcept
{
    if (!juce::isPositiveAndBelow(pad, 16) || !std::isfinite(percent)) return;
    bankControls[getSelectedBank()].warpAnchor[pad].store(
        juce::jlimit(0.0f, 100.0f, percent), std::memory_order_relaxed);
}

float VintageSamplerProcessor::getPadWarpAnchor(int pad) const noexcept
{
    return juce::isPositiveAndBelow(pad, 16)
        ? bankControls[getSelectedBank()].warpAnchor[pad].load(std::memory_order_relaxed)
        : 0.0f;
}

vsl::PlaybackOptions VintageSamplerProcessor::optionsForPad(int bank, int pad, const vsl::PlaybackOptions& global) const noexcept
{
    auto result = global;
    const auto& controls = bankControls[bank];
    const std::pair<float,float> chop{controls.start[pad].load(std::memory_order_relaxed),
                                      controls.end[pad].load(std::memory_order_relaxed)};
    const float globalStart = juce::jlimit(0.0f, 99.0f, global.sampleStartPercent);
    const float globalEnd = juce::jlimit(globalStart + 0.01f, 100.0f, global.sampleEndPercent);
    const float start = juce::jlimit(0.0f, 99.9f, chop.first);
    const float end = juce::jlimit(start + 0.1f, 100.0f, chop.second);
    result.sampleStartPercent = globalStart + (globalEnd - globalStart) * start * 0.01f;
    result.sampleEndPercent = globalStart + (globalEnd - globalStart) * end * 0.01f;
    result.tempoSync = isTempoSyncEnabled();
    result.sourceBpm = controls.sourceBpm[pad].load(std::memory_order_relaxed);
    result.warpAnchorPercent = controls.warpAnchor[pad].load(std::memory_order_relaxed);
    return result;
}

void VintageSamplerProcessor::selectBank(int bank) noexcept
{
    if (!juce::isPositiveAndBelow(bank, numberOfBanks)) return;
    activeBank.store(bank, std::memory_order_release);
    publishedBank.store(bankSnapshots[bank].load(std::memory_order_acquire), std::memory_order_release);
    for (auto& flag : uiReleaseFlags) flag.store(false, std::memory_order_relaxed);
    for (auto& held : padHeld) held.store(false, std::memory_order_relaxed);
    sequenceReset.store(true, std::memory_order_release);
}

bool VintageSamplerProcessor::chopPadTo16(int sourcePad)
{
    if (!juce::isPositiveAndBelow(sourcePad, 16) || retainedBanks.size() >= maximumBankRevisions) return false;
    const int bankIndex = getSelectedBank();
    const auto* bank = bankSnapshots[bankIndex].load(std::memory_order_acquire);
    if (bank == nullptr || !bank->pads[sourcePad] || !bank->pads[sourcePad]->valid()) return false;
    auto next = std::make_unique<vsl::Bank>(*bank);
    const auto sample = bank->pads[sourcePad];
    const auto sourceChop = getPadChop(sourcePad);
    const float width = (sourceChop.second - sourceChop.first) / 16.0f;
    juce::String path, name;
    {
        const juce::ScopedLock guard(stateLock);
        path = samplePaths[bankIndex][sourcePad];
        name = sampleDisplayNames[bankIndex][sourcePad];
        for (int i = 0; i < 16; ++i)
        {
            samplePaths[bankIndex][i] = path;
            sampleDisplayNames[bankIndex][i] = name + " / " + juce::String(i + 1);
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        next->pads[i] = sample; // immutable shared PCM, no 16x audio copy
        bankControls[bankIndex].start[i].store(sourceChop.first + width * i, std::memory_order_relaxed);
        bankControls[bankIndex].end[i].store(sourceChop.first + width * (i + 1), std::memory_order_relaxed);
    }
    const auto* pointer = next.get();
    retainedBanks.push_back(std::move(next));
    bankSnapshots[bankIndex].store(pointer, std::memory_order_release);
    if (getSelectedBank() == bankIndex) publishedBank.store(pointer, std::memory_order_release);
    updateHostDisplay();
    return true;
}

void VintageSamplerProcessor::setTempo(float bpm) noexcept
{
    if (std::isfinite(bpm)) tempo.store(juce::jlimit(30.0f, 300.0f, bpm), std::memory_order_relaxed);
}

float VintageSamplerProcessor::getTempo() const noexcept { return tempo.load(std::memory_order_relaxed); }
void VintageSamplerProcessor::startSequence(bool recording) noexcept
{
    sequenceRecording.store(recording, std::memory_order_release);
    sequenceReset.store(true, std::memory_order_release);
    sequencePlaying.store(true, std::memory_order_release);
}
void VintageSamplerProcessor::stopSequence() noexcept
{
    sequencePlaying.store(false, std::memory_order_release);
    sequenceRecording.store(false, std::memory_order_release);
}
bool VintageSamplerProcessor::isSequencePlaying() const noexcept { return sequencePlaying.load(std::memory_order_acquire); }
bool VintageSamplerProcessor::isSequenceRecording() const noexcept { return isSequencePlaying() && sequenceRecording.load(std::memory_order_acquire); }
void VintageSamplerProcessor::clearSequence() noexcept
{
    for (auto& mask : bankControls[getSelectedBank()].steps) mask.store(0, std::memory_order_relaxed);
}
void VintageSamplerProcessor::setStepEnabled(int pad, int step, bool enabled) noexcept
{
    if (!juce::isPositiveAndBelow(pad, 16) || !juce::isPositiveAndBelow(step, 16)) return;
    const auto bit = std::uint32_t{1} << step;
    auto& mask = bankControls[getSelectedBank()].steps[pad];
    if (enabled) mask.fetch_or(bit, std::memory_order_relaxed);
    else mask.fetch_and(~bit, std::memory_order_relaxed);
}
bool VintageSamplerProcessor::isStepEnabled(int pad, int step) const noexcept
{
    return isStepEnabledInBank(getSelectedBank(), pad, step);
}

bool VintageSamplerProcessor::isStepEnabledInBank(int bank, int pad, int step) const noexcept
{
    return juce::isPositiveAndBelow(bank, numberOfBanks) && juce::isPositiveAndBelow(pad, 16)
        && juce::isPositiveAndBelow(step, 16)
        && (bankControls[bank].steps[pad].load(std::memory_order_relaxed) & (std::uint32_t{1} << step)) != 0;
}

void VintageSamplerProcessor::setNoteRepeatEnabled(bool enabled) noexcept
{
    noteRepeatEnabled.store(enabled, std::memory_order_release);
}

bool VintageSamplerProcessor::isNoteRepeatEnabled() const noexcept
{
    return noteRepeatEnabled.load(std::memory_order_acquire);
}

void VintageSamplerProcessor::setPadHeld(int pad, bool held) noexcept
{
    if (juce::isPositiveAndBelow(pad, 16))
        padHeld[pad].store(held, std::memory_order_release);
}

void VintageSamplerProcessor::releaseResources()
{
    engine.allNotesOff();
    repeatWasActive = false;
    repeatSamplesUntilHit = 0.0;
}

bool VintageSamplerProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void VintageSamplerProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;

    const int renderingBank = getSelectedBank();
    if (renderingBank != lastRenderedBank)
    {
        if (lastRenderedBank >= 0) engine.allNotesOff();
        lastRenderedBank = renderingBank;
    }
    engine.setBank(bankSnapshots[renderingBank].load(std::memory_order_acquire));
    engine.setTempoBpm(getTempo());

    const auto gain = gainParameter != nullptr ? gainParameter->load(std::memory_order_relaxed) : 0.82f;
    const auto pitch = pitchParameter != nullptr ? pitchParameter->load(std::memory_order_relaxed) : 0.0f;
    const auto attackSeconds = (attackParameter != nullptr ?
        attackParameter->load(std::memory_order_relaxed) : 3.0f) * 0.001f;
    const bool keyboardMode = modeParameter != nullptr && modeParameter->load(std::memory_order_relaxed) > 0.5f;
    const auto keyboardPad = keyboardPadParameter != nullptr ?
        static_cast<int>(std::round(keyboardPadParameter->load(std::memory_order_relaxed))) : 0;
    const auto rootNote = rootNoteParameter != nullptr ?
        static_cast<int>(std::round(rootNoteParameter->load(std::memory_order_relaxed))) : 60;
    vsl::PlaybackOptions options;
    options.maxLengthSeconds = lengthParameter ? lengthParameter->load(std::memory_order_relaxed) : 16.0f;
    options.sampleStartPercent = sampleStartParameter ? sampleStartParameter->load(std::memory_order_relaxed) : 0.0f;
    options.sampleEndPercent = sampleEndParameter ? sampleEndParameter->load(std::memory_order_relaxed) : 100.0f;
    options.loop = loopParameter != nullptr && loopParameter->load(std::memory_order_relaxed) > 0.5f;
    options.gated = gateParameter != nullptr && gateParameter->load(std::memory_order_relaxed) > 0.5f;
    options.chokePrevious = true; // MPC/ASR-style exclusive chops; no overlapping pad samples
    options.releaseSeconds = (releaseParameter ? releaseParameter->load(std::memory_order_relaxed) : 120.0f) * 0.001f;

    // Onscreen pad presses are one-shot atomic triggers. The audio callback is
    // the sole writer to the actual voice pool; the editor never touches it.
    if (sequenceReset.exchange(false, std::memory_order_acq_rel))
    {
        sequenceSamplesUntilStep = 0.0;
        sequenceNextStep = 0;
        sequenceCurrentStep.store(0, std::memory_order_relaxed);
    }
    // Consume UI hits in their actual press order. The previous per-pad flags
    // coalesced double taps within one 256-frame buffer and could make pad 16
    // win even if pad 1 was pressed last.
    std::uint32_t next = uiTriggerRead.load(std::memory_order_relaxed);
    const auto available = uiTriggerWrite.load(std::memory_order_acquire);
    while (next != available)
    {
        const auto hit = uiTriggerQueue[next % uiQueueCapacity];
        ++next;
        if (isSequenceRecording())
            bankControls[renderingBank].steps[hit.pad].fetch_or(
                std::uint32_t{1} << sequenceCurrentStep.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
        if (!isPadMutedInBank(renderingBank, hit.pad))
            engine.noteOn(36 + hit.pad, hit.velocity, pitch, false, keyboardPad, rootNote,
                          optionsForPad(renderingBank, hit.pad, options));
    }
    uiTriggerRead.store(next, std::memory_order_release);
    for (int index = 0; index < 16; ++index)
        if (uiReleaseFlags[static_cast<size_t>(index)].exchange(false, std::memory_order_acq_rel))
            engine.releasePad(index);

    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getWritePointer(1);
    const auto total = buffer.getNumSamples();
    int rendered = 0;

    // Render up to every sixteenth-note boundary with sample-accurate timing.
    // The grid and transport are atomic; no heap, lock or file work occurs here.
    const auto renderUntil = [&](int target)
    {
        while (rendered < target)
        {
            const bool sequenceActive = sequencePlaying.load(std::memory_order_acquire);
            const bool repeatEnabled = isNoteRepeatEnabled();
            bool anyHeld = false;
            if (repeatEnabled)
                for (const auto& held : padHeld)
                    anyHeld = held.load(std::memory_order_acquire) || anyHeld;
            const bool repeatActive = repeatEnabled && anyHeld;
            if (!repeatActive)
            {
                repeatWasActive = false;
                repeatSamplesUntilHit = 0.0;
            }
            else if (!repeatWasActive)
            {
                repeatWasActive = true;
                repeatSamplesUntilHit = 0.0;
            }

            if (!sequenceActive && !repeatActive)
            {
                engine.render(left + rendered, right + rendered, target - rendered, gain, attackSeconds);
                rendered = target;
                break;
            }
            const double sampleRate = getSampleRate() > 0 ? getSampleRate() : 44100.0;
            const double sixteenthSamples = sampleRate * 15.0 / static_cast<double>(getTempo());
            if (sequenceActive && sequenceSamplesUntilStep <= 0.0)
            {
                const int step = sequenceNextStep;
                sequenceCurrentStep.store(step, std::memory_order_relaxed);
                for (int pad = 0; pad < 16; ++pad)
                    if (isStepEnabledInBank(renderingBank, pad, step) && !isPadMutedInBank(renderingBank, pad))
                        engine.noteOn(36 + pad, 1.0f, pitch, false, keyboardPad, rootNote, optionsForPad(renderingBank, pad, options));
                sequenceNextStep = (step + 1) % 16;
                sequenceSamplesUntilStep += sixteenthSamples;
            }
            if (repeatActive && repeatSamplesUntilHit <= 0.0)
            {
                for (int pad = 0; pad < 16; ++pad)
                    if (padHeld[pad].load(std::memory_order_acquire) && !isPadMutedInBank(renderingBank, pad))
                        engine.noteOn(36 + pad, 1.0f, pitch, false, keyboardPad, rootNote,
                                      optionsForPad(renderingBank, pad, options));
                repeatSamplesUntilHit += sixteenthSamples;
            }
            // Stop rendering at the earliest sequencer or repeat boundary;
            // the next voice starts at precisely that audio sample offset.
            double framesToBoundary = static_cast<double>(target - rendered);
            if (sequenceActive) framesToBoundary = std::min(framesToBoundary, sequenceSamplesUntilStep);
            if (repeatActive) framesToBoundary = std::min(framesToBoundary, repeatSamplesUntilHit);
            const int count = std::min(target - rendered,
                                       std::max(1, static_cast<int>(std::ceil(framesToBoundary))));
            engine.render(left + rendered, right + rendered, count, gain, attackSeconds);
            rendered += count;
            if (sequenceActive) sequenceSamplesUntilStep -= count;
            if (repeatActive) repeatSamplesUntilHit -= count;
        }
    };

    for (const auto event : midi)
    {
        const auto offset = juce::jlimit(0, total, event.samplePosition);
        if (offset > rendered)
        {
            renderUntil(offset);
        }

        const auto message = event.getMessage();
        if (message.isNoteOn())
        {
            const int pad = keyboardMode ? keyboardPad : message.getNoteNumber() - 36;
            if (juce::isPositiveAndBelow(pad, 16))
            {
                if (isSequenceRecording())
                    bankControls[renderingBank].steps[pad].fetch_or(
                        std::uint32_t{1} << sequenceCurrentStep.load(std::memory_order_relaxed),
                        std::memory_order_relaxed);
                if (!isPadMutedInBank(renderingBank, pad))
                    engine.noteOn(message.getNoteNumber(), message.getFloatVelocity(),
                                  pitch, keyboardMode, keyboardPad, rootNote, optionsForPad(renderingBank, pad, options));
            }
        }
        else if (message.isNoteOff())
        {
            engine.noteOff(message.getNoteNumber(), keyboardMode, keyboardPad);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            engine.allNotesOff();
        }
        // In One-shot mode MIDI note-off is ignored. In Gated mode only the
        // corresponding active voice receives the smooth configured release.
    }

    if (rendered < total)
        renderUntil(total);

    // Master insert chain: render all MIDI/sequencer slices first, then run
    // four persistent stereo DSP states in order. Atomics avoid GUI locks.
    for (auto& slot : fxSlots)
    {
        const int type = slot.type.load(std::memory_order_acquire);
        if (type != slot.lastRenderedType)
        {
            slot.dsp.reset();
            slot.lastRenderedType = type;
        }
        if (type != 0)
            slot.dsp.process(left, right, total, type,
                             slot.amount.load(std::memory_order_relaxed),
                             slot.wet.load(std::memory_order_relaxed));
    }
}

void VintageSamplerProcessor::publishPad(int bank, int pad, std::shared_ptr<const vsl::Sample> sample)
{
    if (!juce::isPositiveAndBelow(bank, numberOfBanks) || !juce::isPositiveAndBelow(pad, 16))
        return;

    // This method may be invoked ONLY outside the real-time audio callback.
    // Revisions keep clips alive until the processor is destroyed, even when
    // a voice is still playing an older clip. The hard revision cap avoids
    // unbounded session RAM use until a later lock-free reclamation release.
    if (retainedBanks.size() >= maximumBankRevisions)
    {
        const juce::ScopedLock guard(stateLock);
        lastError = "Session sample-reload limit reached. Save and reopen the project to reset memory.";
        return;
    }

    auto next = std::make_unique<vsl::Bank>(*bankSnapshots[bank].load(std::memory_order_acquire));
    next->pads[static_cast<size_t>(pad)] = std::move(sample);
    auto* pointer = next.get();
    retainedBanks.push_back(std::move(next));
    bankSnapshots[bank].store(pointer, std::memory_order_release);
    if (getSelectedBank() == bank)
        publishedBank.store(pointer, std::memory_order_release);
}

std::shared_ptr<const vsl::Sample> VintageSamplerProcessor::decodeSample(const juce::File& file)
{
    if (!file.existsAsFile()) return {};
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (! reader)
    {
        const juce::ScopedLock guard(stateLock);
        lastError = "Unsupported format: please use a WAV or AIFF file.";
        return {};
    }

    constexpr int64_t maxSamples = 2'800'000;
    const auto requested = std::min(static_cast<int64_t>(reader->lengthInSamples), maxSamples);
    if (requested < 2 || ! std::isfinite(reader->sampleRate) || reader->sampleRate <= 0)
        return {};

    const auto count = static_cast<int>(requested);
    juce::AudioBuffer<float> decoded(2, count);
    if (! reader->read(&decoded, 0, count, 0, true, true))
    {
        const juce::ScopedLock guard(stateLock);
        lastError = "Unable to decode the selected audio.";
        return {};
    }

    auto sound = std::make_shared<vsl::Sample>();
    sound->sampleRate = reader->sampleRate;
    const auto* sourceLeft = decoded.getReadPointer(0);
    const auto* sourceRight = decoded.getReadPointer(reader->numChannels > 1 ? 1 : 0);
    sound->left.assign(sourceLeft, sourceLeft + count);
    sound->right.assign(sourceRight, sourceRight + count);
    for (int bin = 0; bin < 256; ++bin)
    {
        const int begin = static_cast<int>((static_cast<int64_t>(bin) * count) / 256);
        const int end = static_cast<int>((static_cast<int64_t>(bin + 1) * count) / 256);
        float peak = 0.0f;
        for (int frame = begin; frame < end; ++frame)
            peak = std::max(peak, std::max(std::abs(sourceLeft[frame]), std::abs(sourceRight[frame])));
        sound->waveformPeaks[bin] = juce::jlimit(0.0f, 1.0f, peak);
    }
    if (! sound->valid())
        return {};

    return sound;
}

bool VintageSamplerProcessor::loadSampleForPad(int pad, const juce::File& file)
{
    if (!juce::isPositiveAndBelow(pad, 16) || !file.existsAsFile()) return false;
    const int bankIndex = getSelectedBank();
    if (retainedBanks.size() >= maximumBankRevisions)
    {
        const juce::ScopedLock guard(stateLock);
        lastError = "Sample-reload limit reached; save and reopen this project.";
        return false;
    }
    // A reused source path shares the original decoded PCM across A-H.
    std::shared_ptr<const vsl::Sample> sound;
    const auto path = file.getFullPathName();
    {
        const juce::ScopedLock guard(stateLock);
        for (int bank = 0; bank < numberOfBanks && !sound; ++bank)
        {
            const auto* snapshot = bankSnapshots[bank].load(std::memory_order_acquire);
            for (int other = 0; other < 16 && !sound; ++other)
                if (samplePaths[bank][other] == path && snapshot != nullptr)
                    sound = snapshot->pads[other];
        }
    }
    if (!sound) sound = decodeSample(file);
    if (!sound) return false;

    publishPad(bankIndex, pad, std::move(sound));

    {
        const juce::ScopedLock guard(stateLock);
        samplePaths[bankIndex][pad] = path;
        sampleDisplayNames[bankIndex][pad] = file.getFileNameWithoutExtension();
        lastError.clear();
    }
    updateHostDisplay();
    return true;
}

bool VintageSamplerProcessor::loadKitDirectory(const juce::File& dir)
{
    if (! dir.isDirectory())
        return false;
    // A Music REA export directory contains banks.json and A-H subfolders.
    // Detect the parent without requiring the user to enter each bank.
    if (dir.getChildFile("banks.json").existsAsFile())
        return loadMultiBankDirectory(dir);

    // The audio files deliberately have stable pad_01.wav names. Read the
    // adjacent optional catalog for the *actual* instrument/voice phrase,
    // rather than showing generic PAD_01 to someone using the editor.
    std::array<juce::String, 16> catalogNames;
    auto manifest = dir.getChildFile("kit-manifest.json");
    const bool isVocalBank = !manifest.existsAsFile();
    if (isVocalBank)
        manifest = dir.getChildFile("CHOP_CANDIDATES.json");
    if (manifest.existsAsFile() && manifest.getSize() < 1024 * 1024)
    {
        const juce::var document = juce::JSON::parse(manifest);
        const juce::var items = document.getProperty("samples", {});
        if (auto* list = items.getArray())
        {
            for (const auto& record : *list)
            {
                const int number = static_cast<int>(record.getProperty("pad", 0));
                if (!juce::isPositiveAndBelow(number - 1, 16))
                    continue;
                juce::String label = record.getProperty("name", {}).toString().trim();
                if (label.isNotEmpty())
                    label = juce::File(label).getFileNameWithoutExtension();
                if (isVocalBank)
                {
                    const auto seconds = static_cast<double>(record.getProperty("source_time_start_s", 0));
                    label = "VOCAL " + juce::String(seconds, 2) + "s";
                }
                catalogNames[static_cast<size_t>(number - 1)] = label.substring(0, 80);
            }
        }
    }

    // Loading is a UI/message-thread action and happens outside processBlock.
    const int bankIndex = getSelectedBank();
    bool allGood = true;
    for (int pad = 0; pad < 16; ++pad)
    {
        const auto file = dir.getChildFile("pad_" + juce::String(pad + 1).paddedLeft('0', 2) + ".wav");
        const bool loaded = loadSampleForPad(pad, file);
        if (loaded && catalogNames[static_cast<size_t>(pad)].isNotEmpty())
        {
            const juce::ScopedLock guard(stateLock);
            sampleDisplayNames[bankIndex][pad] = catalogNames[static_cast<size_t>(pad)];
        }
        allGood = loaded && allGood;
    }
    return allGood;
}

bool VintageSamplerProcessor::loadMultiBankDirectory(const juce::File& directory)
{
    const auto fail = [this](const juce::String& message)
    {
        const juce::ScopedLock guard(stateLock);
        lastError = message;
        return false;
    };
    const auto indexFile = directory.getChildFile("banks.json");
    if (!directory.isDirectory() || !indexFile.existsAsFile()
        || indexFile.getSize() > 1024 * 1024)
        return fail("Choose a Music REA A-H bank root containing banks.json.");

    const auto manifest = juce::JSON::parse(indexFile);
    if (!manifest.isObject()
        || manifest.getProperty("format", {}).toString() != "music-rea-multi-bank-kit-v1")
        return fail("Music REA bank manifest format not recognized.");
    const int count = static_cast<int>(manifest.getProperty("bank_count", 0));
    if (!juce::isPositiveAndBelow(count - 1, numberOfBanks))
        return fail("Bank root must contain 1 to 8 banks.");
    if (retainedBanks.size() + static_cast<std::size_t>(count) >= maximumBankRevisions)
        return fail("Session has reached its bank revision limit; save and reopen.");

    std::array<std::unique_ptr<vsl::Bank>, numberOfBanks> staged;
    std::array<std::array<juce::String, 16>, numberOfBanks> stagedPaths;
    std::array<std::array<juce::String, 16>, numberOfBanks> stagedNames;
    std::array<std::array<float, 16>, numberOfBanks> stagedBpm {};
    std::map<juce::String, std::shared_ptr<const vsl::Sample>> reused;
    std::int64_t totalBytes = 0;

    const auto ranking = manifest.getProperty("ranking", {});
    double detectedBpm = static_cast<double>(ranking.getProperty("tempo_bpm", 120.0));
    if (!std::isfinite(detectedBpm) || detectedBpm < 30.0 || detectedBpm > 300.0)
        detectedBpm = 120.0;

    // Validate every sample before publishing any bank. Failures keep all
    // existing loaded banks and their sounds intact.
    for (int bank = 0; bank < count; ++bank)
    {
        const auto folder = directory.getChildFile(
            juce::String::charToString(static_cast<juce_wchar>('A' + bank)));
        if (!folder.isDirectory())
            return fail("Missing bank folder " + folder.getFileName());
        const auto kit = folder.getChildFile("kit.json");
        if (!kit.existsAsFile() || kit.getSize() > 1024 * 1024)
            return fail("Missing or oversized bank kit.json in " + folder.getFileName());
        const auto kitDoc = juce::JSON::parse(kit);
        const auto padEntries = kitDoc.getProperty("pads", {});
        const auto* slots = padEntries.getArray();
        if (slots == nullptr || slots->size() != 16)
            return fail("Expected exactly 16 pad descriptions in " + folder.getFileName());
        auto next = std::make_unique<vsl::Bank>();
        for (int pad = 0; pad < 16; ++pad)
        {
            const auto filename = "pad_" + juce::String(pad + 1).paddedLeft('0', 2) + ".wav";
            const auto waveFile = folder.getChildFile(filename);
            if (!waveFile.existsAsFile())
                return fail("Missing " + folder.getFileName() + "/" + filename);
            const auto bytes = waveFile.getSize();
            if (bytes <= 44 || bytes > 80 * 1024 * 1024)
                return fail("Invalid or excessively large sample: " + filename);
            totalBytes += bytes;
            if (totalBytes > 512LL * 1024LL * 1024LL)
                return fail("Bank import exceeds 512 MB; choose shorter chops.");
            const auto path = waveFile.getFullPathName();
            auto existing = reused.find(path);
            if (existing == reused.end())
            {
                auto decoded = decodeSample(waveFile);
                if (!decoded)
                    return fail("Unable to decode " + folder.getFileName() + "/" + filename);
                existing = reused.emplace(path, std::move(decoded)).first;
            }
            next->pads[static_cast<std::size_t>(pad)] = existing->second;
            stagedPaths[bank][pad] = path;
            const auto entry = (*slots)[pad];
            const double start = static_cast<double>(entry.getProperty("start_seconds", -1));
            const auto tags = entry.getProperty("tags", {});
            juce::String label = "CHOP " + juce::String(bank * 16 + pad + 1);
            if (std::isfinite(start) && start >= 0)
                label += " / " + juce::String(start, 2) + "s";
            if (auto* labels = tags.getArray())
                if (!labels->isEmpty())
                    label += " / " + labels->getFirst().toString();
            stagedNames[bank][pad] = label.substring(0, 80);
            stagedBpm[bank][pad] = static_cast<float>(detectedBpm);
        }
        staged[bank] = std::move(next);
    }

    // Publish a complete, immutable bank in one atomic pointer store. Active
    // playing voices retain their original PCM through retainedBanks.
    for (int bank = 0; bank < count; ++bank)
    {
        const auto* newBank = staged[bank].get();
        retainedBanks.push_back(std::move(staged[bank]));
        {
            const juce::ScopedLock guard(stateLock);
            samplePaths[bank] = std::move(stagedPaths[bank]);
            sampleDisplayNames[bank] = std::move(stagedNames[bank]);
            for (int pad = 0; pad < 16; ++pad)
            {
                bankControls[bank].muted[pad].store(false, std::memory_order_relaxed);
                bankControls[bank].start[pad].store(0.0f, std::memory_order_relaxed);
                bankControls[bank].end[pad].store(100.0f, std::memory_order_relaxed);
                bankControls[bank].warpAnchor[pad].store(0.0f, std::memory_order_relaxed);
                bankControls[bank].sourceBpm[pad].store(stagedBpm[bank][pad],
                                                       std::memory_order_relaxed);
            }
        }
        bankSnapshots[bank].store(newBank, std::memory_order_release);
        if (getSelectedBank() == bank)
            publishedBank.store(newBank, std::memory_order_release);
    }
    {
        const juce::ScopedLock guard(stateLock);
        lastError.clear();
    }
    updateHostDisplay();
    return true;
}

juce::String VintageSamplerProcessor::getPadName(int pad) const
{
    if (! juce::isPositiveAndBelow(pad, 16))
        return {};
    const juce::ScopedLock guard(stateLock);
    const int bank = getSelectedBank();
    const auto& fullPath = samplePaths[bank][pad];
    const auto& label = sampleDisplayNames[bank][pad];
    return fullPath.isEmpty() ? "Empty" :
        (label.isNotEmpty() ? label : juce::File(fullPath).getFileNameWithoutExtension());
}

juce::String VintageSamplerProcessor::getLastError() const
{
    const juce::ScopedLock guard(stateLock);
    return lastError;
}

void VintageSamplerProcessor::triggerPad(int pad) noexcept
{
    triggerPadVelocity(pad, 1.0f);
}

void VintageSamplerProcessor::triggerPadVelocity(int pad, float velocity) noexcept
{
    if (juce::isPositiveAndBelow(pad, 16))
    {
        const auto write = uiTriggerWrite.load(std::memory_order_relaxed);
        const auto read = uiTriggerRead.load(std::memory_order_acquire);
        if (write - read >= uiQueueCapacity) return; // bounded, never block audio
        uiTriggerQueue[write % uiQueueCapacity] = {
            pad, std::isfinite(velocity) ? juce::jlimit(0.01f, 1.0f, velocity) : 1.0f
        };
        uiTriggerWrite.store(write + 1, std::memory_order_release);
    }
}

std::array<float, 256> VintageSamplerProcessor::getPadWaveformPeaks(int pad) const noexcept
{
    if (juce::isPositiveAndBelow(pad, 16))
    {
        const auto* bank = bankSnapshots[getSelectedBank()].load(std::memory_order_acquire);
        if (bank && bank->pads[pad]) return bank->pads[pad]->waveformPeaks;
    }
    return {};
}

void VintageSamplerProcessor::releasePad(int pad) noexcept
{
    if (juce::isPositiveAndBelow(pad, 16))
        uiReleaseFlags[static_cast<size_t>(pad)].store(true, std::memory_order_release);
}

float VintageSamplerProcessor::getPadPlaybackProgress(int pad) const noexcept
{
    return engine.getPadPlaybackProgress(pad);
}

bool VintageSamplerProcessor::isPadPlaying(int pad) const noexcept
{
    return engine.isPadPlaying(pad);
}

void VintageSamplerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree root("VINTAGE_SAMPLER_STATE");
    root.addChild(parameters.copyState(), -1, nullptr);
    root.setProperty("activeBank", getSelectedBank(), nullptr);
    juce::ValueTree banks("BANKS");
    {
        const juce::ScopedLock guard(stateLock);
        for (int bank = 0; bank < numberOfBanks; ++bank)
        {
            juce::ValueTree slot("BANK");
            slot.setProperty("index", bank, nullptr);
            for (int index = 0; index < 16; ++index)
            {
                juce::ValueTree pad("PAD");
                pad.setProperty("index", index, nullptr);
                pad.setProperty("file", samplePaths[bank][index], nullptr);
                pad.setProperty("label", sampleDisplayNames[bank][index], nullptr);
                const auto& controls = bankControls[bank];
                pad.setProperty("muted", controls.muted[index].load(std::memory_order_relaxed), nullptr);
                pad.setProperty("chopStart", controls.start[index].load(std::memory_order_relaxed), nullptr);
                pad.setProperty("chopEnd", controls.end[index].load(std::memory_order_relaxed), nullptr);
                pad.setProperty("sourceBpm", controls.sourceBpm[index].load(std::memory_order_relaxed), nullptr);
                pad.setProperty("warpAnchor", controls.warpAnchor[index].load(std::memory_order_relaxed), nullptr);
                pad.setProperty("steps", static_cast<int>(controls.steps[index].load(std::memory_order_relaxed)), nullptr);
                slot.addChild(pad, -1, nullptr);
            }
            banks.addChild(slot, -1, nullptr);
        }
    }
    root.addChild(banks, -1, nullptr);
    juce::ValueTree sequencer("SEQUENCER");
    sequencer.setProperty("tempo", getTempo(), nullptr);
    sequencer.setProperty("tempoSync", isTempoSyncEnabled(), nullptr);
    sequencer.setProperty("noteRepeat", isNoteRepeatEnabled(), nullptr);
    root.addChild(sequencer, -1, nullptr);

    juce::ValueTree fx("MASTER_FX");
    for (int slot = 0; slot < numberOfFxSlots; ++slot)
    {
        juce::ValueTree entry("SLOT");
        entry.setProperty("index", slot, nullptr);
        entry.setProperty("type", getFxSlotType(slot), nullptr);
        entry.setProperty("amount", getFxSlotAmount(slot), nullptr);
        entry.setProperty("wet", getFxSlotWet(slot), nullptr);
        fx.addChild(entry, -1, nullptr);
    }
    root.addChild(fx, -1, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, dest);
}

void VintageSamplerProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (! xml)
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (! tree.isValid() || ! tree.hasType("VINTAGE_SAMPLER_STATE"))
        return;

    auto savedParameters = tree.getChildWithName("PARAMETERS");
    if (savedParameters.isValid())
        parameters.replaceState(savedParameters);

    // Missing MASTER_FX in old host projects must leave every slot bypassed,
    // including when a previously loaded session had active effects.
    for (int slot = 0; slot < numberOfFxSlots; ++slot)
    {
        setFxSlotType(slot, 0);
        setFxSlotAmount(slot, 0.5f);
        setFxSlotWet(slot, 1.0f);
    }
    const auto savedFx = tree.getChildWithName("MASTER_FX");
    for (int i = 0; i < savedFx.getNumChildren(); ++i)
    {
        const auto entry = savedFx.getChild(i);
        if (!entry.hasType("SLOT")) continue;
        const int index = static_cast<int>(entry.getProperty("index", -1));
        if (!juce::isPositiveAndBelow(index, numberOfFxSlots)) continue;
        setFxSlotAmount(index, static_cast<float>(entry.getProperty("amount", 0.5f)));
        setFxSlotWet(index, static_cast<float>(entry.getProperty("wet", 1.0f)));
        setFxSlotType(index, static_cast<int>(entry.getProperty("type", 0)));
    }

    std::array<std::array<juce::String, 16>, numberOfBanks> restored{};
    std::array<std::array<juce::String, 16>, numberOfBanks> restoredLabels{};
    for (int bank = 0; bank < numberOfBanks; ++bank)
    {
        auto& controls = bankControls[bank];
        for (int index = 0; index < 16; ++index)
        {
            controls.muted[index].store(false, std::memory_order_relaxed);
            controls.start[index].store(0.0f, std::memory_order_relaxed);
            controls.end[index].store(100.0f, std::memory_order_relaxed);
            controls.sourceBpm[index].store(120.0f, std::memory_order_relaxed);
            controls.warpAnchor[index].store(0.0f, std::memory_order_relaxed);
            controls.steps[index].store(0, std::memory_order_relaxed);
        }
    }
    const auto parsePads = [&](int bank, const juce::ValueTree& pads)
    {
        if (!juce::isPositiveAndBelow(bank, numberOfBanks)) return;
        for (int index = 0; index < pads.getNumChildren(); ++index)
        {
            const auto pad = pads.getChild(index);
            if (!pad.hasType("PAD")) continue;
            const auto number = static_cast<int>(pad.getProperty("index", -1));
            if (!juce::isPositiveAndBelow(number, 16)) continue;
            restored[bank][number] = pad.getProperty("file", {}).toString();
            restoredLabels[bank][number] = pad.getProperty("label", {}).toString();
            auto& controls = bankControls[bank];
            controls.muted[number].store(static_cast<bool>(pad.getProperty("muted", false)), std::memory_order_relaxed);
            const float start = static_cast<float>(pad.getProperty("chopStart", 0.0f));
            const float end = static_cast<float>(pad.getProperty("chopEnd", 100.0f));
            const float safeStart = std::isfinite(start) ? juce::jlimit(0.0f, 99.9f, start) : 0.0f;
            const float safeEnd = std::isfinite(end) ? juce::jlimit(safeStart + 0.1f, 100.0f, end) : 100.0f;
            controls.start[number].store(safeStart, std::memory_order_relaxed);
            controls.end[number].store(safeEnd, std::memory_order_relaxed);
            const float bpm = static_cast<float>(pad.getProperty("sourceBpm", 120.0f));
            const float anchor = static_cast<float>(pad.getProperty("warpAnchor", 0.0f));
            controls.sourceBpm[number].store(
                std::isfinite(bpm) ? juce::jlimit(30.0f, 300.0f, bpm) : 120.0f, std::memory_order_relaxed);
            controls.warpAnchor[number].store(
                std::isfinite(anchor) ? juce::jlimit(0.0f, 100.0f, anchor) : 0.0f, std::memory_order_relaxed);
            controls.steps[number].store(static_cast<std::uint32_t>(static_cast<int>(pad.getProperty("steps", 0))),
                                         std::memory_order_relaxed);
        }
    };
    const auto banks = tree.getChildWithName("BANKS");
    if (banks.isValid())
    {
        for (int child = 0; child < banks.getNumChildren(); ++child)
        {
            const auto slot = banks.getChild(child);
            if (slot.hasType("BANK")) parsePads(static_cast<int>(slot.getProperty("index", -1)), slot);
        }
    }
    else
    {
        // v1 projects had one SAMPLES tree: restore their complete kit into A.
        parsePads(0, tree.getChildWithName("SAMPLES"));
    }
    const auto sequencer = tree.getChildWithName("SEQUENCER");
    setTempo(static_cast<float>(sequencer.getProperty("tempo", 120.0f)));
    setTempoSyncEnabled(static_cast<bool>(sequencer.getProperty("tempoSync", false)));
    setNoteRepeatEnabled(static_cast<bool>(sequencer.getProperty("noteRepeat", false)));
    for (auto& held : padHeld) held.store(false, std::memory_order_release);
    stopSequence();

    {
        const juce::ScopedLock guard(stateLock);
        samplePaths = std::move(restored);
        sampleDisplayNames = std::move(restoredLabels);
    }
    const int savedBank = static_cast<int>(tree.getProperty("activeBank", 0));
    selectBank(juce::isPositiveAndBelow(savedBank, numberOfBanks) ? savedBank : 0);

    // AU/host state restoration is not guaranteed to run on a message thread.
    // Deferring disk decoding to AsyncUpdater avoids file I/O in the callback.
    triggerAsyncUpdate();
}

void VintageSamplerProcessor::handleAsyncUpdate()
{
    std::array<std::array<juce::String, 16>, numberOfBanks> pending;
    {
        const juce::ScopedLock guard(stateLock);
        pending = samplePaths;
    }
    // One decode per unique source across all 128 pads. Build all eight
    // immutable banks before publishing; no I/O or refcounts in processBlock.
    std::map<juce::String, std::shared_ptr<const vsl::Sample>> decoded;
    std::array<std::unique_ptr<vsl::Bank>, numberOfBanks> restoredBanks;
    for (int bank = 0; bank < numberOfBanks; ++bank)
    {
        auto next = std::make_unique<vsl::Bank>();
        for (int pad = 0; pad < 16; ++pad)
        {
            const auto& path = pending[bank][pad];
            if (path.isEmpty()) continue;
            auto found = decoded.find(path);
            if (found == decoded.end())
            {
                auto sound = decodeSample(juce::File(path));
                found = decoded.emplace(path, std::move(sound)).first;
            }
            next->pads[pad] = found->second;
        }
        restoredBanks[bank] = std::move(next);
    }
    if (retainedBanks.size() + numberOfBanks > maximumBankRevisions)
    {
        const juce::ScopedLock guard(stateLock);
        lastError = "Bank history limit reached; save and reopen to restore this project.";
        return;
    }
    for (int bank = 0; bank < numberOfBanks; ++bank)
    {
        const auto* snapshot = restoredBanks[bank].get();
        retainedBanks.push_back(std::move(restoredBanks[bank]));
        bankSnapshots[bank].store(snapshot, std::memory_order_release);
    }
    publishedBank.store(bankSnapshots[getSelectedBank()].load(std::memory_order_acquire), std::memory_order_release);
}

void VintageSamplerProcessor::timerCallback()
{
    stopTimer();
    if (startupProjectPath.isEmpty()) return;
    const auto wanted = startupProjectPath;
    startupProjectPath.clear();
    juce::MemoryBlock projectData;
    if (juce::File(wanted).loadFileAsData(projectData)
        && projectData.getSize() > 16 && projectData.getSize() <= 2 * 1024 * 1024)
        setStateInformation(projectData.getData(), static_cast<int>(projectData.getSize()));
    else
    {
        const juce::ScopedLock guard(stateLock);
        lastError = "Startup project could not be loaded; choose PROJECT > OPEN.";
    }
}

juce::AudioProcessorEditor* VintageSamplerProcessor::createEditor()
{
    // JUCE's standalone holder has finished reloading its last user preset
    // before it creates the first editor. Apply an explicitly supplied
    // launch project here to make the requested song win over stale prefs.
    if (startupProjectPath.isNotEmpty()) timerCallback();
    return new VintageSamplerEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VintageSamplerProcessor();
}
