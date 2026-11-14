#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
float finiteRange(float x, float low, float high, float fallback) noexcept
{ return std::isfinite(x) ? juce::jlimit(low, high, x) : fallback; }
float clamp01(float x) noexcept { return finiteRange(x, 0.0f, 1.0f, 0.0f); }
constexpr double pi = juce::MathConstants<double>::pi;
}

ASR10Processor::ASR10Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    formats.registerBasicFormats();
    for (auto& p : clips) p.store(nullptr, std::memory_order_relaxed);
    for (auto& e : noteEvents) e.store(0);
    for (auto& v : queuedVelocity) v.store(1.0f);
    for (int i = 0; i < instruments; ++i)
        for (int j = 0; j <= 16; ++j)
            instrumentSettings[static_cast<size_t>(i)].chopPoints[static_cast<size_t>(j)].store(
                static_cast<float>(j) / 16.0f, std::memory_order_relaxed);
    auto factory = makeFactoryClip();
    clips[0].store(factory.get(), std::memory_order_release);
    names[0] = factory->name;
    cacheBytes = factory->left.size() * sizeof(float) + factory->right.size() * sizeof(float);
    retainedClips.push_back(std::move(factory));
}

ASR10Processor::~ASR10Processor() { cancelPendingUpdate(); }

juce::String ASR10Processor::getLastSampleLoadError() const
{
    const juce::ScopedLock lock(nonAudioStateLock);
    return sampleLoadError;
}

bool ASR10Processor::reclaimCache(size_t incomingBytes)
{
    // Non-audio-thread-only. Exclusive CAS makes raw-pointer use and deletion
    // mutually exclusive. Failed acquisition does not disturb playback.
    uint8_t expected = 0;
    if (!callbackGate.compare_exchange_strong(expected, 2, std::memory_order_acq_rel))
        return retainedClips.size() < maxCachedClips
            && incomingBytes <= maxDecodedBytes - std::min(cacheBytes, maxDecodedBytes);

    const auto pinned = [this](const Clip* c) {
        for (const auto& slot : clips)
            if (slot.load(std::memory_order_acquire) == c) return true;
        for (const auto& voice : voices)
            if (voice.active && voice.clip == c) return true;
        return false;
    };
    for (auto it = retainedClips.begin();
         it != retainedClips.end();) {
        if (!pinned(it->get())) {
            const auto& c = **it;
            cacheBytes -= (c.left.size() + c.right.size()) * sizeof(float);
            it = retainedClips.erase(it);
        } else {
            ++it;
        }
    }
    callbackGate.store(0, std::memory_order_release);
    return retainedClips.size() < maxCachedClips
        && incomingBytes <= maxDecodedBytes - std::min(cacheBytes, maxDecodedBytes);
}

std::unique_ptr<ASR10Processor::Clip> ASR10Processor::decodeSample(
    const juce::File& file, juce::String& error)
{
    error.clear();
    if (!file.existsAsFile()) {
        error = "FILE MISSING " + file.getFullPathName();
        return {};
    }
    // An Expansion/WebDAV mount can transiently reject a stream open even
    // when the WAV remains valid and readable. This is only called on the
    // control thread, never in processBlock; retry boundedly without adding
    // any audio-thread work or copying protected recordings.
    std::unique_ptr<juce::AudioFormatReader> reader;
    for (int attempt = 0; attempt < 6 && !reader; ++attempt) {
        if (attempt != 0)
            juce::Thread::sleep(attempt * 180);
        reader.reset(formats.createReaderFor(file));
    }
    if (!reader && file.getSize() > 0 && file.getSize() <= 32LL * 1024 * 1024) {
        // Bounded control-thread fallback for transient remote stream issues.
        // The temporary decoded source bytes are never persisted or bundled.
        juce::MemoryBlock fileBytes;
        for (int attempt = 0; attempt < 3 && !reader; ++attempt) {
            if (attempt != 0)
                juce::Thread::sleep(attempt * 280);
            fileBytes.reset();
            if (file.loadFileAsData(fileBytes))
                reader.reset(formats.createReaderFor(
                    std::make_unique<juce::MemoryInputStream>(std::move(fileBytes))));
        }
    }
    if (!reader) {
        error = "READER OPEN FAILED " + file.getFullPathName();
        return {};
    }
    constexpr juce::int64 maxDecodedFrames = 30'000'000;
    if (reader->lengthInSamples <= 0 || !std::isfinite(reader->sampleRate)
        || reader->sampleRate <= 0 || reader->lengthInSamples > maxDecodedFrames
        || reader->lengthInSamples > std::numeric_limits<int>::max()) {
        error = "INVALID AUDIO LENGTH/RATE " + file.getFullPathName();
        return {};
    }
    const size_t requiredBytes = static_cast<size_t>(reader->lengthInSamples) * 2 * sizeof(float);
    if (requiredBytes > maxDecodedBytes) {
        error = "SAMPLE OVER 256MiB " + file.getFullPathName();
        return {};
    }
    const int length = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> decoded(2, length);
    if (!reader->read(&decoded, 0, length, 0, true, true)) {
        error = "AUDIO READ FAILED " + file.getFullPathName();
        return {};
    }
    auto clip = std::make_unique<Clip>();
    clip->left.assign(decoded.getReadPointer(0), decoded.getReadPointer(0) + length);
    clip->right.assign(decoded.getReadPointer(1), decoded.getReadPointer(1) + length);
    clip->rate = reader->sampleRate;
    clip->path = file.getFullPathName();
    clip->name = file.getFileNameWithoutExtension();
    for (int k = 0; k < 128; ++k) {
        float peak = 0;
        for (int j = static_cast<int>(static_cast<juce::int64>(k)*length/128);
             j < static_cast<int>(static_cast<juce::int64>(k+1)*length/128); ++j)
            peak = std::max(peak, std::max(std::abs(clip->left[static_cast<size_t>(j)]),
                                           std::abs(clip->right[static_cast<size_t>(j)])));
        clip->peaks[static_cast<size_t>(k)] = peak;
    }
    return clip;
}

std::unique_ptr<ASR10Processor::Clip> ASR10Processor::makeFactoryClip()
{
    auto c = std::make_unique<Clip>();
    c->name = "ANALOG KEYS";
    constexpr int n = 44100 * 2;
    c->left.resize(n);
    c->right.resize(n);
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / c->rate;
        const double p = 2.0 * pi * 261.625565 * t;
        const float decay = static_cast<float>(std::exp(-t * 0.75));
        const float s = decay * static_cast<float>(0.55 * std::sin(p) + 0.20 * std::sin(2*p)
                            + 0.12 * std::sin(3*p) + 0.06 * std::sin(4*p));
        c->left[static_cast<size_t>(i)] = s;
        c->right[static_cast<size_t>(i)] = s;
    }
    for (int k = 0; k < 128; ++k) {
        float peak = 0;
        for (int j = k*n/128; j < (k+1)*n/128; ++j)
            peak = std::max(peak, std::abs(c->left[static_cast<size_t>(j)]));
        c->peaks[static_cast<size_t>(k)] = peak;
    }
    return c;
}

bool ASR10Processor::loadSample(int instrument, const juce::File& file)
{
    if (instrument < 0 || instrument >= instruments) {
        const juce::ScopedLock lock(nonAudioStateLock);
        sampleLoadError = "INVALID INSTRUMENT INDEX " + juce::String(instrument);
        return false;
    }
    const auto slot = static_cast<size_t>(instrument);
    const auto absolutePath = file.getFullPathName();
    if (const auto* current = clips[slot].load(std::memory_order_acquire);
        current != nullptr && current->path == absolutePath) {
        const juce::ScopedLock lock(nonAudioStateLock);
        sampleLoadError.clear();
        return true;
    }
    for (const auto& existing : retainedClips) {
        if (existing->path.isNotEmpty() && existing->path == absolutePath) {
            const juce::ScopedLock lock(nonAudioStateLock);
            paths[slot] = existing->path;
            names[slot] = existing->name;
            clips[slot].store(existing.get(), std::memory_order_release);
            sampleLoadError.clear();
            return true;
        }
    }
    juce::String reason;
    auto clip = decodeSample(file, reason);
    if (!clip) {
        const juce::ScopedLock lock(nonAudioStateLock);
        sampleLoadError = reason;
        return false;
    }
    const auto requiredBytes = (clip->left.size() + clip->right.size()) * sizeof(float);
    if (!reclaimCache(requiredBytes)) {
        const juce::ScopedLock lock(nonAudioStateLock);
        sampleLoadError = "CACHE BUSY/FULL (" + juce::String(static_cast<int>(retainedClips.size()))
            + " clips, " + juce::String(static_cast<juce::int64>(cacheBytes))
            + " bytes, incoming " + juce::String(static_cast<juce::int64>(requiredBytes))
            + " bytes) " + absolutePath;
        return false;
    }
    const juce::ScopedLock lock(nonAudioStateLock);
    const Clip* published = clip.get();
    retainedClips.push_back(std::move(clip));
    cacheBytes += requiredBytes;
    paths[slot] = absolutePath;
    names[static_cast<size_t>(instrument)] = published->name;
    clips[static_cast<size_t>(instrument)].store(published, std::memory_order_release);
    sampleLoadError.clear();
    return true;
}

bool ASR10Processor::installBankClips(
    std::array<std::unique_ptr<Clip>, instruments>& staged,
    const std::array<const Clip*, instruments>& reused, juce::String& error)
{
    // Commit only after every source has decoded. Try to acquire exclusivity
    // briefly; failed attempts retain the existing bank without changing slots.
    uint8_t expected = 0;
    if (!callbackGate.compare_exchange_strong(expected, 2, std::memory_order_acq_rel)) {
        error = "CACHE GATE BUSY (audio callback active)";
        return false;
    }

    // This exclusive gate protects active Voice::clip references while all
    // eight slot pointers are replaced. Previous slots are no longer pins.
    size_t incomingBytes = 0;
    size_t incomingCount = 0;
    std::array<const Clip*, instruments> next = reused;
    for (auto& c : staged) {
        if (c) {
            incomingBytes += (c->left.size() + c->right.size()) * sizeof(float);
            ++incomingCount;
        }
    }
    for (size_t i = 0; i < next.size(); ++i)
        if (staged[i]) next[i] = staged[i].get();
    const auto keep = [this, &next](const Clip* c) {
        for (const auto* retained : next)
            if (retained == c) return true;
        for (const auto& voice : voices)
            if (voice.active && voice.clip == c) return true;
        return false;
    };
    // Stage plan and validate before mutating cache; old clips can be evicted
    // only after the complete replacement is known to fit.
    size_t keptBytes = 0;
    size_t keptCount = 0;
    for (const auto& c : retainedClips) {
        if (keep(c.get())) {
            ++keptCount;
            keptBytes += (c->left.size() + c->right.size()) * sizeof(float);
        }
    }
    const bool fits = keptCount + incomingCount <= maxCachedClips
        && keptBytes <= maxDecodedBytes
        && incomingBytes <= maxDecodedBytes - keptBytes;
    if (!fits) {
        error = "CACHE PINNED LIMIT (" + juce::String(static_cast<int>(keptCount))
            + " held + " + juce::String(static_cast<int>(incomingCount)) + " new clips, "
            + juce::String(static_cast<juce::int64>(keptBytes))
            + " held + " + juce::String(static_cast<juce::int64>(incomingBytes))
            + " new bytes)";
        callbackGate.store(0, std::memory_order_release);
        return false;
    }
    // Any allocation happens on the control thread. Reserve before deletion
    // and pointer publication so a capacity change cannot invalidate commit.
    retainedClips.reserve(std::max(retainedClips.size(), keptCount + incomingCount));
    for (auto it = retainedClips.begin(); it != retainedClips.end();) {
        if (!keep(it->get())) {
            const auto& old = **it;
            cacheBytes -= (old.left.size() + old.right.size()) * sizeof(float);
            it = retainedClips.erase(it);
        } else {
            ++it;
        }
    }
    for (size_t i = 0; i < staged.size(); ++i) {
        if (staged[i]) {
            cacheBytes += (staged[i]->left.size() + staged[i]->right.size()) * sizeof(float);
            retainedClips.push_back(std::move(staged[i]));
        }
    }
    {
        const juce::ScopedLock lock(nonAudioStateLock);
        for (size_t i = 0; i < next.size(); ++i) {
            clips[i].store(next[i], std::memory_order_release);
            paths[i] = next[i] ? next[i]->path : juce::String{};
            names[i] = next[i] ? next[i]->name : juce::String{};
        }
        sampleLoadError.clear();
    }
    callbackGate.store(0, std::memory_order_release);
    return true;
}

bool ASR10Processor::loadBank(const juce::File& manifest, juce::String& error)
{
    error.clear();
    if (!manifest.existsAsFile() || manifest.getSize() > 1024 * 1024) {
        error = "BANK FILE MISSING/LARGE"; return false;
    }
    juce::var data;
    // The bank index lives on the same WebDAV mount as its WAVs. A transient
    // zero-length/partial read can make perfectly valid JSON fail to parse.
    // Retry only on the control thread; parser work stays off the audio path.
    for (int attempt = 0; attempt < 5 && !data.isObject(); ++attempt) {
        if (attempt != 0)
            juce::Thread::sleep(attempt * 180);
        const auto manifestText = manifest.loadFileAsString();
        if (manifestText.isNotEmpty() && manifestText.length() <= 1024 * 1024)
            data = juce::JSON::parse(manifestText);
    }
    if (!data.isObject()) { error = "INVALID BANK JSON"; return false; }
    const auto entries = data.getProperty("instruments", {});
    if (!entries.isArray() || entries.getArray()->size() != instruments) {
        error = "BANK NEEDS 8 SLOTS"; return false;
    }
    // An optional practice sequence is exactly one bar of 16 step arrays.
    // Validate it before loading files to keep malformed manifests harmless.
    const auto practiceSequence = data.getProperty("sequence", {});
    if (!practiceSequence.isVoid()) {
        if (!practiceSequence.isArray() || practiceSequence.getArray()->size() != 16) {
            error = "SEQUENCE NEEDS 16 STEPS"; return false;
        }
        for (int step = 0; step < 16; ++step) {
            const auto notes = practiceSequence[step];
            if (!notes.isArray() || notes.getArray()->size() > 16) {
                error = "INVALID SEQUENCE STEP " + juce::String(step + 1); return false;
            }
            for (const auto& note : *notes.getArray()) {
                if (!(note.isInt() || note.isInt64())
                    || static_cast<int>(note) < 0 || static_cast<int>(note) > 127) {
                    error = "INVALID SEQUENCE NOTE"; return false;
                }
            }
        }
    }
    // Resolve and validate all sample paths before applying any bank settings.
    std::array<juce::File, instruments> files;
    std::array<bool, instruments> populated{};
    std::array<std::array<float, 17>, instruments> bankChops{};
    std::array<float, instruments> bankStarts{}, bankEnds{};
    for (int i = 0; i < instruments; ++i) {
        const auto item = entries[i];
        if (!item.isObject()) { error = "INVALID INSTRUMENT " + juce::String(i+1); return false; }
        const auto rawStart = item.getProperty("sampleStart", {});
        const auto rawEnd = item.getProperty("sampleEnd", {});
        const auto numeric = [](const juce::var& value) {
            return value.isInt() || value.isInt64() || value.isDouble();
        };
        const float start = rawStart.isVoid() ? 0.0f
            : numeric(rawStart) ? finiteRange(static_cast<float>(static_cast<double>(rawStart)), 0.0f, 1.0f, 0.0f) : 0.0f;
        const float end = rawEnd.isVoid() ? 1.0f
            : numeric(rawEnd) ? finiteRange(static_cast<float>(static_cast<double>(rawEnd)), 0.0f, 1.0f, 1.0f) : 1.0f;
        // Invalid or reversed ranges safely fall back to the entire track.
        const bool validTrim = end > start;
        bankStarts[static_cast<size_t>(i)] = validTrim ? start : 0.0f;
        bankEnds[static_cast<size_t>(i)] = validTrim ? end : 1.0f;
        auto& points = bankChops[static_cast<size_t>(i)];
        for (int j = 0; j <= 16; ++j)
            points[static_cast<size_t>(j)] = static_cast<float>(j) / 16.0f;
        const auto supplied = item.getProperty("chopPoints", {});
        if (!supplied.isVoid()) {
            if (!supplied.isArray() || supplied.getArray()->size() != 17) {
                error = "SLOT " + juce::String(i+1) + " CHOP POINTS NEED 17 VALUES"; return false;
            }
            for (int j = 0; j <= 16; ++j) {
                const auto value = supplied[j];
                if (!(value.isInt() || value.isInt64() || value.isDouble())) {
                    error = "INVALID CHOP VALUE SLOT " + juce::String(i+1); return false;
                }
                const float point = static_cast<float>(static_cast<double>(value));
                if (!std::isfinite(point) || point < 0.0f || point > 1.0f
                    || (j == 0 && point != 0.0f) || (j == 16 && point != 1.0f)
                    || (j > 0 && point < points[static_cast<size_t>(j-1)])) {
                    error = "UNSORTED CHOP POINTS SLOT " + juce::String(i+1); return false;
                }
                points[static_cast<size_t>(j)] = point;
            }
        }
        const auto path = item.getProperty("path", {}).toString();
        if (path.isEmpty()) continue;
        auto candidate = juce::File::isAbsolutePath(path) ? juce::File(path)
                         : manifest.getParentDirectory().getChildFile(path);
        if (!candidate.existsAsFile()) { error = "MISSING SLOT " + juce::String(i+1); return false; }
        files[static_cast<size_t>(i)] = candidate;
        populated[static_cast<size_t>(i)] = true;
    }
    std::array<std::unique_ptr<Clip>, instruments> staged;
    std::array<const Clip*, instruments> reused{};
    for (int i = 0; i < instruments; ++i) {
        if (!populated[static_cast<size_t>(i)]) continue;
        const auto path = files[static_cast<size_t>(i)].getFullPathName();
        for (const auto& c : retainedClips) {
            if (c->path.isNotEmpty() && c->path == path) {
                reused[static_cast<size_t>(i)] = c.get();
                break;
            }
        }
        if (reused[static_cast<size_t>(i)]) continue;
        // A bank may intentionally map one WAV to more than one instrument.
        // Decode it once, then share the immutable Clip when committing.
        for (int j = 0; j < i; ++j) {
            if (populated[static_cast<size_t>(j)]
                && files[static_cast<size_t>(j)].getFullPathName() == path) {
                if (reused[static_cast<size_t>(j)])
                    reused[static_cast<size_t>(i)] = reused[static_cast<size_t>(j)];
                else if (staged[static_cast<size_t>(j)])
                    reused[static_cast<size_t>(i)] = staged[static_cast<size_t>(j)].get();
                break;
            }
        }
        if (reused[static_cast<size_t>(i)]) continue;
        juce::String reason;
        staged[static_cast<size_t>(i)] = decodeSample(files[static_cast<size_t>(i)], reason);
        if (!staged[static_cast<size_t>(i)]) {
            error = "FAILED SLOT " + juce::String(i+1) + ": " + reason;
            const juce::ScopedLock lock(nonAudioStateLock);
            sampleLoadError = error;
            return false;
        }
    }
    if (!installBankClips(staged, reused, error)) {
        error = "BANK CLIP COMMIT FAILED: " + error;
        const juce::ScopedLock lock(nonAudioStateLock);
        sampleLoadError = error;
        return false;
    }
    const int selected = juce::jlimit(0, instruments-1, static_cast<int>(data.getProperty("selectedInstrument", 0)));
    for (int i = 0; i < instruments; ++i) {
        const auto settings = entries[i];
        auto& slot = instrumentSettings[static_cast<size_t>(i)];
        slot.rootNote.store(juce::jlimit(0, 127, static_cast<int>(settings.getProperty("rootNote", 60))));
        slot.gain.store(clamp01(static_cast<float>(settings.getProperty("volume", 0.8))));
        slot.attack.store(finiteRange(static_cast<float>(settings.getProperty("attack", 0.005)), 0.0f, 10.0f, 0.005f));
        slot.release.store(finiteRange(static_cast<float>(settings.getProperty("release", 0.25)), 0.001f, 20.0f, 0.25f));
        slot.filter.store(clamp01(static_cast<float>(settings.getProperty("filter", 1.0))));
        slot.pitch.store(finiteRange(static_cast<float>(settings.getProperty("pitch", 0.0)), -36.0f, 36.0f, 0.0f));
        slot.fxType.store(juce::jlimit(0, 4, static_cast<int>(settings.getProperty("fxType", 0))));
        slot.fxAmount.store(clamp01(static_cast<float>(settings.getProperty("fxAmount", 0.3))));
        slot.loop.store(static_cast<bool>(settings.getProperty("loop", false)));
        slot.sampleStart.store(bankStarts[static_cast<size_t>(i)]);
        slot.sampleEnd.store(bankEnds[static_cast<size_t>(i)]);
        replaceChopPoints(i, bankChops[static_cast<size_t>(i)]);
    }
    setTempo(static_cast<float>(data.getProperty("tempo", 120.0)));
    selectInstrument(selected);
    // Prevent the previous performance's trim and beat from leaking into a
    // newly selected bank. Programmed patterns are original practice rhythms.
    stopSequence();
    allNotesOff();
    setChopMode(static_cast<bool>(data.getProperty("chop", false)));
    for (int step = 0; step < 16; ++step) {
        uint64_t low = 0, high = 0;
        if (practiceSequence.isArray()) {
            for (const auto& noteValue : *practiceSequence[step].getArray()) {
                const int note = static_cast<int>(noteValue);
                if (note < 64) low |= uint64_t(1) << note;
                else high |= uint64_t(1) << (note - 64);
            }
        }
        steps[static_cast<size_t>(step)].notesLow.store(low, std::memory_order_release);
        steps[static_cast<size_t>(step)].notesHigh.store(high, std::memory_order_release);
    }
    seqReset.store(true, std::memory_order_release);
    {
        const juce::ScopedLock lock(nonAudioStateLock);
        bankName = data.getProperty("name", "UNTITLED BANK").toString().substring(0, 64);
    }
    return true;
}

juce::String ASR10Processor::getBankName() const
{
    const juce::ScopedLock lock(nonAudioStateLock);
    return bankName;
}
void ASR10Processor::setChopMode(bool enabled) noexcept { chopMode.store(enabled); }
bool ASR10Processor::getChopMode() const noexcept { return chopMode.load(); }
int ASR10Processor::getLastSlice() const noexcept { return lastSlice.load(); }

void ASR10Processor::replaceChopPoints(int instrument, const std::array<float, 17>& points) noexcept
{
    if (instrument < 0 || instrument >= instruments) return;
    auto& settings = instrumentSettings[static_cast<size_t>(instrument)];
    uint32_t revision = settings.chopRevision.load(std::memory_order_acquire);
    // State/bank commits happen off the audio callback; serialize rare editors.
    for (;;) {
        if ((revision & 1u) == 0
            && settings.chopRevision.compare_exchange_weak(revision, revision + 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) break;
        revision = settings.chopRevision.load(std::memory_order_acquire);
    }
    for (int i = 0; i <= 16; ++i)
        settings.chopPoints[static_cast<size_t>(i)].store(points[static_cast<size_t>(i)], std::memory_order_relaxed);
    settings.chopRevision.store(revision + 2, std::memory_order_release);
}

bool ASR10Processor::setChopPoint(int instrument, int boundary, float normalized) noexcept
{
    if (instrument < 0 || instrument >= instruments || boundary <= 0 || boundary >= 16
        || !std::isfinite(normalized) || normalized < 0.0f || normalized > 1.0f) return false;
    auto& s = instrumentSettings[static_cast<size_t>(instrument)];
    uint32_t revision = s.chopRevision.load(std::memory_order_acquire);
    if ((revision & 1u) != 0 || !s.chopRevision.compare_exchange_strong(revision, revision + 1,
            std::memory_order_acq_rel, std::memory_order_acquire)) return false;
    const auto index = static_cast<size_t>(boundary);
    const bool valid = normalized >= s.chopPoints[index-1].load(std::memory_order_relaxed)
                    && normalized <= s.chopPoints[index+1].load(std::memory_order_relaxed);
    if (valid) s.chopPoints[index].store(normalized, std::memory_order_relaxed);
    s.chopRevision.store(revision + 2, std::memory_order_release);
    return valid;
}

float ASR10Processor::getChopPoint(int instrument, int boundary) const noexcept
{
    if (instrument < 0 || instrument >= instruments || boundary < 0 || boundary > 16) return 0.0f;
    const auto& s = instrumentSettings[static_cast<size_t>(instrument)];
    const auto revision = s.chopRevision.load(std::memory_order_acquire);
    if (revision & 1u) return static_cast<float>(boundary) / 16.0f;
    const float value = s.chopPoints[static_cast<size_t>(boundary)].load(std::memory_order_relaxed);
    return revision == s.chopRevision.load(std::memory_order_acquire)
        ? value : static_cast<float>(boundary) / 16.0f;
}

juce::String ASR10Processor::getInstrumentName(int i) const
{
    if (i < 0 || i >= instruments) return {};
    const auto* c = clips[static_cast<size_t>(i)].load(std::memory_order_acquire);
    return c ? c->name : "EMPTY " + juce::String(i + 1);
}
bool ASR10Processor::hasSample(int i) const noexcept
{ return i >= 0 && i < instruments && clips[static_cast<size_t>(i)].load(std::memory_order_acquire) != nullptr; }
void ASR10Processor::selectInstrument(int i) noexcept { if (i >= 0 && i < instruments) currentInstrument.store(i); }
int ASR10Processor::getSelectedInstrument() const noexcept { return currentInstrument.load(); }
void ASR10Processor::triggerNote(int n, float v) noexcept
{
    if (n < 0 || n > 127) return;
    queuedVelocity[static_cast<size_t>(n)].store(clamp01(v), std::memory_order_relaxed);
    noteEvents[static_cast<size_t>(n)].store(1, std::memory_order_release);
}
void ASR10Processor::releaseNote(int n) noexcept
{ if (n >= 0 && n < 128) noteEvents[static_cast<size_t>(n)].store(2, std::memory_order_release); }
void ASR10Processor::allNotesOff() noexcept { panic.store(true, std::memory_order_release); }
void ASR10Processor::setRootNote(int n) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].rootNote.store(juce::jlimit(0,127,n)); }
int ASR10Processor::getRootNote() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].rootNote.load(); }
void ASR10Processor::setLoopEnabled(bool v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].loop.store(v); }
bool ASR10Processor::getLoopEnabled() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].loop.load(); }
void ASR10Processor::setSampleStart(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].sampleStart.store(clamp01(v)); }
float ASR10Processor::getSampleStart() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].sampleStart.load(); }
void ASR10Processor::setSampleEnd(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].sampleEnd.store(finiteRange(v,0.0f,1.0f,1.0f)); }
float ASR10Processor::getSampleEnd() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].sampleEnd.load(); }
void ASR10Processor::setVolume(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].gain.store(clamp01(v)); }
float ASR10Processor::getVolume() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].gain.load(); }
void ASR10Processor::setAttack(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].attack.store(finiteRange(v,0.0f,10.0f,0.005f)); }
float ASR10Processor::getAttack() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].attack.load(); }
void ASR10Processor::setRelease(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].release.store(finiteRange(v,0.001f,20.0f,0.25f)); }
float ASR10Processor::getRelease() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].release.load(); }
void ASR10Processor::setPitch(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].pitch.store(finiteRange(v,-36.0f,36.0f,0.0f)); }
float ASR10Processor::getPitch() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].pitch.load(); }
void ASR10Processor::setFilter(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].filter.store(clamp01(v)); }
float ASR10Processor::getFilter() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].filter.load(); }
void ASR10Processor::setFxType(int v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].fxType.store(juce::jlimit(0,4,v)); }
int ASR10Processor::getFxType() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].fxType.load(); }
void ASR10Processor::setFxAmount(float v) noexcept { instrumentSettings[static_cast<size_t>(getSelectedInstrument())].fxAmount.store(clamp01(v)); }
float ASR10Processor::getFxAmount() const noexcept { return instrumentSettings[static_cast<size_t>(getSelectedInstrument())].fxAmount.load(); }
std::array<float,128> ASR10Processor::getSelectedWaveformPeaks() const noexcept
{
    const int selected = currentInstrument.load(std::memory_order_acquire);
    if (selected < 0 || selected >= instruments) return {};
    const auto* c = clips[static_cast<size_t>(selected)].load(std::memory_order_acquire);
    return c ? c->peaks : std::array<float,128>{};
}
void ASR10Processor::startSequence(bool rec) noexcept
{
    recording.store(rec);
    playing.store(true);
    seqReset.store(true);
}
void ASR10Processor::stopSequence() noexcept { playing.store(false); recording.store(false); }
bool ASR10Processor::isSequencePlaying() const noexcept { return playing.load(); }
bool ASR10Processor::isSequenceRecording() const noexcept { return recording.load(); }
float ASR10Processor::getTempo() const noexcept { return tempo.load(); }
void ASR10Processor::setTempo(float x) noexcept { tempo.store(finiteRange(x,30.0f,300.0f,120.0f)); }

void ASR10Processor::prepareToPlay(double sr, int)
{
    samplerate = std::isfinite(sr) && sr > 0 ? sr : 44100.0;
    for (auto& v : voices) v = {};
    seqCountdown = 0;
    seqStep.store(0);
    crushCounter = 0;
    crushHoldL = crushHoldR = 0.0f;
    fxFilterL = fxFilterR = 0.0f;
    lastFxType = -1;
}
bool ASR10Processor::isBusesLayoutSupported(const BusesLayout& layout) const
{ return layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* ASR10Processor::createEditor() { return new ASR10Editor(*this); }

void ASR10Processor::beginVoice(int note, float velocity) noexcept
{
    const int selected = currentInstrument.load(std::memory_order_relaxed);
    if (selected < 0 || selected >= instruments || !std::isfinite(velocity)) return;
    const auto idx = static_cast<size_t>(selected);
    const auto& settings = instrumentSettings[idx];
    const Clip* c = clips[idx].load(std::memory_order_acquire);
    if (!c || c->left.empty() || velocity <= 0) return;
    Voice* target = nullptr;
    for (auto& v : voices) if (!v.active) { target = &v; break; }
    if (!target) target = &*std::min_element(voices.begin(), voices.end(),
                              [](const Voice& a, const Voice& b) { return a.age < b.age; });
    *target = {};
    target->active = true;
    target->clip = c;
    target->note = note;
    target->velocity = velocity;
    target->age = ++noteAge;
    const bool chop = chopMode.load(std::memory_order_relaxed) && note >= 48 && note <= 63;
    const double n = static_cast<double>(c->left.size() - 1);
    const double clipBegin = clamp01(settings.sampleStart.load()) * n;
    const double clipEnd = std::max(clipBegin + 1.0, static_cast<double>(clamp01(settings.sampleEnd.load())) * n);
    const int slice = note - 48;
    target->chopped = chop;
    float startFraction = static_cast<float>(slice) / 16.0f;
    float endFraction = static_cast<float>(slice + 1) / 16.0f;
    if (chop) {
        const auto revision = settings.chopRevision.load(std::memory_order_acquire);
        if ((revision & 1u) == 0) {
            const float start = settings.chopPoints[static_cast<size_t>(slice)].load(std::memory_order_relaxed);
            const float end = settings.chopPoints[static_cast<size_t>(slice+1)].load(std::memory_order_relaxed);
            if (revision == settings.chopRevision.load(std::memory_order_acquire)) {
                startFraction = start;
                endFraction = end;
            }
        }
    }
    target->regionBegin = chop ? clipBegin + (clipEnd - clipBegin) * startFraction : clipBegin;
    target->regionEnd = chop ? clipBegin + (clipEnd - clipBegin) * endFraction : clipEnd;
    target->position = target->regionBegin;
    target->increment = (c->rate / samplerate) * (chop ? 1.0 : std::exp2((note - settings.rootNote.load() + settings.pitch.load()) / 12.0));
    const float aSeconds = settings.attack.load();
    const float rSeconds = settings.release.load();
    const float cutoff = settings.filter.load();
    target->attackStep = aSeconds <= 0.00001f ? 1.0f : 1.0f / (aSeconds * static_cast<float>(samplerate));
    target->releaseCoeff = std::exp(-1.0f / (std::max(0.001f, rSeconds) * static_cast<float>(samplerate)));
    target->filterAlpha = 1.0f - std::exp(-2.0f * static_cast<float>(pi) *
        (50.0f + 18500.0f * cutoff * cutoff) / static_cast<float>(samplerate));
    target->volume = settings.gain.load();
    target->looping = settings.loop.load();
    if (chop) lastSlice.store(slice, std::memory_order_relaxed);
}
void ASR10Processor::endVoice(int note) noexcept
{ for (auto& v : voices) if (v.active && v.note == note) v.released = true; }
void ASR10Processor::emitSequenceStep(int step) noexcept
{
    const uint64_t lo = steps[static_cast<size_t>(step)].notesLow.load();
    const uint64_t hi = steps[static_cast<size_t>(step)].notesHigh.load();
    for (int n = 0; n < 128; ++n)
        if (((n < 64 ? lo : hi) >> (n % 64)) & 1ULL) beginVoice(n, 0.85f);
}

void ASR10Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    uint8_t expected = 0;
    if (!callbackGate.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        buffer.clear();
        return;
    }
    buffer.clear();
    if (panic.exchange(false)) for (auto& v : voices) v.active = false;
    for (int n = 0; n < 128; ++n) {
        const auto e = noteEvents[static_cast<size_t>(n)].exchange(0, std::memory_order_acq_rel);
        if (e == 1) {
            beginVoice(n, queuedVelocity[static_cast<size_t>(n)].load());
            if (recording.load()) {
                const auto step = static_cast<size_t>(seqStep.load());
                if (n < 64) steps[step].notesLow.fetch_or(1ULL << n);
                else steps[step].notesHigh.fetch_or(1ULL << (n - 64));
            }
        }
        else if (e == 2) endVoice(n);
    }
    auto* l = buffer.getWritePointer(0);
    auto* r = buffer.getWritePointer(1);
    const int count = buffer.getNumSamples();
    const double stepLength = samplerate * 15.0 / std::max(30.0f, tempo.load());
    const auto& selectedSettings = instrumentSettings[static_cast<size_t>(currentInstrument.load())];
    const float amount = selectedSettings.fxAmount.load();
    const int effect = selectedSettings.fxType.load();
    if (effect != lastFxType) {
        fxFilterL = fxFilterR = 0.0f;
        crushHoldL = crushHoldR = 0.0f;
        crushCounter = 0;
        lastFxType = effect;
    }
    const float fxCutoff = effect == 1
        ? 100.0f + 18000.0f * (1.0f - amount) * (1.0f - amount)
        : 30.0f + 12000.0f * amount * amount;
    const float fxAlpha = 1.0f - std::exp(-2.0f * static_cast<float>(pi) * fxCutoff
                                         / static_cast<float>(samplerate));
    auto it = midi.begin();
    for (int i = 0; i < count; ++i) {
        while (it != midi.end() && (*it).samplePosition <= i) {
            const auto msg = (*it).getMessage();
            if (msg.isNoteOn()) {
                beginVoice(msg.getNoteNumber(), msg.getFloatVelocity());
                if (recording.load()) {
                    const int step = seqStep.load();
                    if (msg.getNoteNumber() < 64)
                        steps[static_cast<size_t>(step)].notesLow.fetch_or(1ULL << msg.getNoteNumber());
                    else steps[static_cast<size_t>(step)].notesHigh.fetch_or(1ULL << (msg.getNoteNumber()-64));
                }
            } else if (msg.isNoteOff()) endVoice(msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) v.released = true;
            ++it;
        }
        if (seqReset.exchange(false)) { seqCountdown = 0; seqStep.store(0); }
        if (playing.load() && seqCountdown <= 0.0) {
            const int s = seqStep.load();
            if (!recording.load()) emitSequenceStep(s);
            seqStep.store((s + 1) % 16);
            seqCountdown += stepLength;
        }
        if (playing.load()) seqCountdown -= 1.0;
        float sl = 0, sr = 0;
        for (auto& v : voices) {
            if (!v.active || !v.clip) continue;
            const auto& c = *v.clip;
            const auto len = c.left.size();
            const double begin = v.regionBegin;
            const double end = v.regionEnd;
            if (v.position >= end || v.position >= static_cast<double>(len-1)) {
                if (v.looping && !v.released && !v.chopped) v.position = begin + std::fmod(std::max(0.0, v.position - begin), std::max(1.0,end-begin));
                else { v.active = false; continue; }
            }
            const size_t p = static_cast<size_t>(v.position);
            const float fraction = static_cast<float>(v.position - static_cast<double>(p));
            const float left = c.left[p] + (c.left[p+1] - c.left[p]) * fraction;
            const float right = c.right[p] + (c.right[p+1] - c.right[p]) * fraction;
            if (v.released && !v.chopped) v.envelope *= v.releaseCoeff;
            else v.envelope = std::min(1.0f, v.envelope + (v.chopped ? std::max(v.attackStep, 1.0f / (0.003f * static_cast<float>(samplerate))) : v.attackStep));
            if (v.envelope < 0.0001f && v.released) { v.active = false; continue; }
            const float tail = v.chopped ? static_cast<float>(juce::jlimit(0.0, 1.0,
                (end - v.position) / std::max(1.0, 0.003 * c.rate))) : 1.0f;
            v.filterL += v.filterAlpha * (left - v.filterL);
            v.filterR += v.filterAlpha * (right - v.filterR);
            sl += v.filterL * v.envelope * v.velocity * v.volume * tail;
            sr += v.filterR * v.envelope * v.velocity * v.volume * tail;
            v.position += v.increment;
        }
        if (effect == 1 || effect == 2) {
            fxFilterL += fxAlpha * (sl - fxFilterL);
            fxFilterR += fxAlpha * (sr - fxFilterR);
            if (effect == 1) {
                sl = fxFilterL;
                sr = fxFilterR;
            } else {
                sl -= fxFilterL;
                sr -= fxFilterR;
            }
        } else if (effect == 3) {
            const float drive = 1.0f + 24.0f * amount;
            const float norm = 1.0f / std::tanh(drive);
            sl = std::tanh(sl * drive) * norm;
            sr = std::tanh(sr * drive) * norm;
        } else if (effect == 4) {
            const int hold = 1 + static_cast<int>(amount * 30.0f);
            if (++crushCounter >= hold) { crushCounter = 0; crushHoldL = sl; crushHoldR = sr; }
            const float levels = std::exp2(16.0f - amount * 12.0f);
            sl = std::round(crushHoldL * levels) / levels;
            sr = std::round(crushHoldR * levels) / levels;
        }
        l[i] = juce::jlimit(-1.0f, 1.0f, sl);
        r[i] = juce::jlimit(-1.0f, 1.0f, sr);
    }
    callbackGate.store(0, std::memory_order_release);
}

void ASR10Processor::getStateInformation(juce::MemoryBlock& block)
{
    juce::ValueTree state("ASR10");
    state.setProperty("instrument", getSelectedInstrument(), nullptr);
    state.setProperty("root", getRootNote(), nullptr);
    state.setProperty("loop", getLoopEnabled(), nullptr);
    state.setProperty("start", getSampleStart(), nullptr);
    state.setProperty("end", getSampleEnd(), nullptr);
    state.setProperty("gain", getVolume(), nullptr);
    state.setProperty("attack", getAttack(), nullptr);
    state.setProperty("release", getRelease(), nullptr);
    state.setProperty("pitch", getPitch(), nullptr);
    state.setProperty("filter", getFilter(), nullptr);
    state.setProperty("fx", getFxType(), nullptr);
    state.setProperty("fxAmount", getFxAmount(), nullptr);
    state.setProperty("tempo", getTempo(), nullptr);
    state.setProperty("chop", getChopMode(), nullptr);
    {
        const juce::ScopedLock lock(nonAudioStateLock);
        state.setProperty("bankName", bankName, nullptr);
        for (int i=0;i<instruments;++i) {
            juce::ValueTree instrument("Instrument");
            const auto& s = instrumentSettings[static_cast<size_t>(i)];
            instrument.setProperty("index",i,nullptr);
            instrument.setProperty("path",paths[static_cast<size_t>(i)],nullptr);
            instrument.setProperty("root",s.rootNote.load(),nullptr);
            instrument.setProperty("loop",s.loop.load(),nullptr);
            instrument.setProperty("start",s.sampleStart.load(),nullptr);
            instrument.setProperty("end",s.sampleEnd.load(),nullptr);
            instrument.setProperty("gain",s.gain.load(),nullptr);
            instrument.setProperty("attack",s.attack.load(),nullptr);
            instrument.setProperty("release",s.release.load(),nullptr);
            instrument.setProperty("pitch",s.pitch.load(),nullptr);
            instrument.setProperty("filter",s.filter.load(),nullptr);
            instrument.setProperty("fx",s.fxType.load(),nullptr);
            instrument.setProperty("fxAmount",s.fxAmount.load(),nullptr);
            for (int j = 0; j <= 16; ++j)
                instrument.setProperty("chop" + juce::String(j), getChopPoint(i,j), nullptr);
            state.addChild(instrument,-1,nullptr);
        }
    }
    for (int i=0;i<16;++i) {
        auto step = juce::ValueTree("Step");
        step.setProperty("index",i,nullptr);
        step.setProperty("lo",juce::String::toHexString(static_cast<juce::int64>(steps[static_cast<size_t>(i)].notesLow.load())),nullptr);
        step.setProperty("hi",juce::String::toHexString(static_cast<juce::int64>(steps[static_cast<size_t>(i)].notesHigh.load())),nullptr);
        state.addChild(step,-1,nullptr);
    }
    auto xml = state.createXml();
    AudioProcessor::copyXmlToBinary(*xml,block);
}

void ASR10Processor::setStateInformation(const void* data, int size)
{
    auto xml = AudioProcessor::getXmlFromBinary(data,size);
    if (!xml || !xml->hasTagName("ASR10")) return;
    {
        const juce::ScopedLock lock(nonAudioStateLock);
        pendingState = xml->toString();
    }
    // Hosts may restore state directly on the message thread, including before
    // their dispatch loop runs. Apply there immediately so sample slots are
    // available on return; other threads retain deferred, non-audio file I/O.
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        mm != nullptr && mm->isThisTheMessageThread())
    {
        cancelPendingUpdate();
        handleAsyncUpdate();
    }
    else
    {
        triggerAsyncUpdate();
    }
}

void ASR10Processor::handleAsyncUpdate()
{
    juce::String source;
    {
        const juce::ScopedLock lock(nonAudioStateLock);
        source.swapWith(pendingState);
    }
    auto xml = juce::parseXML(source);
    if (!xml || !xml->hasTagName("ASR10")) return;
    const auto state = juce::ValueTree::fromXml(*xml);
    // Legacy sessions store the selected instrument's controls on the root node.
    // Seed all slots with safe defaults, then apply those values to the old
    // selected slot. Per-instrument properties below override when available.
    const int previousSelected = juce::jlimit(0, instruments-1,
        static_cast<int>(state.getProperty("instrument",0)));
    for (int i = 0; i < instruments; ++i) {
        auto& s = instrumentSettings[static_cast<size_t>(i)];
        const bool legacySelected = i == previousSelected;
        s.rootNote.store(legacySelected ? juce::jlimit(0,127,static_cast<int>(state.getProperty("root",60))) : 60);
        s.loop.store(legacySelected && static_cast<bool>(state.getProperty("loop",false)));
        s.sampleStart.store(legacySelected ? clamp01(static_cast<float>(state.getProperty("start",0.0))) : 0.0f);
        s.sampleEnd.store(legacySelected ? finiteRange(static_cast<float>(state.getProperty("end",1.0)),0.0f,1.0f,1.0f) : 1.0f);
        s.gain.store(legacySelected ? clamp01(static_cast<float>(state.getProperty("gain",0.8))) : 0.8f);
        s.attack.store(legacySelected ? finiteRange(static_cast<float>(state.getProperty("attack",0.005)),0.0f,10.0f,0.005f) : 0.005f);
        s.release.store(legacySelected ? finiteRange(static_cast<float>(state.getProperty("release",0.25)),0.001f,20.0f,0.25f) : 0.25f);
        s.pitch.store(legacySelected ? finiteRange(static_cast<float>(state.getProperty("pitch",0.0)),-36.0f,36.0f,0.0f) : 0.0f);
        s.filter.store(legacySelected ? clamp01(static_cast<float>(state.getProperty("filter",1.0))) : 1.0f);
        s.fxType.store(legacySelected ? juce::jlimit(0,4,static_cast<int>(state.getProperty("fx",0))) : 0);
        s.fxAmount.store(legacySelected ? clamp01(static_cast<float>(state.getProperty("fxAmount",0.3))) : 0.3f);
        std::array<float,17> equal{};
        for (int j = 0; j <= 16; ++j) equal[static_cast<size_t>(j)] = static_cast<float>(j)/16.0f;
        replaceChopPoints(i, equal);
    }
    setTempo(static_cast<float>(state.getProperty("tempo",120.0)));
    setChopMode(static_cast<bool>(state.getProperty("chop",false)));
    {
        const juce::ScopedLock lock(nonAudioStateLock);
        bankName = state.getProperty("bankName", "INIT BANK").toString().substring(0, 64);
    }
    for (auto child : state) {
        if (child.hasType("Instrument")) {
            const int i = static_cast<int>(child.getProperty("index",-1));
            if (i < 0 || i >= instruments) continue;
            auto& s = instrumentSettings[static_cast<size_t>(i)];
            if (child.hasProperty("root"))
                s.rootNote.store(juce::jlimit(0,127,static_cast<int>(child.getProperty("root"))));
            if (child.hasProperty("loop")) s.loop.store(static_cast<bool>(child.getProperty("loop")));
            const float restoredStart = child.hasProperty("start")
                ? finiteRange(static_cast<float>(child.getProperty("start")),0.0f,1.0f,0.0f) : s.sampleStart.load();
            const float restoredEnd = child.hasProperty("end")
                ? finiteRange(static_cast<float>(child.getProperty("end")),0.0f,1.0f,1.0f) : s.sampleEnd.load();
            s.sampleStart.store(restoredEnd > restoredStart ? restoredStart : 0.0f);
            s.sampleEnd.store(restoredEnd > restoredStart ? restoredEnd : 1.0f);
            if (child.hasProperty("gain")) s.gain.store(clamp01(static_cast<float>(child.getProperty("gain"))));
            if (child.hasProperty("attack")) s.attack.store(finiteRange(static_cast<float>(child.getProperty("attack")),0.0f,10.0f,0.005f));
            if (child.hasProperty("release")) s.release.store(finiteRange(static_cast<float>(child.getProperty("release")),0.001f,20.0f,0.25f));
            if (child.hasProperty("pitch")) s.pitch.store(finiteRange(static_cast<float>(child.getProperty("pitch")),-36.0f,36.0f,0.0f));
            if (child.hasProperty("filter")) s.filter.store(clamp01(static_cast<float>(child.getProperty("filter"))));
            if (child.hasProperty("fx")) s.fxType.store(juce::jlimit(0,4,static_cast<int>(child.getProperty("fx"))));
            if (child.hasProperty("fxAmount")) s.fxAmount.store(clamp01(static_cast<float>(child.getProperty("fxAmount"))));
            std::array<float,17> restored{};
            bool valid = true;
            for (int j = 0; j <= 16; ++j) {
                const auto name = "chop" + juce::String(j);
                const float value = static_cast<float>(child.getProperty(name, static_cast<double>(j)/16.0));
                restored[static_cast<size_t>(j)] = value;
                if (!std::isfinite(value) || value < 0.0f || value > 1.0f
                    || (j == 0 && value != 0.0f) || (j == 16 && value != 1.0f)
                    || (j > 0 && value < restored[static_cast<size_t>(j-1)])) valid = false;
            }
            if (valid) replaceChopPoints(i, restored);
            const auto path = child.getProperty("path").toString();
            if (path.isNotEmpty()) loadSample(i,juce::File(path));
            else if (i > 0) {
                clips[static_cast<size_t>(i)].store(nullptr,std::memory_order_release);
                const juce::ScopedLock lock(nonAudioStateLock);
                paths[static_cast<size_t>(i)].clear();
                names[static_cast<size_t>(i)].clear();
            }
        } else if (child.hasType("Step")) {
            const int i = static_cast<int>(child.getProperty("index",-1));
            if (i < 0 || i >= 16) continue;
            steps[static_cast<size_t>(i)].notesLow.store(static_cast<uint64_t>(child.getProperty("lo").toString().getHexValue64()));
            steps[static_cast<size_t>(i)].notesHigh.store(static_cast<uint64_t>(child.getProperty("hi").toString().getHexValue64()));
        }
    }
    selectInstrument(previousSelected);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ASR10Processor();
}
