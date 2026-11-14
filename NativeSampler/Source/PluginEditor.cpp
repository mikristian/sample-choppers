#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace
{
const auto ivory = juce::Colour (0xffd9d9d6);
const auto shellHighlight = juce::Colour (0xffeeeeeb);
const auto ink = juce::Colour (0xff111215);
const auto graphite = juce::Colour (0xff222428);
const auto darkKey = juce::Colour (0xff353b43);
const auto red = juce::Colour (0xffea3c4c);
const auto blue = juce::Colour (0xff67c9ff);
const auto amber = juce::Colour (0xffffb134);
const auto lcd = juce::Colour (0xff050b10);
const auto lcdText = juce::Colour (0xffe3f2f5);

juce::Colour ledColor (int pad)
{
    const float phase = float (juce::Time::getMillisecondCounterHiRes() * 0.000035);
    return juce::Colour::fromHSV (std::fmod (0.56f + float (pad) * 0.069f + phase, 1.0f),
                                   0.88f, 1.0f, 1.0f);
}

class HardwareButtonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour&, bool hover, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (1.5f);
        const auto core = bounds.reduced (1.2f);
        const bool active = down || button.getToggleState();
        const auto accent = button.findColour (juce::TextButton::buttonOnColourId);
        g.setColour (juce::Colour (0xff11151a));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (active ? juce::Colour (0xff394b57) :
                              hover ? juce::Colour (0xff30373e) : juce::Colour (0xff252b32));
        g.fillRoundedRectangle (core, 3.8f);
        g.setColour (active ? accent.withAlpha (0.85f) : juce::Colour (0xff606a72));
        g.drawRoundedRectangle (core.reduced (0.4f), 3.8f, active ? 1.7f : 1.0f);
        if (active)
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillRoundedRectangle (core.reduced (2.4f), 3.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool, bool) override
    {
        const auto font = juce::Font (juce::FontOptions (juce::jmin (12.0f,
                                         float (button.getHeight()) * 0.36f)).withStyle ("Bold"));
        g.setFont (font);
        g.setColour (juce::Colour (0xffeef5fa));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (6, 3),
                          juce::Justification::centred, 1, 0.75f);
    }
};

void roundRect (juce::Graphics& g, juce::Rectangle<float> area,
                juce::Colour fill, juce::Colour border, float corners = 6.0f)
{
    g.setColour (fill);
    g.fillRoundedRectangle (area, corners);
    g.setColour (border);
    g.drawRoundedRectangle (area.reduced (0.6f), corners, 1.2f);
}
}

VintageSamplerEditor::PadButton::PadButton (int index) : number (index)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus (false);
}

void VintageSamplerEditor::PadButton::mouseDoubleClick (const juce::MouseEvent& event)
{
    // A rapid double tap is a second musical trigger, never a file import.
    // JUCE dispatches a double-click instead of mouseDown on the second
    // press, so route it through the ordinary button-down/up machinery.
    juce::TextButton::mouseDown (event);
}

void VintageSamplerEditor::PadButton::paintButton (juce::Graphics& g, bool hover, bool pressed)
{
    const auto outer = getLocalBounds().toFloat().reduced (0.7f);
    const auto pad = outer.reduced (3.0f);
    auto tone = ledColor (number);
    const bool illuminated = pressed || activity > 0.12f || (sequenceStep && stepActive);
    if (muted)
        tone = red;
    if (sequenceStep && ! stepActive)
        tone = juce::Colour (0xff77838f);

    // Square, softly glowing rubber pad inspired by the MPC Sample chassis.
    for (int ring = 3; ring >= 1; --ring)
    {
        g.setColour (tone.withAlpha ((illuminated ? 0.38f : 0.20f) / float (ring)));
        g.drawRoundedRectangle (outer.reduced (float (3 - ring) * 0.55f),
                                5.0f, float (ring) * 1.6f);
    }
    g.setGradientFill (juce::ColourGradient (illuminated ? tone.brighter (0.14f) :
                                                 juce::Colour (0xff8d9aa7),
                                             pad.getTopLeft(),
                                             illuminated ? tone.darker (0.48f) :
                                                 juce::Colour (0xff7a8794),
                                             pad.getBottomRight(), false));
    g.fillRoundedRectangle (pad, 3.0f);
    g.setColour (tone.withAlpha (illuminated ? 1.0f : 0.87f));
    g.drawRoundedRectangle (pad.reduced (0.6f), 3.0f, illuminated ? 3.0f : 1.8f);
    // Independently moving LED segments travel around the pad's square perimeter.
    // All animation is painted on the message thread at the editor's 30 Hz timer rate.
    const float elapsed = float (juce::Time::getMillisecondCounterHiRes() * 0.001);
    const auto track = outer.reduced (1.9f);
    const float left = track.getX(), top = track.getY();
    const float width = track.getWidth(), height = track.getHeight();
    const float perimeter = 2.0f * (width + height);
    const float rotation = std::fmod (elapsed * 75.0f + float (number) * 21.0f, perimeter);
    const auto pointOnTrack = [left, top, width, height, perimeter] (float distance)
    {
        distance = std::fmod (distance + perimeter, perimeter);
        if (distance < width) return juce::Point<float> (left + distance, top);
        distance -= width;
        if (distance < height) return juce::Point<float> (left + width, top + distance);
        distance -= height;
        if (distance < width) return juce::Point<float> (left + width - distance, top + height);
        return juce::Point<float> (left, top + height - (distance - width));
    };
    const float intensity = illuminated ? 1.0f : hover ? 0.86f : 0.70f;
    constexpr int segments = 96;
    const float segmentLength = perimeter / float (segments);
    for (int segment = 0; segment < segments; ++segment)
    {
        const float position = float (segment) * segmentLength;
        const float distance = std::fmod (position - rotation + perimeter, perimeter);
        const float head = std::exp (-std::pow (distance / 38.0f, 2.0f));
        const float second = std::exp (-std::pow (std::fmod (distance + perimeter * 0.5f, perimeter) / 31.0f, 2.0f));
        const float brightness = juce::jmax (head, second * 0.83f);
        if (brightness < 0.035f) continue;
        const float hue = std::fmod (0.56f + 0.071f * float (number) + elapsed * 0.075f
                                     + position / perimeter * 0.40f, 1.0f);
        const auto light = juce::Colour::fromHSV (hue, 0.92f, 1.0f, 1.0f);
        const auto from = pointOnTrack (position);
        const auto to = pointOnTrack (position + segmentLength * 0.96f);
        g.setColour (light.withAlpha (intensity * brightness * 0.32f));
        g.drawLine (from.x, from.y, to.x, to.y, illuminated ? 7.0f : 5.0f);
        g.setColour (light.withAlpha (intensity * brightness));
        g.drawLine (from.x, from.y, to.x, to.y, illuminated ? 3.1f : 2.0f);
    }
    if (selected && ! sequenceStep)
    {
        g.setColour (amber.withAlpha (0.95f));
        g.drawRoundedRectangle (outer.reduced (0.4f), 5.0f, 1.8f);
    }

    g.setFont (juce::Font (juce::FontOptions (12.5f).withStyle ("Bold")));
    g.setColour (juce::Colours::white);
    g.drawText (juce::String (number + 1).paddedLeft ('0', 2),
                pad.toNearestInt().reduced (10, 4).removeFromTop (19),
                juce::Justification::left);
    const auto name = sequenceStep ? (stepActive ? "ON" : "OFF")
                                : (muted ? "MUTED" : sampleName);
    g.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
    g.drawFittedText (name, pad.toNearestInt().reduced (9, 16),
                      juce::Justification::centred, 2, 0.65f);

    if (! sequenceStep && progress > 0.0f)
    {
        const auto bar = juce::Rectangle<float> (pad.getX() + 9, pad.getBottom() - 8,
                                                 pad.getWidth() - 18, 3);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (bar, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.94f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() *
                               juce::jlimit (0.0f, 1.0f, progress)), 1.0f);
    }
}

VintageSamplerEditor::VintageSamplerEditor (VintageSamplerProcessor& owner)
  : juce::AudioProcessorEditor (&owner), processor (owner)
{
    hardwareButtonLookAndFeel = std::make_unique<HardwareButtonLookAndFeel>();
    setOpaque (true);
    setWantsKeyboardFocus (true);

    mainTitle.setText ("AKAI", juce::dontSendNotification);
    mainTitle.setFont (juce::Font (juce::FontOptions (25.0f).withStyle ("Bold")));
    mainTitle.setColour (juce::Label::textColourId, red);
    mainTitle.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (mainTitle);

    subtitle.setText ("PROFESSIONAL  /  VINTAGE SAMPLER LAB", juce::dontSendNotification);
    subtitle.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
    subtitle.setColour (juce::Label::textColourId, juce::Colour (0xffbfc2c4));
    subtitle.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (subtitle);

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff8ab7be));
    statusLabel.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (statusLabel);
    setStatus ("READY / TAP PADS TO PLAY / SAMPLE SELECT IMPORTS AUDIO / EXCLUSIVE CUT ON");

    screenInfo.setColour (juce::Label::textColourId, lcdText);
    screenInfo.setFont (juce::Font (juce::FontOptions (10.5f)));
    screenInfo.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (screenInfo);

    for (int i = 0; i < padCount; ++i)
    {
        auto pad = std::make_unique<PadButton> (i);
        pad->onClick = [this, i]
        {
            if (! processor.isNoteRepeatEnabled() || shiftOn)
                triggerOrEditPad (i);
        };
        pad->onStateChange = [this, i]
        {
            if (! processor.isNoteRepeatEnabled() || shiftOn)
                return;
            const bool down = padButtons[static_cast<size_t> (i)]->getState() ==
                              juce::Button::buttonDown;
            processor.setPadHeld (i, down);
            if (down)
            {
                selectedPad = i;
                refreshScreen();
            }
        };
        pad->setTooltip ("Pad " + juce::String (i + 1)
                         + " / tap repeatedly to retrigger / MIDI " + juce::String (36 + i)
                         + " / use SAMPLE SELECT to import");
        addAndMakeVisible (*pad);
        padButtons[static_cast<size_t> (i)] = std::move (pad);
    }

    // Hardware controls keep the physical layout. Every enabled control has
    // a corresponding engine or project operation.
    const auto navy = juce::Colour (0xff315061);
    const auto green = juce::Colour (0xff26765a);
    configureButton (sampleMode, "Play, select, and edit samples", navy);
    configureButton (sequenceMode, "Sequence: pads toggle the selected sound's 16 steps", navy);
    configureButton (chopMode, "Inspect source and apply sixteen equal chops with B3", amber);
    configureButton (muteMode, "Tap a pad to mute or unmute it", blue);
    configureButton (levelsMode, "Play the selected sample at 16 velocity levels", navy);
    configureButton (sampleSelect, "Import audio to the selected pad", amber);
    configureButton (project, "Save and load .vsl project sessions", navy);
    configureButton (loadKit, "Import a 16-pad kit or a parent A-H folder containing 128 chops", navy);
    configureButton (tempoSync, "Preserve pitch while matching source BPM to the target tempo; off plays at original speed", amber);
    tempoSync.onClick = [this]
    {
        processor.setTempoSyncEnabled (! processor.isTempoSyncEnabled());
        refreshScreen();
        setStatus (processor.isTempoSyncEnabled() ? "TEMPO SYNC ON / PITCH PRESERVED" : "TEMPO SYNC OFF / ORIGINAL SOURCE SPEED");
    };
    configureButton (tempoTap, "Tap repeatedly to set the sequencer tempo", navy);
    configureButton (erase, "Clear the current 16-step sequence", navy);
    configureButton (record, "Quantized recording: pad hits land on the 16-step pattern", red);
    configureButton (play, "Play the current 16-step loop", green);
    configureButton (stop, "Stop sequence playback", navy);
    configureButton (encoderMinus, "Previous sample pad", navy);
    configureButton (encoderPlus, "Next sample pad", navy);
    configureButton (shift, "Hold alternate bank selection: pads 1-8 select banks A-H", navy);
    configureButton (padBank, "Cycle through eight independent banks A-H", navy);
    configureButton (padFxMode, "Edit the four-slot master FX chain", navy);
    configureButton (knobFxMode, "Edit FX slot, amount and wet level using K1-K3", navy);
    configureButton (repeatMode, "Hold a pad for sixteenth-note repeats at the set tempo", navy);

    sampleMode.onClick = [this] { levelsActive = false; switchScreen (Screen::sample); };
    sequenceMode.onClick = [this] { levelsActive = false; switchScreen (Screen::sequence); };
    chopMode.onClick = [this] { levelsActive = false; switchScreen (Screen::chop); };
    muteMode.onClick = [this] { levelsActive = false; switchScreen (Screen::mute); };
    levelsMode.onClick = [this]
    {
        levelsActive = ! levelsActive;
        switchScreen (Screen::sample);
        setStatus (levelsActive ? "16 LEVELS / PAD VELOCITY 1-16"
                                : "16 LEVELS OFF / SAMPLE PLAY");
    };
    sampleSelect.onClick = [this] { chooseSampleForPad (selectedPad); };
    padFxMode.onClick = [this] { switchScreen (Screen::fx); };
    knobFxMode.onClick = [this] { switchScreen (Screen::fx); };
    shift.onClick = [this]
    {
        shiftOn = ! shiftOn;
        shift.setToggleState (shiftOn, juce::dontSendNotification);
        setStatus (shiftOn ? "SHIFT / TAP PADS 1-8 TO SELECT BANK A-H"
                           : "SHIFT OFF");
    };
    padBank.onClick = [this]
    {
        const int current = processor.getSelectedBank();
        const int next = (current + (shiftOn ? 7 : 1)) % 8;
        processor.selectBank (next);
        shiftOn = false;
        selectedPad = 0;
        refreshPadLabels();
        setStatus ("ACTIVE PAD BANK / " +
                   juce::String::charToString (juce_wchar ('A' + processor.getSelectedBank())));
    };
    repeatMode.onClick = [this]
    {
        processor.setNoteRepeatEnabled (! processor.isNoteRepeatEnabled());
        repeatMode.setToggleState (processor.isNoteRepeatEnabled(), juce::dontSendNotification);
        setStatus (processor.isNoteRepeatEnabled()
                   ? "NOTE REPEAT / HOLD A PAD / 1-16 BEAT" : "NOTE REPEAT OFF");
    };
    project.onClick = [this] { switchScreen (Screen::project); };
    loadKit.onClick = [this] { chooseKitDirectory(); };
    erase.onClick = [this]
    {
        processor.clearSequence();
        setStatus ("SEQUENCE CLEARED / 16 STEPS");
        refreshScreen();
    };
    record.onClick = [this]
    {
        processor.startSequence (! processor.isSequenceRecording());
        // Record pad performances in SAMPLE mode; SEQ is the step editor.
        // This keeps live playing enabled while the sequencer captures notes.
        switchScreen (Screen::sample);
        setStatus (processor.isSequenceRecording() ? "REC / 16-STEP QUANTIZED RECORDING" : "16-STEP RECORDING STOPPED / SEQUENCE PLAYING");
    };
    play.onClick = [this]
    {
        processor.startSequence (false);
        setStatus ("PLAYING / " + juce::String (processor.getTempo(), 1) + " BPM");
    };
    stop.onClick = [this] { processor.stopSequence(); setStatus ("SEQUENCE STOPPED"); };
    tempoTap.onClick = [this]
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto interval = now - lastTapMillis;
        if (interval > 220.0 && interval < 1500.0)
            processor.setTempo (juce::jlimit (40.0f, 240.0f, float (60000.0 / interval)));
        lastTapMillis = now;
        tempoSlider.setValue (processor.getTempo(), juce::dontSendNotification);
        setStatus (juce::String (processor.getTempo(), 1) + " BPM / TAP TEMPO");
        repaint();
    };
    encoderMinus.onClick = [this]
    {
        selectedPad = (selectedPad + 15) % 16; refreshScreen();
    };
    encoderPlus.onClick = [this]
    {
        selectedPad = (selectedPad + 1) % 16; refreshScreen();
    };

    for (int i = 0; i < 3; ++i)
    {
        functionButtons[static_cast<size_t> (i)] = std::make_unique<juce::TextButton>();
        configureButton (*functionButtons[static_cast<size_t> (i)],
                         "LCD soft key / function " + juce::String (i + 1), navy);
        functionButtons[static_cast<size_t> (i)]->onClick = [this, i]
        {
            if (screen == Screen::project)
            {
                if (i == 0) chooseProject (true);
                else if (i == 1) chooseProject (false);
                else switchScreen (Screen::sample);
            }
            else if (screen == Screen::chop && i == 2)
                confirmChop();
            else if (screen == Screen::sequence && i == 2)
            {
                processor.clearSequence(); refreshScreen(); setStatus ("PATTERN CLEARED");
            }
            else if (screen == Screen::fx)
            {
                const int current = processor.getFxSlotType (activeFxSlot);
                if (i == 2)
                    processor.setFxSlotType (activeFxSlot, 0);
                else
                    processor.setFxSlotType (activeFxSlot, (current + (i == 0 ? 4 : 1)) % 5);
                refreshScreen();
                setStatus ("FX " + juce::String (activeFxSlot + 1) +
                           " / TYPE " + juce::String (processor.getFxSlotType (activeFxSlot)));
            }
            else if (i == 0) switchScreen (Screen::sample);
            else if (i == 1) switchScreen (Screen::envelope);
            else switchScreen (Screen::midi);
        };
    }

    configureKnob (masterKnob);
    configureKnob (knobStart, " %");
    configureKnob (knobEnd, " %");
    configureKnob (knobPitch, " st");
    configureKnob (knobAttack, " ms");
    configureKnob (knobRelease, " ms");
    configureKnob (knobLength, " s");
    configureKnob (knobRoot);
    configureKnob (tempoSlider, " BPM");
    sourceBpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sourceBpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 21);
    sourceBpmSlider.setTextValueSuffix (" BPM");
    sourceBpmSlider.setRange (40.0, 240.0, 0.1);
    sourceBpmSlider.setColour (juce::Slider::textBoxTextColourId, lcdText);
    sourceBpmSlider.setColour (juce::Slider::textBoxBackgroundColourId, graphite);
    sourceBpmSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    sourceBpmSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff526c78));
    sourceBpmSlider.setColour (juce::Slider::thumbColourId, amber);
    sourceBpmSlider.setTooltip ("Source BPM for selected pad, editable by dragging or typing; target BPM is shown in the LCD");
    sourceBpmSlider.onValueChange = [this]
    {
        if (updatingTrim) return;
        processor.setPadSourceBpm (selectedPad, float (sourceBpmSlider.getValue()));
        repaint (315, 89, 450, 165);
    };
    addAndMakeVisible (sourceBpmSlider);
    configureKnob (fxSlotKnob);
    configureKnob (fxAmountKnob, " %");
    configureKnob (fxWetKnob, " %");
    fxSlotKnob.setRange (1.0, 4.0, 1.0);
    fxAmountKnob.setRange (0.0, 100.0, 1.0);
    fxWetKnob.setRange (0.0, 100.0, 1.0);
    fxSlotKnob.onValueChange = [this]
    {
        if (updatingTrim) return;
        activeFxSlot = juce::jlimit (0, 3, int (fxSlotKnob.getValue()) - 1);
        refreshScreen();
    };
    fxAmountKnob.onValueChange = [this]
    {
        if (! updatingTrim)
            processor.setFxSlotAmount (activeFxSlot, float (fxAmountKnob.getValue() * 0.01));
    };
    fxWetKnob.onValueChange = [this]
    {
        if (! updatingTrim)
            processor.setFxSlotWet (activeFxSlot, float (fxWetKnob.getValue() * 0.01));
    };

    masterKnob.setTooltip ("MAIN VOLUME");
    knobStart.setRange (0.0, 99.9, 0.1);
    knobEnd.setRange (0.1, 100.0, 0.1);
    knobStart.onValueChange = [this] { applySelectedTrim(); };
    knobEnd.onValueChange = [this] { applySelectedTrim(); };
    tempoSlider.setRange (30, 300, 1);
    tempoSlider.setValue (processor.getTempo(), juce::dontSendNotification);
    tempoSlider.onValueChange = [this] { processor.setTempo (float (tempoSlider.getValue())); repaint(); };

    padFader.setSliderStyle (juce::Slider::LinearVertical);
    padFader.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 55, 18);
    padFader.setRange (0.0, 1.0, 0.01);
    padFader.setValue (0.70, juce::dontSendNotification);
    padFader.setColour (juce::Slider::thumbColourId, ink);
    padFader.setColour (juce::Slider::trackColourId, juce::Colour (0xff6d747b));
    padFader.setColour (juce::Slider::textBoxTextColourId, ink);
    padFader.setTooltip ("Pad dynamics fader / playback velocity for on-screen pad triggers");
    addAndMakeVisible (padFader);

    for (auto* label : { &knob1Label, &knob2Label, &knob3Label })
    {
        label->setFont (juce::Font (juce::FontOptions (11.5f).withStyle ("Bold")));
        label->setColour (juce::Label::textColourId, ink);
        label->setJustificationType (juce::Justification::centred);
        label->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*label);
    }

    attachSlider (masterAttachment, "gain", masterKnob);
    attachSlider (pitchAttachment, "pitch", knobPitch);
    attachSlider (attackAttachment, "attack", knobAttack);
    attachSlider (releaseAttachment, "release", knobRelease);
    attachSlider (lengthAttachment, "length", knobLength);
    attachSlider (rootAttachment, "rootNote", knobRoot);
    // New sample knobs edit the *selected* pad's chop; legacy global
    // sampleStart/sampleEnd APVTS parameters remain supported by the engine.

    loopSelector.addItem ("LOOP OFF", 1);
    loopSelector.addItem ("LOOP ON", 2);
    gateSelector.addItem ("ONE SHOT", 1);
    gateSelector.addItem ("GATED", 2);
    mappingSelector.addItem ("16 PADS", 1);
    mappingSelector.addItem ("KEYBOARD", 2);
    for (int i = 0; i < 16; ++i)
        keyboardPadSelector.addItem ("PAD " + juce::String (i + 1), i + 1);
    for (auto* combo : { &loopSelector, &gateSelector, &mappingSelector, &keyboardPadSelector })
    {
        combo->setColour (juce::ComboBox::backgroundColourId, graphite);
        combo->setColour (juce::ComboBox::textColourId, lcdText);
        combo->setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff596574));
        combo->setColour (juce::ComboBox::arrowColourId, amber);
        addAndMakeVisible (*combo);
    }
    attachCombo (loopAttachment, "loop", loopSelector);
    attachCombo (gateAttachment, "gate", gateSelector);
    attachCombo (mappingAttachment, "mode", mappingSelector);
    attachCombo (keyboardAttachment, "keyboardPad", keyboardPadSelector);

    refreshPadLabels();
    refreshScreen();
    startTimerHz (30);
    setSize (1100, 920);
}

VintageSamplerEditor::~VintageSamplerEditor()
{
    for (auto* button : { &sampleMode, &sequenceMode, &padFxMode, &knobFxMode,
                          &chopMode, &muteMode, &levelsMode, &repeatMode,
                          &sampleSelect, &padBank, &shift, &tempoTap, &erase,
                          &record, &play, &stop, &project, &loadKit,
                          &encoderMinus, &encoderPlus, &tempoSync })
        button->setLookAndFeel (nullptr);
    for (auto& button : functionButtons)
        if (button != nullptr) button->setLookAndFeel (nullptr);
}

void VintageSamplerEditor::configureButton (juce::TextButton& b,
                                            const juce::String& tooltip,
                                            juce::Colour tint)
{
    b.setColour (juce::TextButton::buttonColourId, graphite);
    b.setColour (juce::TextButton::buttonOnColourId, tint);
    b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffdbe2e8));
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    b.setTooltip (tooltip);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    b.setLookAndFeel (hardwareButtonLookAndFeel.get());
    addAndMakeVisible (b);
}

void VintageSamplerEditor::configureKnob (juce::Slider& knob, const juce::String& suffix)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 79, 20);
    knob.setTextValueSuffix (suffix);
    knob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff525c68));
    knob.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xffc2c4c2));
    knob.setColour (juce::Slider::thumbColourId, red);
    knob.setColour (juce::Slider::textBoxBackgroundColourId, shellHighlight);
    knob.setColour (juce::Slider::textBoxTextColourId, ink);
    knob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (knob);
}

void VintageSamplerEditor::attachSlider (std::unique_ptr<SliderAttachment>& attachment,
                                         const juce::String& id, juce::Slider& control)
{
    if (processor.parameters.getParameter (id) != nullptr)
        attachment = std::make_unique<SliderAttachment> (processor.parameters, id, control);
}

void VintageSamplerEditor::attachCombo (std::unique_ptr<ComboAttachment>& attachment,
                                        const juce::String& id, juce::ComboBox& control)
{
    if (processor.parameters.getParameter (id) != nullptr)
        attachment = std::make_unique<ComboAttachment> (processor.parameters, id, control);
}

void VintageSamplerEditor::refreshPadLabels()
{
    for (int i = 0; i < padCount; ++i)
    {
        auto& pad = *padButtons[static_cast<size_t> (i)];
        const auto name = processor.getPadName (i).trim();
        pad.loaded = name.isNotEmpty() && ! name.equalsIgnoreCase ("Empty");
        pad.sampleName = pad.loaded ? name.toUpperCase() : "EMPTY";
        pad.setTooltip ("Pad " + juce::String (i + 1) +
                        " / " + name + " / rapid retrigger enabled / import via SAMPLE SELECT");
        pad.repaint();
    }
    refreshScreen();
}

void VintageSamplerEditor::switchScreen (Screen next)
{
    screen = next;
    refreshScreen();
}

void VintageSamplerEditor::refreshScreen()
{
    updatingTrim = true;
    const auto trim = processor.getPadChop (selectedPad);
    knobStart.setValue (trim.first, juce::dontSendNotification);
    knobEnd.setValue (trim.second, juce::dontSendNotification);
    sourceBpmSlider.setValue (processor.getPadSourceBpm (selectedPad), juce::dontSendNotification);
    fxSlotKnob.setValue (activeFxSlot + 1, juce::dontSendNotification);
    fxAmountKnob.setValue (processor.getFxSlotAmount (activeFxSlot) * 100.0f,
                           juce::dontSendNotification);
    fxWetKnob.setValue (processor.getFxSlotWet (activeFxSlot) * 100.0f,
                        juce::dontSendNotification);
    updatingTrim = false;
    const bool samplePage = screen == Screen::sample || screen == Screen::chop
                         || screen == Screen::mute || screen == Screen::project;
    const bool envPage = screen == Screen::envelope;
    const bool seqPage = screen == Screen::sequence;
    const bool midiPage = screen == Screen::midi;
    const bool fxPage = screen == Screen::fx;
    knobStart.setVisible (samplePage);
    knobEnd.setVisible (samplePage);
    knobPitch.setVisible (samplePage);
    knobAttack.setVisible (envPage);
    knobRelease.setVisible (envPage);
    knobLength.setVisible (envPage);
    tempoSlider.setVisible (seqPage);
    tempoSync.setToggleState (processor.isTempoSyncEnabled(), juce::dontSendNotification);
    tempoSync.setButtonText (processor.isTempoSyncEnabled() ? "SYNC ON" : "SYNC OFF");
    sourceBpmSlider.setVisible (samplePage);
    fxSlotKnob.setVisible (fxPage);
    fxAmountKnob.setVisible (fxPage);
    fxWetKnob.setVisible (fxPage);
    knobRoot.setVisible (midiPage);
    mappingSelector.setVisible (midiPage);
    keyboardPadSelector.setVisible (midiPage);
    loopSelector.setVisible (midiPage);
    gateSelector.setVisible (midiPage);
    knob1Label.setText (fxPage ? "K1  FX SLOT" : seqPage ? "K1  TEMPO" : envPage ? "K1  ATTACK" :
                        midiPage ? "K1  ROOT" : "K1  START", juce::dontSendNotification);
    knob2Label.setText (fxPage ? "K2  AMOUNT" : seqPage ? "16 STEPS" : envPage ? "K2  RELEASE" :
                        midiPage ? "PAD MODE" : "K2  END", juce::dontSendNotification);
    knob3Label.setText (fxPage ? "K3  WET" : seqPage ? "1 BAR" : envPage ? "K3  LENGTH" :
                        midiPage ? "GATE / LOOP" : "K3  TUNE", juce::dontSendNotification);

    functionButtons[0]->setButtonText (fxPage ? "TYPE -" : screen == Screen::project ? "SAVE" : "WAVE");
    functionButtons[1]->setButtonText (fxPage ? "TYPE +" : screen == Screen::project ? "OPEN" : "ENV");
    functionButtons[2]->setButtonText (screen == Screen::project ? "BACK" :
                                       fxPage ? "BYPASS" :
                                       screen == Screen::chop ? "CHOP 16" :
                                       screen == Screen::sequence ? "CLEAR" : "MIDI");
    sampleMode.setToggleState (screen == Screen::sample, juce::dontSendNotification);
    sequenceMode.setToggleState (seqPage, juce::dontSendNotification);
    chopMode.setToggleState (screen == Screen::chop, juce::dontSendNotification);
    muteMode.setToggleState (screen == Screen::mute, juce::dontSendNotification);
    padFxMode.setToggleState (fxPage, juce::dontSendNotification);
    knobFxMode.setToggleState (fxPage, juce::dontSendNotification);
    levelsMode.setToggleState (levelsActive, juce::dontSendNotification);
    repeatMode.setToggleState (processor.isNoteRepeatEnabled(), juce::dontSendNotification);
    shift.setToggleState (shiftOn, juce::dontSendNotification);
    record.setToggleState (processor.isSequenceRecording(), juce::dontSendNotification);
    play.setToggleState (processor.isSequencePlaying(), juce::dontSendNotification);
    for (int i = 0; i < padCount; ++i)
    {
        auto& pad = *padButtons[static_cast<size_t> (i)];
        pad.sequenceStep = seqPage;
        pad.stepActive = seqPage && processor.isStepEnabled (selectedPad, i);
        pad.muted = processor.isPadMuted (i);
        pad.selected = i == selectedPad;
        pad.repaint();
    }
    const auto name = processor.getPadName (selectedPad);
    const juce::String mode = fxPage ? "FX SLOT " + juce::String (activeFxSlot + 1)
                                     : seqPage ? "SEQUENCE" : screen == Screen::chop ? "CHOP"
                            : screen == Screen::mute ? "MUTE" : screen == Screen::project ? "PROJECT"
                            : envPage ? "ENVELOPE" : midiPage ? "MIDI" : "SAMPLE";
    const std::array<juce::String, 5> fxNames {
        "BYPASS", "LOWPASS", "HIGHPASS", "DRIVE", "BITCRUSH"
    };
    screenInfo.setText (fxPage ? mode + "  /  " + fxNames[size_t (juce::jlimit (0, 4, processor.getFxSlotType (activeFxSlot)))]
                        : mode + "  /  PAD " + juce::String (selectedPad + 1).paddedLeft ('0', 2)
                          + "  /  " + name, juce::dontSendNotification);
    repaint();
}

void VintageSamplerEditor::setStatus (const juce::String& message)
{
    statusLabel.setText (message.toUpperCase(), juce::dontSendNotification);
    statusLabel.setTooltip (message);
}

void VintageSamplerEditor::triggerOrEditPad (int index)
{
    if (shiftOn)
    {
        shiftOn = false;
        if (index < 8)
        {
            processor.selectBank (index);
            selectedPad = 0;
            refreshPadLabels();
            setStatus ("ACTIVE PAD BANK / " + juce::String::charToString (juce_wchar ('A' + index)));
        }
        else if (index == 15)
            switchScreen (Screen::project);
        else
            setStatus ("SECONDARY FUNCTION NOT IMPLEMENTED ON THIS PAD");
        refreshScreen();
        return;
    }
    if (screen == Screen::sequence)
    {
        const bool wasOn = processor.isStepEnabled (selectedPad, index);
        processor.setStepEnabled (selectedPad, index, ! wasOn);
        setStatus ("SEQ / SOUND " + juce::String (selectedPad + 1)
                   + " / STEP " + juce::String (index + 1)
                   + (! wasOn ? " ON" : " OFF"));
        refreshScreen();
        return;
    }
    if (screen == Screen::mute)
    {
        processor.setPadMuted (index, ! processor.isPadMuted (index));
        setStatus ("PAD " + juce::String (index + 1)
                   + (processor.isPadMuted (index) ? " MUTED" : " UNMUTED"));
        refreshScreen();
        return;
    }

    if (levelsActive)
        processor.triggerPadVelocity (selectedPad, float (index + 1) / 16.0f);
    else
    {
        selectedPad = index;
        processor.triggerPadVelocity (index, float (padFader.getValue()));
    }
    padButtons[static_cast<size_t> (index)]->activity = 1.0f;
    startedAt[static_cast<size_t> (index)] = juce::Time::getMillisecondCounterHiRes();
    refreshScreen();
}

void VintageSamplerEditor::applySelectedTrim()
{
    if (updatingTrim)
        return;
    const auto start = juce::jlimit (0.0f, 99.9f, float (knobStart.getValue()));
    const auto end = juce::jlimit (start + 0.1f, 100.0f, float (knobEnd.getValue()));
    processor.setPadChop (selectedPad, start, end);
    repaint (310, 90, 455, 160);
}

void VintageSamplerEditor::timerCallback()
{
    for (int i = 0; i < padCount; ++i)
    {
        auto& pad = *padButtons[static_cast<size_t> (i)];
        const float progress = processor.getPadPlaybackProgress (i);
        const float target = progress >= 0.0f ? 1.0f : 0.0f;
        pad.activity += (target - pad.activity) * (target > 0 ? 0.45f : 0.14f);
        if (pad.activity < 0.005f) pad.activity = 0;
        pad.progress = std::max (0.0f, progress);
        const bool muted = processor.isPadMuted (i);
        const bool step = screen == Screen::sequence && processor.isStepEnabled (selectedPad, i);
        pad.muted = muted;
        pad.stepActive = step;
        pad.repaint(); // The moving perimeter lights advance every frame (30 Hz).
    }
    record.setToggleState (processor.isSequenceRecording(), juce::dontSendNotification);
    play.setToggleState (processor.isSequencePlaying(), juce::dontSendNotification);
    repaint (312, 94, 453, 159);
}

void VintageSamplerEditor::confirmChop()
{
    if (processor.getPadName (selectedPad).equalsIgnoreCase ("Empty"))
    {
        setStatus ("LOAD AUDIO BEFORE CHOPPING");
        return;
    }
    const auto safe = juce::Component::SafePointer<VintageSamplerEditor> (this);
    const int source = selectedPad;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
        "Create 16 slices?",
        "This assigns 16 parts of the selected sample across the current pads. "
        "Save your project first if you need the existing pad assignments.",
        "Create 16 chops", "Cancel", this,
        juce::ModalCallbackFunction::create ([safe, source] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            if (safe->processor.chopPadTo16 (source))
            {
                safe->selectedPad = 0;
                safe->refreshPadLabels();
                safe->setStatus ("16 SLICES READY / EACH PAD HAS ITS OWN REGION");
                safe->switchScreen (Screen::sample);
            }
            else safe->setStatus ("CHOP FAILED / " + safe->processor.getLastError());
        }));
}

void VintageSamplerEditor::releaseChooserAfterCallback()
{
    const auto safe = juce::Component::SafePointer<VintageSamplerEditor> (this);
    juce::MessageManager::callAsync ([safe]
    {
        if (safe != nullptr)
            safe->fileChooser.reset();
    });
}

void VintageSamplerEditor::chooseSampleForPad (int pad)
{
    if (fileChooser != nullptr) return;
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose sample for pad " + juce::String (pad + 1),
        juce::File{}, "*.wav;*.aif;*.aiff;*.mp3;*.flac");
    const auto safe = juce::Component::SafePointer<VintageSamplerEditor> (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles,
        [safe, pad] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto selectedFile = chooser.getResult();
            safe->releaseChooserAfterCallback();
            if (selectedFile == juce::File{}) return;
            if (safe->processor.loadSampleForPad (pad, selectedFile))
            {
                safe->selectedPad = pad;
                safe->refreshPadLabels();
                safe->setStatus ("SAMPLE LOADED / " + selectedFile.getFileName());
            }
            else safe->setStatus ("IMPORT ERROR / " + safe->processor.getLastError());
        });
}

void VintageSamplerEditor::chooseKitDirectory()
{
    if (fileChooser != nullptr) return;
    auto startFolder = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                           .getChildFile ("Documents/MusicREA/outputs");
    if (!startFolder.isDirectory())
        startFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser> (
        "Choose a 16-pad kit or parent folder with banks A-H (128 chops)", startFolder);
    const auto safe = juce::Component::SafePointer<VintageSamplerEditor> (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectDirectories,
        [safe] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            const auto dir = chooser.getResult();
            safe->releaseChooserAfterCallback();
            if (dir == juce::File{}) return;
            if (safe->processor.loadKitDirectory (dir))
            {
                safe->refreshPadLabels();
                bool hasAllBanks = true;
                for (int bank = 0; bank < 8; ++bank)
                {
                    const auto letter = juce::String::charToString (juce_wchar ('A' + bank));
                    if (! dir.getChildFile (letter).isDirectory()
                        && ! dir.getChildFile ("bank_" + letter).isDirectory()
                        && ! dir.getChildFile ("Bank " + letter).isDirectory())
                        hasAllBanks = false;
                }
                safe->setStatus ((hasAllBanks ? "8 BANKS LOADED / 128 CHOPS / " : "16 PAD KIT LOADED / ")
                                  + dir.getFileName());
            }
            else
            {
                const auto reason = safe->processor.getLastError();
                safe->setStatus ("KIT FAILED / " +
                                 (reason.isEmpty() ? "SELECT A KIT FOLDER CONTAINING BANKS.JSON OR 16 WAVS" : reason));
            }
        });
}

void VintageSamplerEditor::chooseProject (bool save)
{
    if (fileChooser != nullptr) return;
    fileChooser = std::make_unique<juce::FileChooser> (
        save ? "Save Vintage Sampler Lab project" : "Open Vintage Sampler Lab project",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.vsl");
    const auto safe = juce::Component::SafePointer<VintageSamplerEditor> (this);
    const auto flags = save ? juce::FileBrowserComponent::saveMode |
                              juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting
                            : juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync (flags,
        [safe, save] (const juce::FileChooser& chooser)
        {
            if (safe == nullptr) return;
            auto file = chooser.getResult();
            safe->releaseChooserAfterCallback();
            if (file == juce::File{}) return;
            if (save)
            {
                file = file.withFileExtension (".vsl");
                juce::MemoryBlock data;
                safe->processor.getStateInformation (data);
                if (! file.replaceWithData (data.getData(), data.getSize()))
                    safe->setStatus ("PROJECT SAVE FAILED");
                else safe->setStatus ("PROJECT SAVED / " + file.getFileName());
            }
            else
            {
                juce::MemoryBlock data;
                if (! file.loadFileAsData (data) || data.getSize() == 0)
                    safe->setStatus ("PROJECT OPEN FAILED");
                else
                {
                    safe->processor.stopSequence();
                    safe->processor.setStateInformation (data.getData(), int (data.getSize()));
                    safe->setStatus ("PROJECT OPEN / RESTORING AUDIO...");
                    const auto again = juce::Component::SafePointer<VintageSamplerEditor> (safe.getComponent());
                    juce::MessageManager::callAsync ([again]
                    {
                        if (again != nullptr) again->refreshPadLabels();
                    });
                }
            }
        });
}

bool VintageSamplerEditor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isAltDown()
        || key.getModifiers().isCtrlDown())
        return false;
    const juce::String layout = "1234qwerasdfzxcv";
    const int index = layout.indexOfChar (juce::CharacterFunctions::toLowerCase (key.getTextCharacter()));
    if (! juce::isPositiveAndBelow (index, padCount)) return false;
    triggerOrEditPad (index);
    return true;
}

void VintageSamplerEditor::mouseDown (const juce::MouseEvent& event)
{
    const juce::Rectangle<float> wave (342.0f, 159.0f, 398.0f, 76.0f);
    if (! wave.contains (event.position) ||
        ! (screen == Screen::sample || screen == Screen::chop))
        return;
    const auto trim = processor.getPadChop (selectedPad);
    const float point = 100.0f * (event.position.x - wave.getX()) / wave.getWidth();
    const float anchor = processor.getPadWarpAnchor (selectedPad);
    const bool nearTrimHandle = std::abs (point - trim.first) <= 2.0f
                             || std::abs (point - trim.second) <= 2.0f;
    draggingWarpAnchor = screen == Screen::chop &&
                         (event.mods.isAltDown() || (! nearTrimHandle && std::abs (point - anchor) <= 2.0f));
    if (draggingWarpAnchor)
    {
        waveformDrag = true;
        mouseDrag (event);
        return;
    }
    draggedEdge = std::abs (point - trim.first) < std::abs (point - trim.second) ? 0 : 1;
    waveformDrag = true;
    mouseDrag (event);
}

void VintageSamplerEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (! waveformDrag) return;
    const juce::Rectangle<float> wave (342.0f, 159.0f, 398.0f, 76.0f);
    const float point = juce::jlimit (0.0f, 100.0f,
                                    100.0f * (event.position.x - wave.getX()) / wave.getWidth());
    if (draggingWarpAnchor)
    {
        processor.setPadWarpAnchor (selectedPad, point);
        repaint (342, 159, 398, 76);
        return;
    }
    const auto trim = processor.getPadChop (selectedPad);
    const float start = draggedEdge == 0 ? juce::jmin (point, trim.second - 0.1f) : trim.first;
    const float end = draggedEdge == 1 ? juce::jmax (point, trim.first + 0.1f) : trim.second;
    processor.setPadChop (selectedPad, start, end);
    refreshScreen();
}

void VintageSamplerEditor::mouseUp (const juce::MouseEvent&)
{
    waveformDrag = false;
    draggingWarpAnchor = false;
}

void VintageSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff45484a));
    const auto whole = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (shellHighlight, 26.0f, 20.0f,
                                            ivory.darker (0.08f), 1070.0f, 870.0f, false));
    g.fillRoundedRectangle (whole.reduced (17.0f, 15.0f), 16.0f);
    g.setColour (juce::Colour (0xff989b9c));
    g.drawRoundedRectangle (whole.reduced (17.5f, 15.5f), 16.0f, 2.0f);
    // Top metal cap and high-contrast black display/speaker assembly.
    roundRect (g, { 57, 54, 987, 214 }, ink, juce::Colour (0xff55595e), 9.0f);
    roundRect (g, { 315, 89, 441, 162 }, lcd, juce::Colour (0xff3b6678), 5.5f);
    g.setColour (juce::Colour (0xff172c33));
    for (int y = 100; y <= 244; y += 19)
        g.drawHorizontalLine (y, 325.0f, 747.0f);
    // Backlit speaker grille with evenly spaced perforations.
    roundRect (g, { 791, 88, 232, 166 }, juce::Colour (0xff171b20),
               juce::Colour (0xff394046), 7.0f);
    g.setColour (juce::Colour (0xff343c45));
    for (int y = 100; y < 241; y += 7)
        for (int x = 802; x < 1012; x += 7)
            g.fillEllipse (float (x), float (y), 2.0f, 2.0f);

    g.setColour (red);
    g.setFont (juce::Font (juce::FontOptions (20.0f).withStyle ("Bold")));
    g.drawText ("MPC SAMPLE", 806, 67, 217, 23, juce::Justification::centred);
    g.setColour (juce::Colour (0xffcdd4d9));
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.drawText ("MAIN VOLUME", 108, 229, 134, 18, juce::Justification::centred);

    g.setColour (amber);
    g.setFont (juce::Font (juce::FontOptions (12.0f).withStyle ("Bold")));
    g.drawText (juce::String::charToString (juce_wchar ('A' + processor.getSelectedBank()))
                + "   /   TARGET " + juce::String (processor.getTempo(), 1) + " BPM",
                341, 133, 270, 16, juce::Justification::left);
    g.setColour (juce::Colour (0xff88c9b4));
    const auto mode = screen == Screen::sequence ? "16-STEP SEQUENCE" :
                      screen == Screen::fx ? "4-SLOT MASTER FX" :
                      screen == Screen::chop ? "INSTANT CHOP / 16" :
                      screen == Screen::mute ? "PAD MUTE" :
                      screen == Screen::project ? "PROJECT / SAVE + OPEN" :
                      screen == Screen::envelope ? "ENVELOPE" :
                      screen == Screen::midi ? "MIDI / PLAYBACK" : "SAMPLE EDIT";
    g.drawText (mode, 548, 132, 192, 16, juce::Justification::centredRight);

    const juce::Rectangle<float> wave (342, 159, 398, 76);
    if (screen == Screen::fx)
    {
        const std::array<juce::String, 5> names {
            "BYPASS", "LOWPASS", "HIGHPASS", "DRIVE", "BITCRUSH"
        };
        for (int i = 0; i < 4; ++i)
        {
            const auto type = juce::jlimit (0, 4, processor.getFxSlotType (i));
            const auto area = juce::Rectangle<float> (345.0f + 98.0f * i, 168, 89, 42);
            roundRect (g, area, i == activeFxSlot ? juce::Colour (0xff275768) :
                          juce::Colour (0xff182935), i == activeFxSlot ? amber :
                          juce::Colour (0xff315162), 4.0f);
            g.setColour (i == activeFxSlot ? amber : lcdText);
            g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
            g.drawFittedText ("FX" + juce::String (i + 1) + " " + names[size_t (type)],
                              area.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
    else if (screen == Screen::sequence)
    {
        for (int step = 0; step < 16; ++step)
        {
            const int x = 347 + step * 24;
            const bool active = processor.isStepEnabled (selectedPad, step);
            roundRect (g, { float (x), 172, 19, 37 },
                       active ? amber : juce::Colour (0xff23343e),
                       active ? amber.brighter (0.25f) : juce::Colour (0xff456575), 3.0f);
            g.setColour (active ? ink : lcdText);
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.drawText (juce::String (step + 1), x, 216, 20, 12, juce::Justification::centred);
        }
    }
    else
    {
        const auto peaks = processor.getPadWaveformPeaks (selectedPad);
        const auto trim = processor.getPadChop (selectedPad);
        const auto xStart = wave.getX() + wave.getWidth() * trim.first / 100.0f;
        const auto xEnd = wave.getX() + wave.getWidth() * trim.second / 100.0f;
        g.setColour (juce::Colour (0xff0b141a));
        g.fillRect (wave);
        for (int x = 0; x < int (wave.getWidth()); ++x)
        {
            const auto bin = std::min (peaks.size() - 1, size_t (x * peaks.size() / wave.getWidth()));
            const float height = peaks[bin] * wave.getHeight() * 0.90f;
            g.setColour (amber.withAlpha (0.35f + peaks[bin] * 0.6f));
            const float cx = wave.getX() + float (x);
            g.drawVerticalLine (int (cx), wave.getCentreY() - height * 0.5f,
                                wave.getCentreY() + height * 0.5f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.58f));
        g.fillRect (juce::Rectangle<float> (wave.getX(), wave.getY(),
                                             xStart - wave.getX(), wave.getHeight()));
        g.fillRect (juce::Rectangle<float> (xEnd, wave.getY(),
                                             wave.getRight() - xEnd, wave.getHeight()));
        g.setColour (amber);
        g.drawVerticalLine (int (xStart), wave.getY(), wave.getBottom());
        g.drawVerticalLine (int (xEnd), wave.getY(), wave.getBottom());
        if (screen == Screen::chop)
        {
            const float anchorX = wave.getX() + wave.getWidth() *
                                  juce::jlimit (0.0f, 100.0f, processor.getPadWarpAnchor (selectedPad)) / 100.0f;
            g.setColour (blue);
            g.drawVerticalLine (int (anchorX), wave.getY(), wave.getBottom());
            juce::Path marker;
            marker.addTriangle (anchorX - 5.0f, wave.getY(), anchorX + 5.0f, wave.getY(),
                                anchorX, wave.getY() + 8.0f);
            g.fillPath (marker);
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText ("WARP: DRAG BLUE / OPTION+DRAG", 347, 216, 260, 14,
                        juce::Justification::left);
        }
    }

    // Recessed navigation/fader wells and printed legends.
    roundRect (g, { 80, 476, 148, 274 }, ivory.darker (0.05f),
               juce::Colour (0xffadb2b5), 6.0f);
    g.setColour (graphite);
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.drawText ("LEGACY FADER", 95, 489, 121, 15, juce::Justification::centred);
    g.drawText ("DYNAMICS", 95, 739, 122, 15, juce::Justification::centred);
    g.drawText ("RGB PERFORMANCE PADS  /  BANK " +
                juce::String::charToString (juce_wchar ('A' + processor.getSelectedBank())),
                299, 418, 505, 16,
                juce::Justification::centred);
    roundRect (g, { 838, 543, 185, 140 }, ivory.darker (0.09f),
               juce::Colour (0xffc1c1bd), 13.0f);
    g.setColour (juce::Colour (0xffdce0dd));
    g.fillEllipse (874, 555, 108, 108);
    g.setColour (juce::Colour (0xff969d9f));
    g.drawEllipse (875, 556, 106, 106, 3.0f);
    g.setColour (ink);
    g.fillEllipse (887, 568, 82, 82);
    g.setColour (juce::Colour (0xffc3c5c2));
    g.drawLine (927, 576, 927, 595, 3.2f);
    g.setColour (graphite);
    g.drawText ("PUSH / SELECT", 861, 664, 145, 15, juce::Justification::centred);

    // Large dark rubber wrist rest anchors the control surface.
    roundRect (g, { 59, 805, 984, 77 }, juce::Colour (0xff4b5055),
               juce::Colour (0xffa2a9ae), 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine (808, 65, 1038);
    g.setColour (juce::Colour (0xff434b52));
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    g.drawText ("VINTAGE SAMPLER LAB  /  ORIGINAL SOFTWARE - MPC-INSPIRED LAYOUT",
                283, 887, 700, 16, juce::Justification::centred);
}

void VintageSamplerEditor::resized()
{
    mainTitle.setBounds (85, 81, 160, 31);
    subtitle.setBounds (86, 112, 218, 18);
    masterKnob.setBounds (108, 141, 120, 88);
    screenInfo.setBounds (340, 102, 398, 24);
    statusLabel.setBounds (158, 825, 794, 35);

    const int softX[3] = { 335, 480, 625 };
    for (int i = 0; i < 3; ++i)
        functionButtons[size_t (i)]->setBounds (softX[i], 262, 113, 26);

    sampleMode.setBounds (74, 300, 94, 35);
    sequenceMode.setBounds (178, 300, 94, 35);
    padFxMode.setBounds (74, 341, 94, 35);
    knobFxMode.setBounds (178, 341, 94, 35);
    project.setBounds (74, 384, 94, 33);
    loadKit.setBounds (178, 384, 94, 33);

    const auto setKnob = [] (juce::Slider& control, int x)
    {
        control.setBounds (x, 317, 104, 87);
    };
    for (auto* knob : { &knobStart, &knobAttack, &tempoSlider, &knobRoot })
        setKnob (*knob, 333);
    setKnob (fxSlotKnob, 333);
    for (auto* knob : { &knobEnd, &knobRelease })
        setKnob (*knob, 482);
    setKnob (fxAmountKnob, 482);
    for (auto* knob : { &knobPitch, &knobLength })
        setKnob (*knob, 631);
    setKnob (fxWetKnob, 631);
    knob1Label.setBounds (335, 291, 104, 18);
    knob2Label.setBounds (482, 291, 104, 18);
    knob3Label.setBounds (631, 291, 104, 18);

    constexpr int gridX = 328, gridY = 435, w = 88, h = 88, spacingX = 4, spacingY = 4;
    for (int i = 0; i < padCount; ++i)
        padButtons[size_t (i)]->setBounds (gridX + (i % 4) * (w + spacingX),
                                           gridY + (i / 4) * (h + spacingY), w, h);

    padFader.setBounds (122, 518, 68, 205);
    loopSelector.setBounds (81, 766, 105, 29);
    gateSelector.setBounds (190, 766, 102, 29);
    mappingSelector.setBounds (479, 319, 105, 34);
    keyboardPadSelector.setBounds (626, 319, 106, 34);

    chopMode.setBounds (836, 299, 93, 35);
    muteMode.setBounds (940, 299, 93, 35);
    levelsMode.setBounds (836, 340, 93, 35);
    repeatMode.setBounds (940, 340, 93, 35);
    sampleSelect.setBounds (836, 382, 197, 34);
    padBank.setBounds (836, 424, 93, 32);
    shift.setBounds (940, 424, 93, 32);
    tempoTap.setBounds (836, 463, 93, 32);
    tempoSync.setBounds (836, 263, 93, 27);
    sourceBpmSlider.setBounds (932, 263, 101, 27);
    erase.setBounds (940, 463, 93, 32);
    encoderMinus.setBounds (872, 695, 62, 30);
    encoderPlus.setBounds (947, 695, 62, 30);
    record.setBounds (837, 734, 93, 32);
    play.setBounds (940, 734, 93, 32);
    stop.setBounds (837, 773, 196, 27);
}
