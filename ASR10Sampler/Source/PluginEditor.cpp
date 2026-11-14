#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float W = 1600.0f, H = 650.0f;
const auto ink = juce::Colour::fromRGB (185, 190, 186);
const auto muted = juce::Colour::fromRGB (103, 109, 107);
const auto gold = juce::Colour::fromRGB (196, 159, 75);
const auto green = juce::Colour::fromRGB (123, 210, 155);
const auto teal = juce::Colour::fromRGB (96, 180, 174);
const auto dark = juce::Colour::fromRGB (30, 34, 35);
const auto panel = juce::Colour::fromRGB (43, 46, 47);
const char* pages[] = { "INSTRUMENT", "SEQ / SONG", "SYSTEM / MIDI", "EFFECTS" };
const char* parameters[] = { "VOLUME", "ATTACK", "RELEASE", "FILTER", "PITCH",
                             "SAMPLE START", "SAMPLE END", "ROOT NOTE",
                             "LOOP", "FX TYPE", "FX AMOUNT", "TEMPO" };
const char* bankLabels[] = { "ENV 1", "ENV 2", "ENV 3", "PITCH", "FILTERS",
                             "AMP", "LFO", "WAVE", "LAYER", "TRACK" };
bool isBlack (int note)
{
    const auto p = note % 12;
    return p == 1 || p == 3 || p == 6 || p == 8 || p == 10;
}
juce::String pad22 (juce::String s)
{
    if (s.length() > 22) s = s.substring (0, 22);
    return s.paddedRight (' ', 22);
}
}

ASR10Editor::ASR10Editor (ASR10Processor& p) : juce::AudioProcessorEditor (&p), processor (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setSize (1600, 650);
    setResizable (true, true);
    setResizeLimits (960, 390, 2560, 1040);
    startTimerHz (24);
    // The standalone app uses the last explicitly loaded bank preference,
    // which may differ from a stale host auto-restore snapshot. AU/VST3 hosts
    // still restore the project state and never read this global preference.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        const auto preference = lastBankPreferenceFile();
        if (preference.existsAsFile() && preference.getSize() < 8192)
        {
            const juce::File previousBank (preference.loadFileAsString().trim());
            if (previousBank.existsAsFile() && previousBank.hasFileExtension (".json"))
                loadBankFile (previousBank);
        }
    }
}

ASR10Editor::~ASR10Editor()
{
    stopTimer();
    if (heldMouseNote >= 0) processor.releaseNote (heldMouseNote);
    for (int k = 0; k < 128; ++k)
        if (heldComputerNotes[k] != 0) processor.releaseNote (heldComputerNotes[k] - 1);
}

void ASR10Editor::timerCallback()
{
    levelLeft *= 0.88f;
    levelRight *= 0.88f;
    repaint();
}

juce::Point<float> ASR10Editor::localPoint (const juce::Point<float>& p) const
{
    return { p.x * W / (float) getWidth(), p.y * H / (float) getHeight() };
}

void ASR10Editor::label (juce::Graphics& g, const juce::String& t, float x, float y,
                          float w, float h, float size, juce::Colour c, juce::Justification j)
{
    g.setColour (c);
    g.setFont (juce::Font (juce::FontOptions (size).withStyle ("Bold")));
    g.drawFittedText (t, juce::Rectangle<int> ((int)x, (int)y, (int)w, (int)h), j, 1, 0.65f);
}

void ASR10Editor::button (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text,
                          const juce::String& action, int value, bool selected, bool led, float size)
{
    const auto keyColour = selected ? juce::Colour::fromRGB (73, 80, 77) : juce::Colour::fromRGB (37, 41, 42);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (r.translated (1.0f, 3.0f), 2.0f);
    g.setColour (juce::Colour::fromRGB (95, 99, 96));
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (keyColour);
    g.fillRoundedRectangle (r.reduced (2.0f), 1.5f);
    g.setColour (juce::Colour::fromRGB (107, 111, 108));
    g.drawLine (r.getX() + 2, r.getY() + 2, r.getRight() - 2, r.getY() + 2, 0.75f);
    if (led)
    {
        g.setColour (selected ? green : juce::Colour::fromRGB (54, 70, 60));
        g.fillEllipse (r.getX() + 5, r.getY() + 5, 5, 5);
    }
    label (g, text, r.getX() + 2, r.getY() + 1, r.getWidth() - 4, r.getHeight() - 2,
           size, selected ? juce::Colours::white : ink);
    hits.push_back ({ r, action, value });
}

void ASR10Editor::drawChassis (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (14, 17, 18));
    juce::ColourGradient metal (juce::Colour::fromRGB (52, 56, 57), 0, 0,
                                juce::Colour::fromRGB (24, 28, 29), 0, 322, false);
    metal.addColour (0.18, juce::Colour::fromRGB (45, 49, 50));
    metal.addColour (0.76, juce::Colour::fromRGB (36, 39, 40));
    g.setGradientFill (metal);
    g.fillRect (0.0f, 0.0f, W, 324.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    for (int i = 0; i < 324; i += 3) g.drawHorizontalLine (i, 0, W);
    g.setColour (gold);
    g.drawLine (14, 309, 1588, 309, 1.3f);
    g.setColour (juce::Colour::fromRGB (11, 13, 14));
    g.fillRect (0, 321, (int)W, 329);
    g.setColour (juce::Colour::fromRGB (89, 92, 88));
    g.drawLine (0, 322, W, 322, 2);
    // Floppy mechanism, left cheek and screw heads.
    g.setColour (juce::Colour::fromRGB (17, 20, 21));
    g.fillRoundedRectangle (juce::Rectangle<float> (21, 27, 178, 76), 3);
    g.setColour (muted);
    g.drawRect (juce::Rectangle<float> (21, 27, 178, 76), 1);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (juce::Rectangle<float> (35, 53, 145, 13), 2);
    g.setColour (juce::Colour::fromRGB (85, 91, 94));
    g.fillRect (35, 72, 27, 10);
    label (g, "3.5\" DISK DRIVE", 41, 33, 133, 15, 10, muted);
    for (const auto x : { 12.0f, 1586.0f })
        for (const auto y : { 12.0f, 299.0f })
        {
            g.setColour (juce::Colour::fromRGB (100, 102, 100));
            g.fillEllipse (x, y, 6, 6);
            g.setColour (juce::Colours::black);
            g.drawLine (x + 1, y + 3, x + 5, y + 3, 1);
        }
}

void ASR10Editor::drawSlider (juce::Graphics& g, juce::Rectangle<float> r,
                              float value, const juce::String& action)
{
    value = juce::jlimit (0.0f, 1.0f, value);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r, 3);
    g.setColour (muted);
    g.drawRoundedRectangle (r, 3, 1.0f);
    g.setColour (juce::Colour::fromRGB (13, 14, 14));
    g.fillRect (r.getCentreX() - 2, r.getY() + 5, 4.0f, r.getHeight() - 10);
    const auto y = r.getBottom() - 10 - value * (r.getHeight() - 20);
    g.setColour (juce::Colour::fromRGB (131, 135, 130));
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() - 7, y - 8, r.getWidth() + 14, 16), 2);
    g.setColour (juce::Colours::black);
    g.drawLine (r.getX() - 4, y, r.getRight() + 4, y, 1.3f);
    hits.push_back ({ r.expanded (13, 5), action, 0 });
}

void ASR10Editor::drawMeter (juce::Graphics& g, float x, float top, float v, const juce::String& t)
{
    label (g, t, x - 3, top - 14, 15, 13, 10, ink);
    for (int i = 0; i < 10; ++i)
    {
        const auto c = i >= 8 ? juce::Colours::red : (i >= 6 ? gold : green);
        g.setColour (c.withAlpha (v >= (i + 1) / 10.0f ? 0.95f : 0.12f));
        g.fillRect (x, top + (9 - i) * 10, 9.0f, 7.0f);
    }
}

void ASR10Editor::drawScreen (juce::Graphics& g)
{
    const juce::Rectangle<float> shell (817, 38, 343, 132);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (shell, 6);
    g.setColour (juce::Colour::fromRGB (103, 110, 109));
    g.drawRoundedRectangle (shell, 6, 2);
    const juce::Rectangle<float> glass (829, 52, 319, 101);
    juce::ColourGradient lcd (juce::Colour::fromRGB (59, 120, 113), 831, 54,
                              juce::Colour::fromRGB (24, 61, 64), 1144, 150, false);
    g.setGradientFill (lcd);
    g.fillRoundedRectangle (glass, 3);
    g.setColour (juce::Colour::fromRGB (132, 216, 183).withAlpha (0.28f));
    for (int y = 54; y < 151; y += 3) g.drawHorizontalLine (y, 832, 1145);
    g.setColour (green);
    g.setFont (juce::Font (juce::FontOptions ("Monospaced", 17, juce::Font::plain)));
    if (waveformView)
    {
        const int instrument = processor.getSelectedInstrument();
        g.drawText (pad22 ("WAVE " + juce::String (instrument + 1) + " "
                           + processor.getInstrumentName (instrument)),
                    840, 65, 298, 29, juce::Justification::centredLeft, false);

        const auto peaks = processor.getSelectedWaveformPeaks();
        constexpr float x0 = 839.0f, width = 299.0f, centreY = 125.0f;
        constexpr float maxHeight = 20.0f;
        const auto trimStart = juce::jlimit (0.0f, 1.0f, processor.getSampleStart());
        const auto trimEnd = juce::jlimit (0.0f, 1.0f, processor.getSampleEnd());
        const auto playableWidth = width * juce::jmax (0.0f, trimEnd - trimStart);

        g.setColour (green.withAlpha (0.55f));
        for (int i = 0; i < 128; ++i)
        {
            const float x = x0 + width * (i + 0.5f) / 128.0f;
            const float h = juce::jlimit (0.0f, 1.0f, peaks[(size_t) i]) * maxHeight;
            g.drawLine (x, centreY - h, x, centreY + h, 1.4f);
        }
        g.setColour (juce::Colour::fromRGB (3, 18, 23).withAlpha (0.72f));
        g.fillRect (x0, 102.0f, width * trimStart, 48.0f);
        g.fillRect (x0 + width * trimEnd, 102.0f, width * (1.0f - trimEnd), 48.0f);
        g.setColour (green.brighter (0.6f));
        g.drawVerticalLine (juce::roundToInt (x0 + width * trimStart), 99.0f, 150.0f);
        g.setColour (gold);
        g.drawVerticalLine (juce::roundToInt (x0 + width * trimEnd), 99.0f, 150.0f);
        if (processor.getChopMode())
        {
            const int activeSlice = processor.getLastSlice();
            if (juce::isPositiveAndBelow (activeSlice, 16))
            {
                const float left = x0 + width * trimStart
                                   + playableWidth * processor.getChopPoint (instrument, activeSlice);
                const float right = x0 + width * trimStart
                                   + playableWidth * processor.getChopPoint (instrument, activeSlice + 1);
                g.setColour (gold.withAlpha (0.22f));
                g.fillRect (left, 101.0f, juce::jmax (0.0f, right - left), 49.0f);
            }
            for (int marker = 0; marker <= 16; ++marker)
            {
                const float x = x0 + width * trimStart
                    + playableWidth * processor.getChopPoint (instrument, marker);
                g.setColour (marker == selectedChopBoundary ? green.brighter (0.75f)
                                                          : gold.withAlpha (0.53f));
                g.drawVerticalLine (juce::roundToInt (x), 101.0f, 149.0f);
            }
        }
        label (g, "DRAG TRIM • OPTION-DRAG CHOP MARKER", 839, 157, 297, 10, 8, muted);
        return;
    }
    juce::String line1 = "I" + juce::String (processor.getSelectedInstrument() + 1)
                       + " " + processor.getBankName();
    juce::String line2 = numericEntry ? ">" + juce::String (numericValue) + " _"
                                      : (editingValue ? parameterName() + ": " + parameterValue() : message);
    if (processor.getChopMode() && !numericEntry && !editingValue && processor.getLastSlice() >= 0)
        line2 = "CHOP " + juce::String (processor.getLastSlice() + 1) + "/16 " + message;
    g.drawText (pad22 (line1), 840, 65, 298, 29, juce::Justification::centredLeft, false);
    g.drawText (pad22 (line2), 840, 108, 298, 29, juce::Justification::centredLeft, false);
    label (g, "22 CHARACTER - FLUORESCENT DISPLAY", 839, 157, 297, 10, 8, muted);
}

void ASR10Editor::drawControlPanel (juce::Graphics& g)
{
    label (g, "ENSONIQ", 209, 13, 146, 24, 20, gold, juce::Justification::centredLeft);
    label (g, "ADVANCED SAMPLING RECORDER", 376, 12, 264, 18, 10, muted,
           juce::Justification::centredLeft);
    label (g, "PITCH", 25, 119, 73, 15, 11, ink);
    label (g, "MODULATION", 105, 119, 95, 15, 11, ink);
    drawSlider (g, { 43, 143, 26, 103 }, 0.5f + wheelPitch / 24.0f, "pitchwheel");
    drawSlider (g, { 131, 143, 26, 103 }, wheelMod, "modwheel");
    label (g, "VOLUME", 211, 47, 76, 16, 11, ink);
    drawSlider (g, { 234, 76, 24, 173 }, processor.getVolume(), "volume");

    label (g, "MODE", 300, 47, 72, 18, 11, gold);
    const char* modes[] = { "LOAD", "COMMAND", "EDIT" };
    for (int i = 0; i < 3; ++i)
        button (g, { 300.0f, 79.0f + i * 54.0f, 71, 40 }, modes[i], "mode", i,
                mode == modes[i], true, 9);
    label (g, "PAGES", 390, 47, 77, 18, 11, gold);
    for (int i = 0; i < 4; ++i)
        button (g, { 381.0f, 77.0f + i * 49.0f, 90, 36 },
                juce::String (pages[i]), "page", i, i == page, true, 8);

    label (g, "PARAMETER / NUMERIC", 480, 47, 150, 16, 10, gold);
    for (int i = 0; i < 10; ++i)
    {
        const int row = i / 3, col = i % 3;
        const float x = 479.0f + col * 48.0f;
        const float y = 76.0f + row * 48.0f;
        juce::String text = bankLabels[i];
        if (i == 9) text = "TRACK 0";
        button (g, { x, y, 45, 39 }, text, "param", i, parameter == i, false, 8);
    }
    label (g, "MODE: COMMAND FOR DIGITS", 477, 276, 149, 15, 8, muted);
    label (g, "DATA", 632, 46, 56, 18, 10, gold);
    drawSlider (g, { 650, 78, 22, 167 }, getParameterNormalized(), "data");
    label (g, "CURSOR", 705, 47, 90, 18, 10, gold);
    button (g, { 731, 81, 35, 34 }, "^", "arrow", 1);
    button (g, { 731, 159, 35, 34 }, "v", "arrow", -1);
    button (g, { 690, 120, 35, 34 }, "<", "arrow", -10);
    button (g, { 772, 120, 35, 34 }, ">", "arrow", 10);
    button (g, { 691, 213, 56, 38 }, "CANCEL", "cancel", 0, false, false, 9);
    button (g, { 752, 213, 56, 38 }, "ENTER", "enter", 0, false, false, 9);
    label (g, parameterName(), 688, 262, 126, 15, 9, muted);
    label (g, parameterValue(), 688, 279, 126, 26, 15, green);
    drawScreen (g);

    label (g, "INSTRUMENT / SEQUENCE TRACKS", 818, 178, 341, 15, 10, gold);
    for (int i = 0; i < 8; ++i)
        button (g, { 820.0f + i * 42.0f, 196, 38, 36 },
                juce::String (i + 1), "instrument", i,
                processor.getSelectedInstrument() == i, true, 12);
    label (g, "SHIFT-CLICK TRACK TO LOAD", 818, 238, 212, 15, 9, muted);

    button (g, { 822, 265, 72, 35 }, "LOOP", "loop", 0, processor.getLoopEnabled(), true, 9);
    button (g, { 901, 265, 74, 35 }, "CHOP", "chop", 0, processor.getChopMode(), true, 9);
    button (g, { 981, 265, 84, 35 }, "LOAD WAV", "load", 0, false, true, 9);
    button (g, { 1071, 265, 85, 35 }, "BANKS", "bank", 0, false, true, 10);
    label (g, "LOAD + < >: NEXT", 1062, 303, 105, 11, 8, muted);
    label (g, juce::String (processor.getTempo(), 1) + " BPM", 1043, 235, 115, 19, 11, ink);

    label (g, "SOURCE / EFFECTS", 1182, 40, 212, 17, 11, gold);
    button (g, { 1180, 69, 103, 37 }, "SOURCE " + juce::String (source + 1), "source", 0,
            source != 0, true, 10);
    button (g, { 1291, 69, 106, 37 }, "FX " + juce::String (processor.getFxType()), "fx", 0,
            processor.getFxType() != 0, true, 11);
    label (g, "LEVEL", 1223, 119, 119, 16, 11, ink);
    drawMeter (g, 1255, 151, levelLeft, "L");
    drawMeter (g, 1277, 151, levelRight, "R");
    label (g, "AUDIO TRACKS", 1313, 144, 95, 16, 10, gold);
    button (g, { 1321, 170, 39, 32 }, "A", "audio", 0, audioTrack == 0, true, 11);
    button (g, { 1367, 170, 39, 32 }, "B", "audio", 1, audioTrack == 1, true, 11);
    label (g, "TRANSPORT", 1190, 238, 206, 16, 10, gold);
    button (g, { 1183, 268, 70, 35 }, "REC", "record", 0,
            processor.isSequenceRecording() || recordArmed, true, 10);
    button (g, { 1258, 268, 69, 35 }, "STOP", "stop", 0, false, true, 10);
    button (g, { 1332, 268, 71, 35 }, "PLAY", "play", 0,
            processor.isSequencePlaying(), true, 10);

    label (g, "ASR-10", 1430, 77, 160, 48, 43, gold);
    label (g, "1992", 1461, 120, 97, 24, 15, muted);
    label (g, "ADVANCED SAMPLING", 1425, 158, 163, 17, 11, ink);
    label (g, "RECORDER", 1438, 177, 140, 17, 11, ink);
    label (g, "16 BIT / 44.1 kHz", 1441, 228, 138, 18, 10, muted);
}

void ASR10Editor::drawKeyboard (juce::Graphics& g)
{
    constexpr float x0 = 204.0f, xEnd = 1596.0f;
    constexpr float y0 = 329.0f, yEnd = 637.0f;
    const auto whiteW = (xEnd - x0) / 36.0f;
    // White notes from C2 to C7 inclusive; the end note C7 is the 36th white.
    for (int n = 0, wi = 0; n < 61; ++n)
    {
        if (isBlack (n)) continue;
        auto r = juce::Rectangle<float> (x0 + wi * whiteW, y0, whiteW - 1.2f, yEnd - y0);
        const bool pressed = heldMouseNote == n + 36;
        g.setGradientFill (juce::ColourGradient (pressed ? juce::Colour::fromRGB (129, 172, 161)
                                                       : juce::Colour::fromRGB (247, 246, 237),
                                                   r.getX(), y0,
                                                   pressed ? juce::Colour::fromRGB (95, 130, 127)
                                                           : juce::Colour::fromRGB (186, 186, 176),
                                                   r.getX(), yEnd, false));
        g.fillRoundedRectangle (r, 2);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r, 2, 1);
        if (n % 12 == 0)
            label (g, "C" + juce::String (n / 12 + 2), r.getX() + 2, 608,
                   whiteW - 5, 16, 10, juce::Colour::fromRGB (80, 80, 76));
        ++wi;
    }
    for (int n = 0, whitesBefore = 0; n < 61; ++n)
    {
        if (! isBlack (n)) { ++whitesBefore; continue; }
        auto r = juce::Rectangle<float> (x0 + (whitesBefore - 0.36f) * whiteW - 12,
                                         y0, whiteW * 0.66f, 186);
        g.setGradientFill (juce::ColourGradient (juce::Colour::fromRGB (46, 49, 49), r.getX(), y0,
                                                juce::Colour::fromRGB (5, 7, 8), r.getX(), y0 + 185, false));
        g.fillRoundedRectangle (r, 3);
        g.setColour (juce::Colour::fromRGB (82, 86, 86));
        g.drawRoundedRectangle (r, 3, 1);
    }
    // When chopping, show the playable slice numbers on the actual keys.
    // MIDI notes 48..63 are C3 to D#4, the sixteen hardware-style triggers.
    if (processor.getChopMode())
    {
        const int last = processor.getLastSlice();
        for (int n = 12; n < 28; ++n)
        {
            const bool blackKey = isBlack (n);
            int whitesBefore = 0;
            for (int k = 0; k < n; ++k)
                if (!isBlack (k))
                    ++whitesBefore;

            const float x = blackKey
                ? x0 + (whitesBefore - 0.36f) * whiteW - 12 + whiteW * 0.33f
                : x0 + whitesBefore * whiteW + (whiteW - 1.2f) * 0.5f;
            const float y = blackKey ? 475.0f : 575.0f;
            const int slice = n - 12;
            const auto active = last == slice;
            const auto badge = juce::Rectangle<float> (x - 13, y, 26, 23);
            g.setColour (active ? green : gold.withAlpha (0.8f));
            g.fillRoundedRectangle (badge, 3);
            label (g, juce::String (slice + 1), badge.getX(), badge.getY() + 1,
                   badge.getWidth(), badge.getHeight() - 1, 11,
                   juce::Colour::fromRGB (13, 20, 19));
        }
    }
    // Instrument cheek at the far left of the keys.
    g.setColour (juce::Colour::fromRGB (31, 34, 34));
    g.fillRect (0, 329, 201, 307);
    g.setColour (gold);
    label (g, "ASR-10", 17, 341, 169, 28, 22, gold);
    label (g, "SAMPLING KEYBOARD", 16, 371, 176, 19, 10, muted);
    label (g, "Z S X D C V G B H N J M", 17, 576, 172, 16, 10, ink);
    label (g, "COMPUTER KEYBOARD", 18, 595, 166, 18, 9, muted);
    label (g, "61 KEYS", 20, 611, 160, 18, 11, ink);
    if (processor.getChopMode())
        label (g, "16-SLICE LIVE MODE", 14, 542, 179, 18, 10, gold);
}

void ASR10Editor::paint (juce::Graphics& g)
{
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale ((float)getWidth() / W, (float)getHeight() / H));
    hits.clear();
    drawChassis (g);
    drawKeyboard (g);
    drawControlPanel (g);
}

void ASR10Editor::resized() { repaint(); }

int ASR10Editor::noteAt (juce::Point<float> p) const
{
    constexpr float x0 = 204.0f, xEnd = 1596.0f;
    if (p.x < x0 || p.x >= xEnd || p.y < 329 || p.y > 637) return -1;
    const auto whiteW = (xEnd - x0) / 36.0f;
    if (p.y < 515)
    {
        for (int n = 0, before = 0; n < 61; ++n)
        {
            if (! isBlack (n)) { ++before; continue; }
            const auto x = x0 + (before - 0.36f) * whiteW - 12;
            if (p.x >= x && p.x < x + whiteW * 0.66f) return n + 36;
        }
    }
    const int wi = juce::jlimit (0, 35, (int)((p.x - x0) / whiteW));
    int found = 0;
    for (int n = 0; n < 61; ++n)
        if (! isBlack (n) && found++ == wi) return n + 36;
    return -1;
}

void ASR10Editor::playMouseKey (juce::Point<float> p)
{
    const int note = noteAt (p);
    if (note == heldMouseNote) return;
    if (heldMouseNote >= 0) processor.releaseNote (heldMouseNote);
    heldMouseNote = note;
    if (note >= 0)
    {
        processor.triggerNote (note, 0.9f);
        levelLeft = levelRight = 0.9f;
        display ("KEY " + juce::String (note));
    }
    repaint();
}

void ASR10Editor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = localPoint (e.position);
    if (waveformView && p.x >= 839.0f && p.x <= 1138.0f
        && p.y >= 99.0f && p.y <= 151.0f)
    {
        const float x = juce::jlimit (0.0f, 1.0f, (p.x - 839.0f) / 299.0f);
        const float start = processor.getSampleStart();
        const float end = processor.getSampleEnd();
        if (e.mods.isAltDown())
        {
            const int instrument = processor.getSelectedInstrument();
            const float normalized = juce::jlimit (0.0f, 1.0f,
                (x - start) / juce::jmax (0.00001f, end - start));
            float distance = 2.0f;
            for (int boundary = 1; boundary < 16; ++boundary)
            {
                const float d = std::abs (normalized - processor.getChopPoint (instrument, boundary));
                if (d < distance)
                {
                    distance = d;
                    selectedChopBoundary = boundary;
                }
            }
            activeDrag = "wave-marker";
            display ("CHOP MARKER " + juce::String (selectedChopBoundary));
            repaint();
            return;
        }
        // Choose the closest trim edge; Shift explicitly chooses the end.
        activeDrag = e.mods.isShiftDown() || std::abs (x - end) < std::abs (x - start)
            ? "wave-end" : "wave-start";
        const float minSpan = 0.005f;
        if (activeDrag == "wave-start")
        {
            processor.setSampleStart (juce::jmin (x, end - minSpan));
            display ("TRIM START " + juce::String (processor.getSampleStart() * 100.0f, 1) + "%");
        }
        else
        {
            processor.setSampleEnd (juce::jmax (x, start + minSpan));
            display ("TRIM END " + juce::String (processor.getSampleEnd() * 100.0f, 1) + "%");
        }
        repaint();
        return;
    }
    if (noteAt (p) >= 0) { playMouseKey (p); return; }
    for (auto i = hits.rbegin(); i != hits.rend(); ++i)
        if (i->bounds.contains (p))
        {
            if (i->action == "data" || i->action == "volume" ||
                i->action == "pitchwheel" || i->action == "modwheel")
            {
                activeDrag = i->action;
                lastSliderY = p.y;
                dragStart = i->action == "volume" ? processor.getVolume() :
                            i->action == "data" ? getParameterNormalized() :
                            i->action == "modwheel" ? wheelMod : 0.5f + wheelPitch / 24.0f;
                mouseDrag (e);
            }
            else activate (*i, e.mods);
            return;
        }
}

void ASR10Editor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = localPoint (e.position);
    if (heldMouseNote >= 0) { playMouseKey (p); return; }
    if (activeDrag.isEmpty()) return;
    if (activeDrag == "wave-marker")
    {
        const float normalizedX = juce::jlimit (0.0f, 1.0f,
            (p.x - 839.0f) / 299.0f);
        const float start = processor.getSampleStart();
        const float end = processor.getSampleEnd();
        const float regionPos = juce::jlimit (0.0f, 1.0f,
            (normalizedX - start) / juce::jmax (0.00001f, end - start));
        const int instrument = processor.getSelectedInstrument();
        const float low = processor.getChopPoint (instrument, selectedChopBoundary - 1);
        const float high = processor.getChopPoint (instrument, selectedChopBoundary + 1);
        const float edited = juce::jlimit (low, high, regionPos);
        if (processor.setChopPoint (instrument, selectedChopBoundary, edited))
            display ("CHOP " + juce::String (selectedChopBoundary) + " = "
                    + juce::String (edited * 100.0f, 1) + "%");
        repaint();
        return;
    }
    if (activeDrag == "wave-start" || activeDrag == "wave-end")
    {
        const float x = juce::jlimit (0.0f, 1.0f, (p.x - 839.0f) / 299.0f);
        if (activeDrag == "wave-start")
            processor.setSampleStart (juce::jmax (0.0f,
                juce::jmin (x, processor.getSampleEnd() - 0.005f)));
        else
            processor.setSampleEnd (juce::jmin (1.0f,
                juce::jmax (x, processor.getSampleStart() + 0.005f)));
        display ((activeDrag == "wave-start" ? "TRIM START " : "TRIM END ")
            + juce::String ((activeDrag == "wave-start" ? processor.getSampleStart()
                                                          : processor.getSampleEnd()) * 100.0f, 1) + "%");
        repaint();
        return;
    }
    const float travel = activeDrag == "volume" ? 140.0f : activeDrag == "data" ? 126.0f : 95.0f;
    const float value = juce::jlimit (0.0f, 1.0f, dragStart + (lastSliderY - p.y) / travel);
    if (activeDrag == "volume") processor.setVolume (value);
    else if (activeDrag == "data") setParameterNormalized (value);
    else if (activeDrag == "modwheel") { wheelMod = value; display ("MOD WHEEL " + juce::String ((int)(value * 100)) + "%"); }
    else if (activeDrag == "pitchwheel")
    {
        wheelPitch = juce::roundToInt ((value - 0.5f) * 24);
        processor.setPitch ((float)wheelPitch);
        display ("PITCH " + juce::String (wheelPitch) + " SEMI");
    }
    editingValue = true;
    repaint();
}

void ASR10Editor::mouseUp (const juce::MouseEvent&)
{
    if (heldMouseNote >= 0) { processor.releaseNote (heldMouseNote); heldMouseNote = -1; }
    if (activeDrag == "pitchwheel")
    {
        wheelPitch = 0;
        processor.setPitch (0);
    }
    activeDrag.clear();
    repaint();
}

void ASR10Editor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = localPoint (e.position);
    for (const auto& h : hits)
        if (h.bounds.contains (p) && h.action == "instrument")
        { loadInstrument (h.value); return; }
}

void ASR10Editor::activate (const Hit& h, const juce::ModifierKeys& mods, bool)
{
    if (h.action == "instrument")
    {
        processor.selectInstrument (h.value);
        if (mods.isShiftDown() || mode == "LOAD") loadInstrument (h.value);
        else display ("INSTR " + juce::String (h.value + 1) + ": " + processor.getInstrumentName (h.value));
    }
    else if (h.action == "load") loadInstrument (processor.getSelectedInstrument());
    else if (h.action == "bank") loadBank();
    else if (h.action == "chop")
    {
        processor.setChopMode (! processor.getChopMode());
        editingValue = false;
        display (processor.getChopMode() ? "CHOP 16: C3-D#4" : "CHOP OFF");
    }
    else if (h.action == "mode")
    {
        static constexpr const char* modeNames[] = { "LOAD", "COMMAND", "EDIT" };
        mode = modeNames[juce::jlimit (0, 2, h.value)];
        display (mode + " MODE");
        editingValue = (h.value == 2);
    }
    else if (h.action == "page") { page = h.value; display (pages[page]); }
    else if (h.action == "param")
    {
        if (mode == "COMMAND")
        {
            numericValue = juce::jlimit (0, 999, (numericEntry ? numericValue * 10 : 0) + h.value);
            numericEntry = true;
        }
        else showParameter (h.value);
    }
    else if (h.action == "arrow")
    {
        if (mode == "LOAD" && std::abs (h.value) == 10)
            stepBank (h.value > 0 ? 1 : -1);
        else
            adjustParameter ((float)h.value / 100.0f);
    }
    else if (h.action == "cancel") { numericEntry = false; editingValue = false; display ("CANCEL"); }
    else if (h.action == "enter")
    {
        if (numericEntry) { setParameterNormalized (juce::jlimit (0.0f, 1.0f, numericValue / 100.0f)); numericEntry = false; }
        editingValue = true;
        display (parameterName() + " " + parameterValue());
    }
    else if (h.action == "loop") { processor.setLoopEnabled (! processor.getLoopEnabled()); showParameter (8); }
    else if (h.action == "fx") { fxSelection = (processor.getFxType() + 1) % 5; processor.setFxType (fxSelection); showParameter (9); }
    else if (h.action == "source") { source = (source + 1) % 3; display ("INPUT SOURCE " + juce::String (source + 1)); }
    else if (h.action == "audio") { audioTrack = h.value; display ("AUDIO TRACK " + juce::String (h.value == 0 ? "A" : "B")); }
    else if (h.action == "record")
    {
        recordArmed = true;
        processor.startSequence (true);
        display ("SEQUENCE RECORDING");
    }
    else if (h.action == "play") { processor.startSequence (false); display ("SEQUENCE PLAY"); }
    else if (h.action == "stop") { processor.stopSequence(); recordArmed = false; display ("SEQUENCE STOP"); }
    repaint();
}

void ASR10Editor::loadInstrument (int i)
{
    processor.selectInstrument (i);
    chooser = std::make_unique<juce::FileChooser> ("Load an instrument sample",
                                                   juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [this, i] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f.existsAsFile())
            display (processor.loadSample (i, f) ? "LOADED: " + f.getFileNameWithoutExtension()
                                                 : "LOAD FAILED: " + f.getFileName());
        repaint();
    });
}

void ASR10Editor::loadBank()
{
    if (! refreshBankIndex())
    {
        browseBankFile();
        return;
    }
    juce::PopupMenu menu;
    juce::PopupMenu eras, exercises, newPacks, realSongs;
    std::array<juce::PopupMenu, 4> realSongLetters;
    std::array<int, 4> realLetterCounts{};
    int eraCount = 0, exerciseCount = 0, flipCount = 0, realCount = 0;
    for (int i = 0; i < (int) availableBanks.size(); ++i)
    {
        const auto selected = currentBankIndex == i;
        const auto id = i + 1;
        const auto& title = availableBankTitles[(size_t) i];
        if (availableBankRealSongs[(size_t) i])
        {
            const auto songName = title.contains ("—")
                ? title.fromLastOccurrenceOf ("—", false, false).trim()
                : title.fromLastOccurrenceOf (":", false, false).trim();
            const auto letter = songName.isNotEmpty()
                ? juce::CharacterFunctions::toUpperCase (songName[0]) : (juce::juce_wchar) '?';
            const int section = letter < 'G' ? 0 : (letter < 'M' ? 1 : (letter < 'S' ? 2 : 3));
            realSongLetters[(size_t) section].addItem (
                id, songName + "  (" + juce::String (id) + ")", true, selected);
            ++realLetterCounts[(size_t) section];
            ++realCount;
        }
        else
        {
            auto& category = i < 12 ? eras : (i < 19 ? exercises : newPacks);
            category.addItem (id, title + "  (" + juce::String (id) + ")", true, selected);
            if (i < 12) ++eraCount;
            else if (i < 19) ++exerciseCount;
            else ++flipCount;
        }
    }
    if (realCount > 0)
    {
        const juce::String sections[] { "A–F", "G–L", "M–R", "S–Z" };
        for (int i = 0; i < 4; ++i)
            if (realLetterCounts[(size_t) i] > 0)
                realSongs.addSubMenu (sections[i] + "  /  "
                    + juce::String (realLetterCounts[(size_t) i]) + " SONGS",
                    realSongLetters[(size_t) i]);
        menu.addSubMenu ("REAL SONGS  /  " + juce::String (realCount) + " TRACKS", realSongs);
    }
    if (eraCount > 0)
        menu.addSubMenu ("ERA STUDIES  /  " + juce::String (eraCount) + " BANKS", eras);
    if (exerciseCount > 0)
        menu.addSubMenu ("SONG-STYLE PRACTICE  /  " + juce::String (exerciseCount) + " BANKS", exercises);
    if (flipCount > 0)
        menu.addSubMenu ("NEW PACK FLIPS  /  " + juce::String (flipCount) + " BANKS", newPacks);
    menu.addSeparator();
    menu.addItem (10002, "IMPORT MY SONG AUDIO...");
    menu.addItem (10003, "SAVE MY SONG CHOP EDITS");
    menu.addItem (10001, "OPEN CUSTOM BANK JSON...");
    const juce::Component::SafePointer<ASR10Editor> safeEditor (this);
    const auto toScreen = [this] (juce::Rectangle<float> bounds) {
        const auto scaled = juce::Rectangle<int> (
            juce::roundToInt (bounds.getX() * getWidth() / W),
            juce::roundToInt (bounds.getY() * getHeight() / H),
            juce::roundToInt (bounds.getWidth() * getWidth() / W),
            juce::roundToInt (bounds.getHeight() * getHeight() / H));
        return localAreaToGlobal (scaled);
    };
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                            toScreen ({ 1071, 265, 85, 35 })),
                        [safeEditor] (int result)
    {
        if (safeEditor == nullptr || result == 0)
            return;
        if (result == 10001)
        {
            safeEditor->browseBankFile();
            return;
        }
        if (result == 10002)
        {
            safeEditor->importOwnedSong();
            return;
        }
        if (result == 10003)
        {
            safeEditor->saveOwnedSongChops();
            return;
        }
        const int index = result - 1;
        if (juce::isPositiveAndBelow (index, (int) safeEditor->availableBanks.size()))
            safeEditor->loadBankFile (safeEditor->availableBanks[(size_t) index]);
    });
}

bool ASR10Editor::refreshBankIndex()
{
    const auto previouslySelected = juce::isPositiveAndBelow (currentBankIndex, (int) availableBanks.size())
        ? availableBanks[(size_t) currentBankIndex] : juce::File();
    availableBanks.clear();
    availableBankTitles.clear();
    availableBankRealSongs.clear();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto expansionBanks = home.getChildFile (
        "Documents/ASR10SamplerLab/banks");
    const auto indexFile = expansionBanks.getParentDirectory().getChildFile ("bank-index.json");
    if (! indexFile.existsAsFile())
        return false;
    juce::var parsed;
    for (int attempt = 0; attempt < 4 && ! parsed.isObject(); ++attempt)
    {
        if (attempt != 0)
            juce::Thread::sleep (attempt * 150);
        parsed = juce::JSON::parse (indexFile.loadFileAsString());
    }
    const auto entries = parsed.getProperty ("banks", juce::var());
    if (! entries.isArray())
        return false;
    const auto nameInProcessor = processor.getBankName();
    int selected = -1;
    for (const auto& bank : *entries.getArray())
    {
        const auto title = bank.getProperty ("title", "").toString();
        const auto filename = bank.getProperty ("path", "").toString();
        if (title.isEmpty() || filename.isEmpty())
            continue;
        const juce::File path (filename);
        // Do not stat each remote JSON here. The index itself was loaded,
        // and the chosen bank is validated when the user selects it.
        if (title == nameInProcessor || previouslySelected == path)
            selected = (int) availableBanks.size();
        const bool realSong = path.getFileName().startsWithIgnoreCase ("owned-")
            || bank.getProperty ("sourcePath", "").toString().isNotEmpty()
            || bank.getProperty ("ownedAlbum", "").toString().isNotEmpty();
        availableBankTitles.push_back (title);
        availableBanks.push_back (path);
        availableBankRealSongs.push_back (realSong);
    }
    currentBankIndex = selected;
    return ! availableBanks.empty();
}

bool ASR10Editor::loadBankFile (const juce::File& file)
{
    juce::String error;
    const bool ok = processor.loadBank (file, error);
    editingValue = false;
    numericEntry = false;
    if (ok)
    {
        currentBankFile = file;
        if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        {
            const auto preference = lastBankPreferenceFile();
            if (preference.getParentDirectory().createDirectory())
                preference.replaceWithText (file.getFullPathName());
        }
        currentBankIndex = -1;
        for (int i = 0; i < (int) availableBanks.size(); ++i)
            if (availableBanks[(size_t) i] == file)
            {
                currentBankIndex = i;
                break;
            }
    }
    display (ok ? "BANK: " + processor.getBankName() : "BANK ERROR: " + error);
    repaint();
    return ok;
}

juce::File ASR10Editor::lastBankPreferenceFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("ASR10SamplerLab/last-bank.txt");
}

void ASR10Editor::stepBank (int direction)
{
    if (! refreshBankIndex())
    {
        display ("NO BANK LIBRARY");
        return;
    }
    const auto count = (int) availableBanks.size();
    int index = currentBankIndex < 0
        ? (direction > 0 ? 0 : count - 1)
        : (currentBankIndex + (direction > 0 ? 1 : -1) + count) % count;
    // When playing a real-song bank, advance to the next real song instead
    // of jumping back into a royalty-free demo pack between tracks.
    if (currentBankIndex >= 0 && availableBankRealSongs[(size_t) currentBankIndex])
        for (int checked = 0; checked < count && !availableBankRealSongs[(size_t) index]; ++checked)
            index = (index + (direction > 0 ? 1 : -1) + count) % count;
    loadBankFile (availableBanks[(size_t) index]);
}

void ASR10Editor::browseBankFile()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto expansionBanks = home.getChildFile (
        "Documents/ASR10SamplerLab/banks");
    const auto initialFolder = expansionBanks.isDirectory() ? expansionBanks : home;
    chooser = std::make_unique<juce::FileChooser> ("Load an ASR-10 bank manifest",
                    initialFolder, "*.json");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (!file.existsAsFile()) return;
        loadBankFile (file);
    });
}

void ASR10Editor::importOwnedSong()
{
    // Imports only an existing local recording the user selects. The sampler
    // never downloads artist audio from YouTube or streaming services.
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto music = home.getChildFile ("Music");
    chooser = std::make_unique<juce::FileChooser> (
        "Choose an audio recording you own (WAV, AIFF, FLAC, MP3)",
        music.isDirectory() ? music : home,
        "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    const juce::Component::SafePointer<ASR10Editor> safeEditor (this);
    chooser->launchAsync (flags, [safeEditor] (const juce::FileChooser& dialog)
    {
        if (safeEditor != nullptr)
        {
            const auto source = dialog.getResult();
            if (source.existsAsFile())
                safeEditor->createOwnedSongBank (source);
        }
    });
}

bool ASR10Editor::createOwnedSongBank (const juce::File& source)
{
    const bool supported = source.hasFileExtension (".wav") || source.hasFileExtension (".aif")
                        || source.hasFileExtension (".aiff") || source.hasFileExtension (".flac")
                        || source.hasFileExtension (".mp3");
    if (! source.existsAsFile() || ! supported)
    {
        display ("CHOOSE WAV / AIFF / FLAC / MP3");
        return false;
    }
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    const auto banks = home.getChildFile (
        "Documents/ASR10SamplerLab/banks");
    if (! banks.isDirectory())
    {
        display ("EXPANSION IS NOT MOUNTED");
        return false;
    }

    const auto title = "OWNED SONG: " + source.getFileNameWithoutExtension();
    juce::var bank (new juce::DynamicObject());
    auto* info = bank.getDynamicObject();
    info->setProperty ("name", title.substring (0, 64));
    info->setProperty ("title", title);
    info->setProperty ("tempo", 90.0);
    info->setProperty ("selectedInstrument", 0);
    info->setProperty ("chop", true);
    info->setProperty ("note", "User-selected local audio reference, no media bundled.");

    juce::Array<juce::var> sequence;
    for (int i = 0; i < 16; ++i)
        sequence.add (juce::var (juce::Array<juce::var>()));
    info->setProperty ("sequence", juce::var (sequence));

    juce::Array<juce::var> instruments;
    for (int slot = 0; slot < 8; ++slot)
    {
        juce::var track (new juce::DynamicObject());
        auto* t = track.getDynamicObject();
        t->setProperty ("name", slot == 0 ? "FULL SONG / 16 CHOPS"
                                           : "PHRASE ZONE " + juce::String (slot));
        t->setProperty ("path", source.getFullPathName());
        t->setProperty ("rootNote", 60);
        t->setProperty ("volume", 0.8);
        t->setProperty ("attack", 0.005);
        t->setProperty ("release", 0.25);
        t->setProperty ("filter", 1.0);
        t->setProperty ("loop", false);
        // Slot 1 plays 16 chops from the entire recording. Slots 2–8 each
        // open a smaller, continuous region to make phrase hunting easier.
        t->setProperty ("sampleStart", slot == 0 ? 0.0 : (slot - 1) / 7.0);
        t->setProperty ("sampleEnd", slot == 0 ? 1.0 : slot / 7.0);
        juce::Array<juce::var> points;
        for (int boundary = 0; boundary <= 16; ++boundary)
            points.add ((double) boundary / 16.0);
        t->setProperty ("chopPoints", juce::var (points));
        instruments.add (track);
    }
    info->setProperty ("instruments", juce::var (instruments));

    const auto name = source.getFileNameWithoutExtension()
        .retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")
        .substring (0, 36);
    const auto bankFile = banks.getChildFile (
        "owned-" + name + "-" + juce::Uuid().toString().substring (0, 8) + ".json");
    if (! bankFile.replaceWithText (juce::JSON::toString (bank, true)))
    {
        display ("FAILED TO SAVE SONG BANK");
        return false;
    }
    if (! loadBankFile (bankFile))
    {
        bankFile.deleteFile();
        return false;
    }

    // Add the user-created bank to the existing index, preserving the
    // official/free-pack sessions. The audio itself stays at source.
    const auto indexFile = banks.getParentDirectory().getChildFile ("bank-index.json");
    auto index = juce::JSON::parse (indexFile.loadFileAsString());
    if (index.isObject())
    {
        auto entries = index.getProperty ("banks", {});
        if (auto* items = entries.getArray())
        {
            juce::var record (new juce::DynamicObject());
            record.getDynamicObject()->setProperty ("title", title);
            record.getDynamicObject()->setProperty ("path", bankFile.getFullPathName());
            record.getDynamicObject()->setProperty ("instruments", 8);
            record.getDynamicObject()->setProperty ("tempo", 90.0);
            items->add (record);
            if (! indexFile.replaceWithText (juce::JSON::toString (index, true)))
                display ("SONG LOADED, BANK SAVED · INDEX ERROR");
            else
                display ("SONG LOADED · PLAY KEYS C3–D#4");
        }
    }
    refreshBankIndex();
    repaint();
    return true;
}

bool ASR10Editor::saveOwnedSongChops()
{
    if (! currentBankFile.existsAsFile()
        || ! currentBankFile.getFileName().startsWithIgnoreCase ("owned-"))
    {
        display ("SAVE IS FOR IMPORTED SONG BANKS");
        repaint();
        return false;
    }
    auto manifest = juce::JSON::parse (currentBankFile);
    if (! manifest.isObject())
    {
        display ("BANK JSON CANNOT BE READ");
        return false;
    }
    auto slots = manifest.getProperty ("instruments", {});
    if (! slots.isArray() || slots.getArray()->size() != 8)
    {
        display ("BANK NEEDS EIGHT TRACKS");
        return false;
    }
    const int before = processor.getSelectedInstrument();
    for (int slot = 0; slot < 8; ++slot)
    {
        processor.selectInstrument (slot);
        auto entry = slots[slot];
        if (! entry.isObject())
            continue;
        auto* s = entry.getDynamicObject();
        s->setProperty ("rootNote", processor.getRootNote());
        s->setProperty ("volume", processor.getVolume());
        s->setProperty ("attack", processor.getAttack());
        s->setProperty ("release", processor.getRelease());
        s->setProperty ("filter", processor.getFilter());
        s->setProperty ("pitch", processor.getPitch());
        s->setProperty ("loop", processor.getLoopEnabled());
        s->setProperty ("fxType", processor.getFxType());
        s->setProperty ("fxAmount", processor.getFxAmount());
        s->setProperty ("sampleStart", processor.getSampleStart());
        s->setProperty ("sampleEnd", processor.getSampleEnd());
        juce::Array<juce::var> chopPoints;
        for (int boundary = 0; boundary <= 16; ++boundary)
            chopPoints.add ((double) processor.getChopPoint (slot, boundary));
        s->setProperty ("chopPoints", juce::var (chopPoints));
    }
    processor.selectInstrument (before);
    manifest.getDynamicObject()->setProperty ("selectedInstrument", before);
    manifest.getDynamicObject()->setProperty ("tempo", processor.getTempo());
    manifest.getDynamicObject()->setProperty ("chop", processor.getChopMode());
    if (! currentBankFile.replaceWithText (juce::JSON::toString (manifest, true)))
    {
        display ("COULD NOT SAVE CHOP EDITS");
        repaint();
        return false;
    }
    display ("SAVED CHOP MARKERS / TRACK SETTINGS");
    repaint();
    return true;
}

void ASR10Editor::display (const juce::String& s) { message = s; }
void ASR10Editor::showParameter (int n)
{
    parameter = juce::jlimit (0, 11, n);
    waveformView = parameter == 7;
    editingValue = true;
    numericEntry = false;
    display (parameterName() + " " + parameterValue());
}
juce::String ASR10Editor::parameterName() const { return parameters[parameter]; }

float ASR10Editor::getParameterNormalized() const
{
    switch (parameter)
    {
        case 0: return processor.getVolume();
        case 1: return processor.getAttack() / 10.0f;
        case 2: return processor.getRelease() / 10.0f;
        case 3: return processor.getFilter();
        case 4: return (processor.getPitch() + 24.0f) / 48.0f;
        case 5: return processor.getSampleStart();
        case 6: return processor.getSampleEnd();
        case 7: return processor.getRootNote() / 127.0f;
        case 8: return processor.getLoopEnabled() ? 1.0f : 0.0f;
        case 9: return processor.getFxType() / 4.0f;
        case 10: return processor.getFxAmount();
        case 11: return (processor.getTempo() - 40.0f) / 260.0f;
        default: return 0;
    }
}

void ASR10Editor::setParameterNormalized (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    switch (parameter)
    {
        case 0: processor.setVolume (v); break;
        case 1: processor.setAttack (v * 10.0f); break;
        case 2: processor.setRelease (v * 10.0f); break;
        case 3: processor.setFilter (v); break;
        case 4: processor.setPitch (v * 48.0f - 24.0f); break;
        case 5: processor.setSampleStart (juce::jmin (v, processor.getSampleEnd() - 0.001f)); break;
        case 6: processor.setSampleEnd (juce::jmax (v, processor.getSampleStart() + 0.001f)); break;
        case 7: processor.setRootNote (juce::roundToInt (v * 127.0f)); break;
        case 8: processor.setLoopEnabled (v >= 0.5f); break;
        case 9: processor.setFxType (juce::roundToInt (v * 4.0f)); break;
        case 10: processor.setFxAmount (v); break;
        case 11: processor.setTempo (40.0f + v * 260.0f); break;
    }
    editingValue = true;
    numericEntry = false;
    display (parameterName() + " " + parameterValue());
}

void ASR10Editor::adjustParameter (float delta)
{
    setParameterNormalized (getParameterNormalized() + delta);
}

juce::String ASR10Editor::parameterValue() const
{
    switch (parameter)
    {
        case 0: return juce::String ((int)(processor.getVolume() * 100)) + "%";
        case 1: return juce::String (processor.getAttack(), 2) + " s";
        case 2: return juce::String (processor.getRelease(), 2) + " s";
        case 3: return juce::String ((int)(processor.getFilter() * 100)) + "%";
        case 4: return juce::String (processor.getPitch(), 1) + " st";
        case 5: return juce::String ((int)(processor.getSampleStart() * 100)) + "%";
        case 6: return juce::String ((int)(processor.getSampleEnd() * 100)) + "%";
        case 7: return juce::String (processor.getRootNote());
        case 8: return processor.getLoopEnabled() ? "ON" : "OFF";
        case 9: return juce::String (processor.getFxType());
        case 10: return juce::String ((int)(processor.getFxAmount() * 100)) + "%";
        case 11: return juce::String (processor.getTempo(), 1) + " BPM";
        default: return {};
    }
}

int ASR10Editor::computerKeyToOffset (int key)
{
    const juce::String pattern ("zsxdcvgbhnjmq2w3er5t6y7ui9o0p");
    int idx = pattern.indexOfChar ((juce::juce_wchar)juce::CharacterFunctions::toLowerCase ((juce::juce_wchar)key));
    return idx;
}

bool ASR10Editor::keyPressed (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (k.getModifiers().isCommandDown() && (code == 'b' || code == 'B'))
    { loadBank(); return true; }
    if (k.getModifiers().isCommandDown() && (code == 'i' || code == 'I'))
    { importOwnedSong(); return true; }
    if (k.getModifiers().isCommandDown() && k.getModifiers().isShiftDown()
        && (code == 's' || code == 'S'))
    { saveOwnedSongChops(); return true; }
    if (k.getModifiers().isAltDown() && (code == 'w' || code == 'W'))
    { waveformView = ! waveformView; repaint(); return true; }
    if (k.getModifiers().isAltDown() && (code == juce::KeyPress::leftKey ||
                                        code == juce::KeyPress::rightKey))
    { stepBank (code == juce::KeyPress::rightKey ? 1 : -1); return true; }
    if (code == juce::KeyPress::escapeKey) { numericEntry = false; editingValue = false; repaint(); return true; }
    if (code == juce::KeyPress::returnKey)
    {
        activate ({ {}, "enter", 0 }, k.getModifiers()); return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)
    { adjustParameter (code == juce::KeyPress::upKey ? 0.01f : 0.1f); repaint(); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey)
    { adjustParameter (code == juce::KeyPress::downKey ? -0.01f : -0.1f); repaint(); return true; }
    if (code == juce::KeyPress::spaceKey)
    {
        if (processor.isSequencePlaying()) processor.stopSequence();
        else processor.startSequence (false);
        repaint(); return true;
    }
    if (k.getModifiers().isCommandDown() && (code == 'o' || code == 'O'))
    { loadInstrument (processor.getSelectedInstrument()); return true; }
    if (k.getModifiers().isCommandDown() && (code == 'r' || code == 'R'))
    { processor.startSequence (true); repaint(); return true; }
    if (code >= '0' && code <= '9' && k.getModifiers().isShiftDown())
    {
        numericValue = juce::jlimit (0, 999, (numericEntry ? numericValue * 10 : 0) + code - '0');
        numericEntry = true; repaint(); return true;
    }
    const int offset = computerKeyToOffset (code);
    if (offset >= 0 && code >= 0 && code < 128)
    {
        if (heldComputerNotes[code] == 0)
        {
            const int note = 48 + offset;
            heldComputerNotes[code] = note + 1;
            processor.triggerNote (note, 0.85f);
            levelLeft = levelRight = 0.8f;
        }
        repaint(); return true;
    }
    return false;
}

bool ASR10Editor::keyStateChanged (bool)
{
    bool changed = false;
    for (int code = 0; code < 128; ++code)
        if (heldComputerNotes[code] != 0 && ! juce::KeyPress::isKeyCurrentlyDown (code))
        {
            processor.releaseNote (heldComputerNotes[code] - 1);
            heldComputerNotes[code] = 0;
            changed = true;
        }
    if (changed) repaint();
    return changed;
}
