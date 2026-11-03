/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "util/dsp/TransientDetect.h"
#include "util/dsp/SamplingUtil.h"
#include "util/SustainData.h"
#include "util/LicenseManager.h"

//==============================================================================
/**
*/
class SustainPedalAudioProcessor  : public AudioProcessor
                            #if JucePlugin_Enable_ARA
                             , public AudioProcessorARAExtension
                            #endif
{
public:
    //==============================================================================
    SustainPedalAudioProcessor();
    ~SustainPedalAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;

    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    std::vector<float> getLatestSample() { return sample; }
    std::vector<float> getLatestRecording() { return tailSignal; }
#if JUCE_DEBUG
    std::vector<float> getTransients() { return td->getFrameFlags(); }
#endif

    //==============================================================================
    // Safe to call from any thread: the audio thread picks up the change.
    void setPedal(bool val);
    // True while the pedal is down from any source (GUI, keybind, host parameter or MIDI CC64).
    bool isPedalDown() const { return pedalDown.load() || *pedalParam > 0.5f; }
    int getActiveLayerCount() const { return activeLayerCount.load(); }

    void setAutoCatch(bool val) { *autoCatch = val; }
    bool getAutoCatch() { return *autoCatch > 0.5f; }

    void setMaxLayers(int val) { *maxLayers = (float)val; }
    int getMaxLayers() { return (int)*maxLayers; }

    void setRise(float val) { *rise = val; }
    float getRise() { return *rise; }

    void setTail(float val) { *tail = val; }
    float getTail() { return *tail; }

    void setWet(float val) { *wet = val; }
    float getWet() { return *wet; }

    void setDry(float val) { *dry = val; }
    float getDry() { return *dry; }

    void setTargetSampleLength(float val) { *period = val; }
    float getTargetSampleLength() { return *period; }

    void setHoldToSustain(bool val) { *holdToggle = val; }
    bool getHoldToSustain() { return *holdToggle > 0.5f; }

    void setKeycode(int val) { *keycode = (float)val; }
    int getKeycode() { return (int)*keycode; }

    void setForcePeriod(bool val) { *forcePeriod = val; }
    bool getForcePeriod() { return *forcePeriod > 0.5f; }

    void setLicenseKey(std::string val) { licenseKey = val; LicenseManager::saveLicense(licenseKey); }
    std::string getLicenseKey() { return licenseKey; }

    //==============================================================================
    void start() { ready = true; }
    bool isReady() { return ready; }
    bool canRun() { return tailSignal.size() >= 4096; }

    //==============================================================================
#if JUCE_DEBUG
    std::vector<float> getSpectralFluxes() { return td->getFrameFlags(); }
#endif

private:
    AudioProcessorValueTreeState parameters;

    //==============================================================================
    // Audio-thread-only helpers.
    void handlePedalChange(bool down);
    void catchLayer();
    bool tailIsAudible() const;

    TransientDetector* td = nullptr;

    std::vector<float> tailSignal, sample;
    std::vector<SustainData*> layers;
    std::vector<SustainData*> fadingOut;
    std::vector<std::future<std::vector<float>>> generatedSamples;

    bool ready = false,
        gettingPhrase = false,
        phraseFound = false;

    // Pedal state. pedalDown is the combined state seen by the audio thread;
    // midiPedal comes from MIDI CC64 and is only touched on the audio thread.
    std::atomic<bool> pedalDown{ false };
    bool midiPedal = false;

    // Auto-catch: samples elapsed since the last onset while waiting for the
    // note to settle before catching it. -1 means nothing is pending.
    int pendingCatchSamples = -1;
    std::atomic<int> activeLayerCount{ 0 };

    int sr = 48000;

    std::atomic<float>* rise,
        * tail,
        * wet,
        * dry,
        * period,
        * maxLayers,
        * holdToggle,
        * keycode,
        * forcePeriod,
        * autoCatch,
        * settle,
        * pedalParam;

    std::string licenseKey = "";

    std::mutex mtx;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SustainPedalAudioProcessor)
};