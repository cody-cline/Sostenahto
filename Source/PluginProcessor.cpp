/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include <climits>
#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
SustainPedalAudioProcessor::SustainPedalAudioProcessor() :
    parameters(*this, nullptr, Identifier("APVTS"), {
            std::make_unique<AudioParameterFloat> ("wet", "Wet", 0.f, 100.f, 70.f),
            std::make_unique<AudioParameterFloat> ("dry", "Dry", 0.f, 100.f, 100.f),
            std::make_unique<AudioParameterFloat>("rise", "Rise", 0.f, 5.f, 0.5f),
            std::make_unique<AudioParameterFloat>("tail", "Tail", 0.f, 5.f, 1.f),
            std::make_unique<AudioParameterFloat>("period", "Period", 0.01f, 1.5f, 0.5f),
            std::make_unique<AudioParameterInt>("maxLayers", "Max Layers", 1, 10, 5),
            std::make_unique<AudioParameterBool>("holdToggle", "Hold to sustain", true),
            std::make_unique<AudioParameterInt>("keycode", "Keycode", -1, INT_MAX, -1),
            std::make_unique<AudioParameterBool>("forcePeriod", "Force Period", true),
            std::make_unique<AudioParameterBool>("autoCatch", "Auto Catch", true),
            std::make_unique<AudioParameterFloat>("settle", "Settle", 0.1f, 1.f, 0.2f),
            std::make_unique<AudioParameterBool>("pedal", "Pedal", false)
        })
#ifndef JucePlugin_PreferredChannelConfigurations
    , AudioProcessor(BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
        .withInput("Input", AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", AudioChannelSet::stereo(), true)
#endif
    )
#endif
{
    wet = parameters.getRawParameterValue("wet");
    dry = parameters.getRawParameterValue("dry");
    rise = parameters.getRawParameterValue("rise");
    tail = parameters.getRawParameterValue("tail");
    period = parameters.getRawParameterValue("period");
    maxLayers = parameters.getRawParameterValue("maxLayers");
    holdToggle = parameters.getRawParameterValue("holdToggle");
    keycode = parameters.getRawParameterValue("keycode");
    forcePeriod = parameters.getRawParameterValue("forcePeriod");
    autoCatch = parameters.getRawParameterValue("autoCatch");
    settle = parameters.getRawParameterValue("settle");
    pedalParam = parameters.getRawParameterValue("pedal");

    licenseKey = LicenseManager::loadLicense();
}

SustainPedalAudioProcessor::~SustainPedalAudioProcessor()
{
    delete td;
}

//==============================================================================
const String SustainPedalAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SustainPedalAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SustainPedalAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SustainPedalAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SustainPedalAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SustainPedalAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SustainPedalAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SustainPedalAudioProcessor::setCurrentProgram (int)
{
}

const String SustainPedalAudioProcessor::getProgramName (int)
{
    return {};
}

void SustainPedalAudioProcessor::changeProgramName (int, const String&)
{
}

//==============================================================================
void SustainPedalAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    sr = (int)sampleRate;
    delete td;
    td = new TransientDetector(sr, samplesPerBlock);
    pendingCatchSamples = -1;
}

void SustainPedalAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SustainPedalAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SustainPedalAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    if (!ready) return;

    ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    if (totalNumInputChannels == 0 || totalNumOutputChannels == 0) return;

    int nSamples = buffer.getNumSamples();

    // MIDI sustain pedal (CC64): any standard sustain pedal or MIDI foot switch works.
    for (const auto metadata : midiMessages) {
        auto msg = metadata.getMessage();
        if (msg.isSustainPedalOn()) midiPedal = true;
        else if (msg.isSustainPedalOff()) midiPedal = false;
    }

    auto* channelData = buffer.getWritePointer(0);
    mtx.lock();
    bool onset = td->process(std::vector<float>(channelData, channelData + nSamples), tailSignal);
    // Record the tail of a transient
    if (tailSignal.size() < sr * 5) {
        tailSignal.resize(tailSignal.size() + nSamples);
        memcpy(&tailSignal[tailSignal.size() - nSamples], channelData, sizeof(float) * nSamples);
    }
    mtx.unlock();

    // Combine every pedal source and react to presses/releases.
    bool down = midiPedal || *pedalParam > 0.5f;
    if (down != pedalDown.load()) {
        pedalDown = down;
        handlePedalChange(down);
    }

    // Auto catch: while the pedal is held, every new note is caught as a
    // layer once it has settled (so the attack is left out of the loop).
    if (down && *autoCatch > 0.5f) {
        if (onset) pendingCatchSamples = 0; // a new note restarts the wait
        else if (pendingCatchSamples >= 0) pendingCatchSamples += nSamples;

        if (pendingCatchSamples >= 0 && pendingCatchSamples >= (int)(*settle * sr)) {
            if (tailIsAudible()) catchLayer();
            pendingCatchSamples = -1;
        }
    }
    else {
        pendingCatchSamples = -1;
    }

    float dryDecimal = *dry / 100.f;
    for (int i = 0; i < nSamples; i++) {
        channelData[i] *= dryDecimal;
    }

    int nGeneratedSamples = generatedSamples.size();
    for (int i = nGeneratedSamples - 1; i >= 0; i--) {
        if (generatedSamples[i].wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto sample = generatedSamples[i].get();
            if (!sample.empty()) {
                // If the pedal was lifted while this layer was being prepared, it starts
                // at full volume and fades out at the Tail rate like the other layers
                // (pressing again before it's gone brings it back).
                const bool down = pedalDown.load();
                auto* layer = new SustainData(sample, *rise, *tail, down ? 0.f : 1.f);
                layer->fade(down);
                layers.emplace_back(layer);
            }
            generatedSamples.erase(generatedSamples.begin() + i);
        }
    }

    float wetDecimal = *wet / 100.f;
    // If the pedal is triggered
    for (int i = 0; i < layers.size();) {
        if (layers[i] == nullptr) {
            layers.erase(layers.begin() + i);
            continue;
        }
        auto sustain = layers[i]->getSample(nSamples, wetDecimal, sr);
        if (sustain.empty()) {
            delete layers[i];
            layers.erase(layers.begin() + i);
            continue;
        }
        buffer.addFrom(0, 0, &sustain[0], nSamples);
        //buffer.addFrom(1, 0, &sustain[0], nSamples);
        i++;
    }
    activeLayerCount = (int)layers.size();

    for (int i = 0; i < fadingOut.size();) {
        if (fadingOut[i] == nullptr) {
            fadingOut.erase(fadingOut.begin() + i);
            continue;
        }
        auto sustain = fadingOut[i]->getSample(nSamples, wetDecimal, sr);
        if (sustain.empty()) {
            delete fadingOut[i];
            fadingOut.erase(fadingOut.begin() + i);
            continue;
        }
        buffer.addFrom(0, 0, &sustain[0], nSamples);
        //buffer.addFrom(1, 0, &sustain[0], nSamples);
        i++;
    }
    
    for (int i = 1; i < totalNumOutputChannels; i++) {
        buffer.copyFrom(i, 0, channelData, nSamples);
    }

    /*for (int i = 1; i < totalNumOutputChannels; i++) {
        buffer.addFrom(i, 0, channelData, nSamples);
    }*/
}

//==============================================================================
bool SustainPedalAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* SustainPedalAudioProcessor::createEditor()
{
    return new SustainPedalAudioProcessorEditor (*this, parameters);
}

//==============================================================================
void SustainPedalAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    auto state = parameters.copyState();
    state.getChildWithProperty("id", "keycode").setProperty("value", var(*keycode), nullptr);
    std::unique_ptr<XmlElement> xml(state.createXml());

    copyXmlToBinary(*xml, destData);
    DBG(xml->toString());
}

void SustainPedalAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    std::unique_ptr<XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    DBG(xmlState.get()->toString());

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xmlState));

    // Never restore a session with the pedal stuck down.
    if (auto* p = parameters.getParameter("pedal"))
        p->setValueNotifyingHost(0.f);

    licenseKey = LicenseManager::loadLicense();
}

void SustainPedalAudioProcessor::setPedal(bool val) {
    // Called from the GUI/keybind. Route it through the host-visible "pedal"
    // parameter so the audio thread is the only place that touches the layers.
    if (auto* p = parameters.getParameter("pedal")) {
        if ((p->getValue() > 0.5f) != val)
            p->setValueNotifyingHost(val ? 1.f : 0.f);
    }
}

void SustainPedalAudioProcessor::handlePedalChange(bool down) {
    if (down) {
        // Bring back layers that were still fading out from the last release.
        for (auto* layer : layers) {
            if (layer != nullptr) layer->fade(true);
        }
        // Catch whatever is sounding right now (the classic "play, then press" use).
        if (tailIsAudible()) {
            int needed = (int)(*settle * sr);
            if ((int)tailSignal.size() >= needed) catchLayer();
            else if (*autoCatch > 0.5f) pendingCatchSamples = (int)tailSignal.size(); // let it settle first
            else catchLayer();
        }
    }
    else {
        pendingCatchSamples = -1;
        for (auto* layer : layers) {
            if (layer != nullptr) layer->fade(false);
        }
    }
}

bool SustainPedalAudioProcessor::tailIsAudible() const {
    // Ignore silence/breath noise so an early press doesn't waste a layer.
    const size_t n = std::min<size_t>(tailSignal.size(), 2048);
    if (n < 512) return false;
    double sum = 0.0;
    for (size_t i = tailSignal.size() - n; i < tailSignal.size(); i++) {
        sum += (double)tailSignal[i] * tailSignal[i];
    }
    return std::sqrt(sum / (double)n) > 0.003; // about -50 dBFS
}

void SustainPedalAudioProcessor::catchLayer() {
    if (tailSignal.size() < 4096) return;

    // Keep within the layer limit: the oldest layers fade out first.
    const size_t limit = (size_t)std::max(1.f, maxLayers->load());
    while (layers.size() + generatedSamples.size() >= limit && !layers.empty()) {
        layers[0]->fade(false);
        fadingOut.push_back(layers[0]);
        layers.erase(layers.begin());
    }

    // Leave out the start of the note (the tongued attack) so only the steady
    // tone gets looped, as long as enough audio remains to work with.
    const size_t attackSkip = std::min<size_t>((size_t)(0.08 * sr), tailSignal.size() - 4096);
    std::vector<float> steady(tailSignal.begin() + (long)attackSkip, tailSignal.end());

    mtx.lock();
    generatedSamples.emplace_back(
        std::async(
            std::launch::async,
            SamplingUtil::getSample,
            std::move(steady),
            int(sr * *period),
            *forcePeriod > 0.5f,
            (size_t)sr
        )
    );
    mtx.unlock();
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SustainPedalAudioProcessor();
}