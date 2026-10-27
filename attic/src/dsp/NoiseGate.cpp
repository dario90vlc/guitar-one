#include "NoiseGate.h"

#include <cmath>

namespace
{
    /** Coeficiente de un filtro de un polo para una constante de tiempo dada. */
    inline float onePoleCoefficient (double sampleRate, float milliseconds) noexcept
    {
        const float samples = juce::jmax (1.0f, (float) (sampleRate * (double) milliseconds * 0.001));

        return std::exp (-1.0f / samples);
    }
}

//==============================================================================
void NoiseGate::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    numChannels = juce::jmax ((juce::uint32) 1, spec.numChannels);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.numChannels = numChannels;

    sidechainHighPass.prepare (filterSpec);

    coefficientsDirty = true;
    updateCoefficientsIfNeeded();

    prepared = true;
    reset();
}

void NoiseGate::reset()
{
    sidechainHighPass.reset();

    envelope = 0.0f;
    gain = 1.0f;
    holdCounter = 0;
    gateOpen = false;
    currentGainReductionDb = 0.0f;
    currentEnvelopeDb = -100.0f;
}

//==============================================================================
void NoiseGate::setThreshold (float newThresholdDb)
{
    thresholdDb = juce::jlimit (-100.0f, 0.0f, newThresholdDb);
}

void NoiseGate::setRatio (float newRatio)
{
    ratio = juce::jlimit (1.0f, 20.0f, newRatio);
}

void NoiseGate::setAttackMs (float milliseconds)
{
    const float newValue = juce::jlimit (0.1f, 200.0f, milliseconds);

    if (newValue != attackMs)
    {
        attackMs = newValue;
        coefficientsDirty = true;
    }
}

void NoiseGate::setReleaseMs (float milliseconds)
{
    const float newValue = juce::jlimit (5.0f, 2000.0f, milliseconds);

    if (newValue != releaseMs)
    {
        releaseMs = newValue;
        coefficientsDirty = true;
    }
}

void NoiseGate::setHoldMs (float milliseconds)
{
    const float newValue = juce::jlimit (0.0f, 1000.0f, milliseconds);

    if (newValue != holdMs)
    {
        holdMs = newValue;
        coefficientsDirty = true;
    }
}

void NoiseGate::setRangeDb (float decibels)
{
    rangeDb = juce::jlimit (0.0f, 100.0f, decibels);
}

void NoiseGate::setSidechainHighPassHz (float frequencyHz)
{
    const float newFreq = juce::jlimit (20.0f, 500.0f, frequencyHz);

    if (newFreq != sidechainHz)
    {
        sidechainHz = newFreq;
        coefficientsDirty = true;
    }
}

void NoiseGate::setEnabled (bool shouldBeEnabled)
{
    enabled = shouldBeEnabled;
}

//==============================================================================
void NoiseGate::updateCoefficientsIfNeeded()
{
    if (! coefficientsDirty)
        return;

    coefficientsDirty = false;

    // Seguidor de envolvente: ataque rápido (1 ms) y liberación moderada (30 ms).
    envelopeAttackCoeff = onePoleCoefficient (sampleRate, 1.0f);
    envelopeReleaseCoeff = onePoleCoefficient (sampleRate, 30.0f);

    // Apertura y cierre de la puerta.
    openCoeff = onePoleCoefficient (sampleRate, attackMs);
    closeCoeff = onePoleCoefficient (sampleRate, releaseMs);

    holdSamples = (int) (sampleRate * (double) holdMs * 0.001);

    // Pasa-altos de la cadena lateral: evita que los graves abran la puerta.
    sidechainHighPass.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, sidechainHz, 0.50f);
}

//==============================================================================
void NoiseGate::process (juce::AudioBuffer<float>& buffer)
{
    if (! prepared || ! enabled || buffer.getNumSamples() == 0)
        return;

    juce::ScopedNoDenormals noDenormals;

    updateCoefficientsIfNeeded();

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) numChannels);

    if (numCh <= 0)
        return;

    // Pendiente del expansor: con ratio 1 la reducción es nula.
    const float slope = juce::jmax (0.0f, ratio - 1.0f);
    const float maxReductionDb = rangeDb;

    float lastGainReductionDb = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // 1) Detección: máximo de |x| entre canales, filtrado en la cadena lateral.
        float peak = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float x = buffer.getReadPointer (ch)[i];
            const float sidechain = sidechainHighPass.processSample (ch, x);

            peak = juce::jmax (peak, std::abs (sidechain));
        }

        // 2) Seguidor de envolvente (ataque rápido, liberación lenta).
        const float envCoeff = (peak > envelope) ? envelopeAttackCoeff : envelopeReleaseCoeff;
        envelope = peak + envCoeff * (envelope - peak);

        const float envDb = juce::Decibels::gainToDecibels (envelope, -100.0f);

        // 3) Comparación con el umbral, con retención (hold) antes de cerrar.
        if (envDb >= thresholdDb)
        {
            gateOpen = true;
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0)
        {
            --holdCounter;
        }
        else
        {
            gateOpen = false;
        }

        // 4) Ganancia objetivo: 1 si está abierta, reducción proporcional si está cerrada.
        float targetGain = 1.0f;

        if (! gateOpen)
        {
            const float reductionDb = juce::jlimit (-maxReductionDb, 0.0f, (envDb - thresholdDb) * slope);
            targetGain = juce::Decibels::decibelsToGain (reductionDb, -100.0f);
        }

        // 5) Suavizado: ataque al abrir, liberación al cerrar.
        const float gainCoeff = (targetGain > gain) ? openCoeff : closeCoeff;
        gain = targetGain + gainCoeff * (gain - targetGain);

        // 6) Aplicación de la ganancia a todos los canales.
        for (int ch = 0; ch < numCh; ++ch)
            buffer.getWritePointer (ch)[i] *= gain;

        lastGainReductionDb = juce::Decibels::gainToDecibels (gain, -100.0f);
    }

    currentEnvelopeDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
    currentGainReductionDb = lastGainReductionDb;
}
