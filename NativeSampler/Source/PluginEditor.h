#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>

class VintageSamplerProcessor;

class VintageSamplerEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VintageSamplerEditor (VintageSamplerProcessor&);
    ~VintageSamplerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int padCount = 16;
    enum class Screen { sample, envelope, midi, sequence, chop, mute, project, fx };
    struct PadButton final : juce::TextButton
    {
        explicit PadButton (int number);
        void paintButton (juce::Graphics&, bool hover, bool pressed) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        int number;
        juce::String sampleName { "EMPTY" };
        float activity = 0.0f;
        float progress = 0.0f;
        bool loaded = false, muted = false, sequenceStep = false, stepActive = false, selected = false;
    };

    void timerCallback() override;
    void refreshPadLabels();
    void refreshScreen();
    void switchScreen (Screen);
    void chooseSampleForPad (int);
    void chooseKitDirectory();
    void chooseProject (bool save);
    void confirmChop();
    void triggerOrEditPad (int);
    void releaseChooserAfterCallback();
    void setStatus (const juce::String&);
    void configureKnob (juce::Slider&, const juce::String& suffix = {});
    void configureButton (juce::TextButton&, const juce::String& tooltip, juce::Colour tint);
    void attachSlider (std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>&,
                       const juce::String&, juce::Slider&);
    void attachCombo (std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&,
                      const juce::String&, juce::ComboBox&);
    void applySelectedTrim();

    VintageSamplerProcessor& processor;
    Screen screen = Screen::sample;
    int selectedPad = 0;
    int activeFxSlot = 0;
    bool levelsActive = false;
    bool shiftOn = false;
    bool updatingTrim = false;
    bool waveformDrag = false;
    int draggedEdge = 0;
    bool draggingWarpAnchor = false;
    double lastTapMillis = 0.0;
    std::array<std::unique_ptr<PadButton>, padCount> padButtons;
    std::unique_ptr<juce::LookAndFeel_V4> hardwareButtonLookAndFeel;
    std::array<double, padCount> startedAt {};
    juce::Label statusLabel, mainTitle, subtitle, screenInfo, knob1Label, knob2Label, knob3Label;
    juce::TextButton sampleMode { "SAMPLE" }, sequenceMode { "SEQ" }, padFxMode { "PAD FX" },
                     knobFxMode { "KNOB FX" }, chopMode { "CHOP" }, muteMode { "MUTE" },
                     levelsMode { "16 LEVELS" }, repeatMode { "NOTE REPEAT" },
                     sampleSelect { "SAMPLE SELECT" }, padBank { "PAD BANK" }, shift { "SHIFT" },
                     tempoTap { "TAP TEMPO" }, erase { "ERASE" }, record { "REC" },
                     play { "PLAY" }, stop { "STOP" }, project { "PROJECT" },
                     loadKit { "IMPORT KIT" }, encoderMinus { "-" }, encoderPlus { "+" },
                     tempoSync { "SYNC OFF" };
    std::array<std::unique_ptr<juce::TextButton>, 3> functionButtons;
    juce::Slider masterKnob, knobStart, knobEnd, knobPitch, knobAttack, knobRelease,
                 knobLength, knobRoot, tempoSlider, padFader, sourceBpmSlider;
    juce::Slider fxSlotKnob, fxAmountKnob, fxWetKnob;
    juce::ComboBox loopSelector, gateSelector, mappingSelector, keyboardPadSelector;
    std::unique_ptr<juce::FileChooser> fileChooser;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SliderAttachment> masterAttachment, pitchAttachment, attackAttachment,
             releaseAttachment, lengthAttachment, rootAttachment;
    std::unique_ptr<ComboAttachment> loopAttachment, gateAttachment, mappingAttachment, keyboardAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VintageSamplerEditor)
};
