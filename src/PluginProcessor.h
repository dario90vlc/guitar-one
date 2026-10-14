#pragma once

#include <JuceHeader.h>
#include "dsp/AmpModel.h"
#include "dsp/CleanAmp.h"
#include "dsp/CrunchAmp.h"
#include "dsp/DistortionAmp.h"
#include "dsp/NoiseGate.h"

class GuitarAmpAudioProcessor : public juce::AudioProcessor
{
public:
    GuitarAmpAudioProcessor();
    ~GuitarAmpAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Guitar Amp"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Parámetros de audio
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Acceso a modelos
    CleanAmp* getCleanAmp() { return &cleanAmp; }
    CrunchAmp* getCrunchAmp() { return &crunchAmp; }
    DistortionAmp* getDistortionAmp() { return &distortionAmp; }
    NoiseGate* getNoiseGate() { return &noiseGate; }

    // Latencia reportada
    int getLatencySamples() const override { return 0; }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Modelos de amplificador
    CleanAmp cleanAmp;
    CrunchAmp crunchAmp;
    DistortionAmp distortionAmp;
    NoiseGate noiseGate;

    // Estado
    int currentAmp = 0; // 0=clean, 1=crunch, 2=distortion
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // Buffer de trabajo
    juce::AudioBuffer<float> processBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarAmpAudioProcessor)
};
