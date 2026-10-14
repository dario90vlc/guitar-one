#include "TubeStage.h"

#include <cmath>

//==============================================================================
void TubeStage::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    numChannels = juce::jmax ((juce::uint32) 1, spec.numChannels);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.numChannels = numChannels;

    couplingFilter.prepare (filterSpec);
    gridFilter.prepare (filterSpec);
    cathodeFilter.prepare (filterSpec);

    filtersDirty = true;
    updateFilterCoefficientsIfNeeded();
    reset();

    prepared = true;
}

void TubeStage::reset()
{
    couplingFilter.reset();
    gridFilter.reset();
    cathodeFilter.reset();
}

//==============================================================================
void TubeStage::setDrive (float driveLinear) noexcept
{
    drive = juce::jlimit (0.0f, 200.0f, driveLinear);
}

void TubeStage::setDriveDb (float driveDb) noexcept
{
    setDrive (juce::Decibels::decibelsToGain (juce::jlimit (-60.0f, 60.0f, driveDb)));
}

void TubeStage::setBias (float newBias) noexcept
{
    bias = juce::jlimit (-0.95f, 0.95f, newBias);
}

void TubeStage::setHardness (float newHardness) noexcept
{
    hardness = juce::jlimit (0.05f, 6.0f, newHardness);
}

void TubeStage::setOutputLevel (float levelLinear) noexcept
{
    outputLevel = juce::jlimit (0.0f, 8.0f, levelLinear);
}

void TubeStage::setCouplingHighPassHz (float frequencyHz)
{
    const float newFreq = juce::jlimit (5.0f, 500.0f, frequencyHz);

    if (newFreq != couplingHz)
    {
        couplingHz = newFreq;
        filtersDirty = true;
    }
}

void TubeStage::setGridLowPassHz (float frequencyHz)
{
    const float maxFreq = (float) (sampleRate * 0.45);
    const float newFreq = juce::jlimit (500.0f, juce::jmax (1000.0f, maxFreq), frequencyHz);

    if (newFreq != gridLowPassHz)
    {
        gridLowPassHz = newFreq;
        filtersDirty = true;
    }
}

void TubeStage::setCathodeBypass (float amount01)
{
    const float newAmount = juce::jlimit (0.0f, 1.0f, amount01);

    if (newAmount != cathodeAmount)
    {
        cathodeAmount = newAmount;
        filtersDirty = true;
    }
}

void TubeStage::setCharacter (Character newCharacter) noexcept
{
    character = newCharacter;

    switch (character)
    {
        case Character::Triode12AX7:
            hardness = 1.20f;
            bias = 0.12f;
            break;

        case Character::Triode12AT7:
            hardness = 0.75f;
            bias = 0.06f;
            break;

        case Character::TriodeEL34:
            hardness = 1.85f;
            bias = 0.24f;
            break;

        case Character::Pentode6L6:
            hardness = 1.45f;
            bias = 0.18f;
            break;
    }
}

//==============================================================================
void TubeStage::updateFilterCoefficientsIfNeeded()
{
    if (! filtersDirty)
        return;

    filtersDirty = false;

    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    // Condensador de acoplo: elimina la continua que introduce el bias.
    couplingFilter.coefficients = Coefficients::makeHighPass (sampleRate, couplingHz, 0.5f);

    // Capacidad de reja / Miller: redondea los agudos antes de saturar (evita "fizz").
    gridFilter.coefficients = Coefficients::makeLowPass (sampleRate, gridLowPassHz, 0.707f);

    // Bypass de cátodo: pico de medios-bajos (0 a +4 dB).
    const float cathodeGainDb = cathodeAmount * 4.0f;
    cathodeFilter.coefficients = Coefficients::makePeakFilter (sampleRate,
                                                              cathodeFreqHz,
                                                              0.9f,
                                                              juce::Decibels::decibelsToGain (cathodeGainDb));
}

//==============================================================================
float TubeStage::softSaturate (float x, float k) noexcept
{
    // Saturación racional: monótona, sin funciones trascendentes y con asíntota +/- 1/k.
    return x / (1.0f + k * std::abs (x));
}

float TubeStage::softClipCubic (float x) noexcept
{
    // Recorte cúbico: y = 1.5x - 0.5x^3 para |x| <= 1, saturado fuera.
    if (x >= 1.0f)
        return 1.0f;

    if (x <= -1.0f)
        return -1.0f;

    return x * (1.5f - 0.5f * x * x);
}

float TubeStage::triodeTransfer (float x, float bias, float hardness) noexcept
{
    const float k = juce::jmax (0.05f, hardness);
    const float b = juce::jlimit (-0.95f, 0.95f, bias);

    const float xb = x + b;

    // Curva de conducción: por debajo del umbral la válvula entra en corte.
    // La transición con smoothstep mantiene la continuidad de valor y pendiente.
    const float cutoffStart = -1.6f;
    const float cutoffWidth = 0.6f;

    float conduction = 1.0f;

    if (xb <= cutoffStart)
    {
        conduction = 0.0f;
    }
    else if (xb < cutoffStart + cutoffWidth)
    {
        const float t = (xb - cutoffStart) / cutoffWidth;   // 0 .. 1
        conduction = t * t * (3.0f - 2.0f * t);             // smoothstep
    }

    // Transferencia no lineal (asíntota en +/- 1/k).
    const float y = softSaturate (xb, k);

    // Compensación del punto de reposo: con entrada 0 la salida es 0 (sin continua).
    const float y0 = softSaturate (b, k);

    return (y - y0) * conduction;
}

//==============================================================================
float TubeStage::processSample (int channel, float input) noexcept
{
    // Filtro de reja antes de la saturación (redondea los agudos).
    float x = gridFilter.processSample (channel, input);

    // Ganancia de la etapa y saturación del triodo.
    x = triodeTransfer (x * drive, bias, hardness);

    // Condensador de acoplo: quita la continua generada por el bias.
    x = couplingFilter.processSample (channel, x);

    // Realce de cátodo (medios-bajos).
    x = cathodeFilter.processSample (channel, x);

    return x * outputLevel;
}

void TubeStage::process (juce::AudioBuffer<float>& buffer)
{
    if (! prepared)
        return;

    updateFilterCoefficientsIfNeeded();

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) numChannels);

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            data[i] = processSample (ch, data[i]);
    }
}
