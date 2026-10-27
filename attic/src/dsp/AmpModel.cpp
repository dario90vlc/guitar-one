#include "AmpModel.h"

//==============================================================================
void AmpModel::prepare (const juce::dsp::ProcessSpec& spec)
{
    currentSpec = spec;
    currentSpec.sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    currentSpec.numChannels = juce::jmax ((juce::uint32) 1, spec.numChannels);
    currentSpec.maximumBlockSize = juce::jmax ((juce::uint32) 1, spec.maximumBlockSize);

    sampleRate = currentSpec.sampleRate;
    maximumBlockSize = (int) currentSpec.maximumBlockSize;
    numChannels = currentSpec.numChannels;

    toneStack.prepare (currentSpec);
    cabinet.prepare (currentSpec);

    volumeSmoothed.reset (sampleRate, 0.02);
    volumeSmoothed.setCurrentAndTargetValue (volumeToLinearGain (volume));

    parametersDirty = true;
    prepared = true;

    reset();
}

void AmpModel::reset()
{
    toneStack.reset();
    cabinet.reset();
    volumeSmoothed.setCurrentAndTargetValue (volumeToLinearGain (volume));
}

//==============================================================================
void AmpModel::process (juce::AudioBuffer<float>& buffer)
{
    if (! prepared || buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    // 1) Aplicar los controles que hayan cambiado (sólo recalcula si es necesario).
    updateParametersIfNeeded();

    // 2) Etapa no lineal propia del amplificador (válvulas / clipping).
    processSaturation (buffer);

    // 3) Pila de tonos (bass, mid, treble, presence).
    toneStack.process (buffer);

    // 4) Simulador de pantalla (respuesta del altavoz).
    cabinet.process (buffer);

    // 5) Volumen de salida con suavizado por muestra.
    applyOutputVolume (buffer);
}

//==============================================================================
void AmpModel::setGain (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != gain)
    {
        gain = newValue;
        parametersDirty = true;
    }
}

void AmpModel::setBass (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != bass)
    {
        bass = newValue;
        parametersDirty = true;
    }
}

void AmpModel::setMid (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != mid)
    {
        mid = newValue;
        parametersDirty = true;
    }
}

void AmpModel::setTreble (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != treble)
    {
        treble = newValue;
        parametersDirty = true;
    }
}

void AmpModel::setPresence (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != presence)
    {
        presence = newValue;
        parametersDirty = true;
    }
}

void AmpModel::setVolume (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != volume)
    {
        volume = newValue;
        parametersDirty = true;
    }
}

//==============================================================================
void AmpModel::updateParametersIfNeeded()
{
    if (! parametersDirty)
        return;

    parametersDirty = false;

    toneStack.setControls (bass, mid, treble, presence);
    volumeSmoothed.setTargetValue (volumeToLinearGain (volume));

    ampParametersChanged();
}

float AmpModel::gainToDriveDb (float minDb, float maxDb) const noexcept
{
    return juce::jmap (juce::jlimit (0.0f, 1.0f, gain), 0.0f, 1.0f, minDb, maxDb);
}

float AmpModel::volumeToLinearGain (float volume01) noexcept
{
    const float v = juce::jlimit (0.0f, 1.0f, volume01);

    if (v <= 0.0f)
        return 0.0f;

    // Taper de audio: -60 dB .. +6 dB (silencio real en el mínimo).
    const float db = juce::jmap (v, 0.0f, 1.0f, -60.0f, 6.0f);

    return juce::Decibels::decibelsToGain (db, -100.0f);
}

void AmpModel::applyGain (juce::AudioBuffer<float>& buffer, float gainLinear) noexcept
{
    if (gainLinear == 1.0f)
        return;

    buffer.applyGain (gainLinear);
}

void AmpModel::applyOutputVolume (juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) numChannels);

    if (numCh <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = volumeSmoothed.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }
}
