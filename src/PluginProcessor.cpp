#include "PluginProcessor.h"
#include "PluginEditor.h"

GuitarAmpAudioProcessor::GuitarAmpAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

GuitarAmpAudioProcessor::~GuitarAmpAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout GuitarAmpAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Selector de amplificador
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "ampModel", "Amp Model",
        juce::StringArray{"Clean", "Crunch", "Distortion"}, 0));

    // Controles de Clean
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cleanGain", "Clean Gain", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cleanBass", "Clean Bass", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cleanMid", "Clean Mid", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cleanTreble", "Clean Treble", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cleanPresence", "Clean Presence", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "cleanVolume", "Clean Volume", 0.0f, 1.0f, 0.7f));

    // Controles de Crunch
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crunchGain", "Crunch Gain", 0.0f, 1.0f, 0.6f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crunchBass", "Crunch Bass", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crunchMid", "Crunch Mid", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crunchTreble", "Crunch Treble", 0.0f, 1.0f, 0.6f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crunchPresence", "Crunch Presence", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "crunchVolume", "Crunch Volume", 0.0f, 1.0f, 0.7f));

    // Controles de Distortion
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "distGain", "Dist Gain", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "distBass", "Dist Bass", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "distMid", "Dist Mid", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "distTreble", "Dist Treble", 0.0f, 1.0f, 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "distPresence", "Dist Presence", 0.0f, 1.0f, 0.6f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "distVolume", "Dist Volume", 0.0f, 1.0f, 0.6f));

    // Noise Gate
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateThreshold", "Gate Threshold", -80.0f, -20.0f, -60.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gateRatio", "Gate Ratio", 1.0f, 20.0f, 4.0f));
    // Interruptor de la puerta de ruido (usado por el switch de la GUI).
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "gateEnabled", "Gate Enabled", true));

    // Output
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "outputLevel", "Output Level", 0.0f, 1.0f, 0.8f));

    return { params.begin(), params.end() };
}

void GuitarAmpAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    cleanAmp.prepare(spec);
    crunchAmp.prepare(spec);
    distortionAmp.prepare(spec);
    noiseGate.prepare(spec);

    processBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);
}

void GuitarAmpAudioProcessor::releaseResources() {}

void GuitarAmpAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Limpiar canales no usados
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Obtener parámetros
    int ampModel = (int) apvts.getRawParameterValue("ampModel")->load();
    float outputLevel = apvts.getRawParameterValue("outputLevel")->load();

    // Procesar según el modelo seleccionado
    switch (ampModel)
    {
        case 0: // Clean
        {
            cleanAmp.setGain(apvts.getRawParameterValue("cleanGain")->load());
            cleanAmp.setBass(apvts.getRawParameterValue("cleanBass")->load());
            cleanAmp.setMid(apvts.getRawParameterValue("cleanMid")->load());
            cleanAmp.setTreble(apvts.getRawParameterValue("cleanTreble")->load());
            cleanAmp.setPresence(apvts.getRawParameterValue("cleanPresence")->load());
            cleanAmp.setVolume(apvts.getRawParameterValue("cleanVolume")->load());
            cleanAmp.process(buffer);
            break;
        }
        case 1: // Crunch
        {
            crunchAmp.setGain(apvts.getRawParameterValue("crunchGain")->load());
            crunchAmp.setBass(apvts.getRawParameterValue("crunchBass")->load());
            crunchAmp.setMid(apvts.getRawParameterValue("crunchMid")->load());
            crunchAmp.setTreble(apvts.getRawParameterValue("crunchTreble")->load());
            crunchAmp.setPresence(apvts.getRawParameterValue("crunchPresence")->load());
            crunchAmp.setVolume(apvts.getRawParameterValue("crunchVolume")->load());
            crunchAmp.process(buffer);
            break;
        }
        case 2: // Distortion
        {
            distortionAmp.setGain(apvts.getRawParameterValue("distGain")->load());
            distortionAmp.setBass(apvts.getRawParameterValue("distBass")->load());
            distortionAmp.setMid(apvts.getRawParameterValue("distMid")->load());
            distortionAmp.setTreble(apvts.getRawParameterValue("distTreble")->load());
            distortionAmp.setPresence(apvts.getRawParameterValue("distPresence")->load());
            distortionAmp.setVolume(apvts.getRawParameterValue("distVolume")->load());
            distortionAmp.process(buffer);
            break;
        }
    }

    // Noise gate
    noiseGate.setThreshold(apvts.getRawParameterValue("gateThreshold")->load());
    noiseGate.setRatio(apvts.getRawParameterValue("gateRatio")->load());
    noiseGate.setEnabled(apvts.getRawParameterValue("gateEnabled")->load() > 0.5f);
    noiseGate.process(buffer);

    // Output level
    buffer.applyGain(outputLevel);
}

juce::AudioProcessorEditor* GuitarAmpAudioProcessor::createEditor()
{
    return new GuitarAmpAudioProcessorEditor(*this);
}

void GuitarAmpAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void GuitarAmpAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GuitarAmpAudioProcessor();
}
