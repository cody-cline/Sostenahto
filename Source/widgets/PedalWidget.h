/*
  ==============================================================================

    PedalWidget.h
    Created: 19 Mar 2023 7:19:35pm
    Author:  andre

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginEditor.h"
#include "CustomDial.h"

//==============================================================================
/*
*/
class PedalWidget  : public juce::Component
{
public:
    PedalWidget(SustainPedalAudioProcessor& audioProcessor, Label& infoText, int width = 200, int height = 680) : 
        audioProcessor(audioProcessor), infoText(infoText), pedalToggle(*this), bindButton(*this), holdToggle(*this), keybindLabel(*this) {
        setSize(width, height);
        
        addAndMakeVisible(pedalToggle);
        pedalToggle.setClickingTogglesState(true);
        pedalToggle.setBounds(0, 0, 200, 650);
        pedalToggle.onStateChange = [this] { this->audioProcessor.setPedal(pedalToggle.getToggleState()); };
        pedalToggle.setColour(pedalToggle.tickColourId, Colour());
        pedalToggle.setColour(pedalToggle.tickDisabledColourId, Colour());
        pedalUp = ImageCache::getFromMemory(BinaryData::pedalUp_png, BinaryData::pedalUp_pngSize);
        pedalDown = ImageCache::getFromMemory(BinaryData::pedalDown_png, BinaryData::pedalDown_pngSize);
        addAndMakeVisible(bindButton);
        bindButton.setButtonText(audioProcessor.getKeycode() == -1 ? "None" : KeyPress(audioProcessor.getKeycode()).getTextDescription());
        bindButton.setBounds(50, 60, 100, 24);
        bindButton.onClick = [this] { rebinding = true; bindButton.setButtonText("Listening..."); };
        keybindLabel.setText("Keybind", dontSendNotification);
        keybindLabel.attachToComponent(&bindButton, false);
        keybindLabel.setSize(100, 24);
        keybindLabel.setJustificationType(Justification::centred);
        addAndMakeVisible(holdToggle);
        holdToggle.setClickingTogglesState(true);
        holdToggle.setBounds(35, 640, 150, 20);
        holdToggle.setButtonText("Hold to sustain");
    }
    ~PedalWidget() override {}

    void paint(juce::Graphics& g) override {
        g.drawImageWithin(pedalToggle.getToggleState() ? pedalDown : pedalUp, 0, 0, 200, 650, RectanglePlacement::Flags::stretchToFit);
    }
    void resized() override {}

    void mouseEnter(const MouseEvent&) override {
        infoText.setText(std::string("The \"Pedal\" that enables the sustain. Hold it down and play: with Auto Catch on, every new note is layered in. Click the pedal, press your keybind, use a MIDI sustain pedal (CC64), or map the \"Pedal\" parameter in your host.") +
            "\n\nKeybind: click on the button, followed by a key. Only works while this window has focus." +
            "\n\nHold to sustain: whether the keybind holds or toggles the pedal.",
            dontSendNotification);
    }

    bool rebinding = false;

    PedalToggleButton pedalToggle, holdToggle;
    PedalTextButton bindButton;

private:
    CustomLabel keybindLabel;
    Label& infoText;
    Image pedalUp, pedalDown;

    SustainPedalAudioProcessor& audioProcessor;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalWidget)
};
