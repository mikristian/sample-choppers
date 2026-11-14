#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <memory>
#include <vector>

// An ASR-10-inspired hardware front panel. Everything is painted in a fixed
// 1600x650 coordinate system so the keyboard and hit targets scale together.
class ASR10Editor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit ASR10Editor (ASR10Processor&);
    ~ASR10Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    struct Hit
    {
        juce::Rectangle<float> bounds;
        juce::String action;
        int value = 0;
    };

    ASR10Processor& processor;
    std::vector<Hit> hits;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<juce::File> availableBanks;
    std::vector<juce::String> availableBankTitles;
    std::vector<bool> availableBankRealSongs;
    int currentBankIndex = -1;
    juce::File currentBankFile;
    int selectedChopBoundary = 1;
    juce::String message { "ASR-10 READY" };
    juce::String mode { "SELECT" };
    int page = 0;
    int parameter = 0;
    int numericValue = 0;
    bool numericEntry = false;
    bool editingValue = false;
    bool waveformView = false;
    int heldMouseNote = -1;
    int heldComputerNotes[128] {};
    juce::String activeDrag;
    float dragStart = 0.0f;
    float lastSliderY = 0.0f;
    int wheelPitch = 0;
    float wheelMod = 0.0f;
    bool recordArmed = false;
    int source = 0;
    int fxSelection = 0;
    int audioTrack = 0;
    float levelLeft = 0.0f, levelRight = 0.0f;

    void timerCallback() override;
    juce::Point<float> localPoint (const juce::Point<float>&) const;
    void button (juce::Graphics&, juce::Rectangle<float>, const juce::String&,
                 const juce::String&, int, bool selected = false, bool led = false,
                 float fontSize = 12.0f);
    void label (juce::Graphics&, const juce::String&, float x, float y,
                float w, float h, float size, juce::Colour,
                juce::Justification = juce::Justification::centred);
    void drawChassis (juce::Graphics&);
    void drawControlPanel (juce::Graphics&);
    void drawKeyboard (juce::Graphics&);
    void drawScreen (juce::Graphics&);
    void drawSlider (juce::Graphics&, juce::Rectangle<float>,
                     float value, const juce::String& action);
    void drawMeter (juce::Graphics&, float x, float top, float v, const juce::String&);
    void activate (const Hit&, const juce::ModifierKeys&, bool doubleClick = false);
    void loadInstrument (int);
    void loadBank();
    void browseBankFile();
    void importOwnedSong();
    bool createOwnedSongBank (const juce::File&);
    bool saveOwnedSongChops();
    bool loadBankFile (const juce::File&);
    juce::File lastBankPreferenceFile() const;
    bool refreshBankIndex();
    void stepBank (int direction);
    void adjustParameter (float delta);
    void setParameterNormalized (float);
    float getParameterNormalized() const;
    juce::String parameterName() const;
    juce::String parameterValue() const;
    void showParameter (int);
    void display (const juce::String&);
    void playMouseKey (juce::Point<float>);
    int noteAt (juce::Point<float>) const;
    static int computerKeyToOffset (int key);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ASR10Editor)
};
