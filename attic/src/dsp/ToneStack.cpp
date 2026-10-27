#include "ToneStack.h"

//==============================================================================
ToneStack::StyleParameters ToneStack::getStyleParameters (Style styleToUse) noexcept
{
    //              bassFreq  bassQ  bassRange | midFreq midQ midRange | trebleFreq trebleQ trebleRange
    //              |         |      |          |       |    |          |          |       |
    switch (styleToUse)
    {
        case Style::Marshall:
            return { 110.0f, 0.70f, 11.0f,
                     650.0f, 0.90f, 12.0f,
                     3200.0f, 0.70f, 12.0f,
                     4200.0f, 1.00f, 10.0f,
                     0.35f, 1.5f };

        case Style::Modern:
            return { 100.0f, 0.80f, 12.0f,
                     750.0f, 1.10f, 14.0f,
                     3500.0f, 0.80f, 13.0f,
                     5500.0f, 1.20f, 12.0f,
                     0.25f, 1.0f };

        case Style::Fender:
        default:
            // Pila pasiva clásica: poca Q, bastante interacción y graves cálidos.
            return { 90.0f, 0.60f, 10.0f,
                     500.0f, 0.60f, 8.0f,
                     3000.0f, 0.60f, 10.0f,
                     5000.0f, 0.80f, 8.0f,
                     0.55f, 2.0f };
    }
}

//==============================================================================
void ToneStack::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    numChannels = juce::jmax ((juce::uint32) 1, spec.numChannels);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.numChannels = numChannels;

    bassFilter.prepare (filterSpec);
    midFilter.prepare (filterSpec);
    trebleFilter.prepare (filterSpec);
    presenceFilter.prepare (filterSpec);

    dirty = true;
    updateCoefficientsIfNeeded();
    reset();

    prepared = true;
}

void ToneStack::reset()
{
    bassFilter.reset();
    midFilter.reset();
    trebleFilter.reset();
    presenceFilter.reset();
}

//==============================================================================
void ToneStack::setStyle (Style newStyle)
{
    if (newStyle != style)
    {
        style = newStyle;
        markDirty();
    }
}

void ToneStack::setBass (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != bass)
    {
        bass = newValue;
        markDirty();
    }
}

void ToneStack::setMid (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != mid)
    {
        mid = newValue;
        markDirty();
    }
}

void ToneStack::setTreble (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != treble)
    {
        treble = newValue;
        markDirty();
    }
}

void ToneStack::setPresence (float value01)
{
    const float newValue = juce::jlimit (0.0f, 1.0f, value01);

    if (newValue != presence)
    {
        presence = newValue;
        markDirty();
    }
}

void ToneStack::setControls (float bass01, float mid01, float treble01, float presence01)
{
    setBass (bass01);
    setMid (mid01);
    setTreble (treble01);
    setPresence (presence01);
}

//==============================================================================
void ToneStack::updateCoefficientsIfNeeded()
{
    if (! dirty)
        return;

    dirty = false;

    const auto p = getStyleParameters (style);

    // Mapeo 0 .. 1 => -rango .. +rango dB, con 0.5 como posición plana.
    const float bassDb     = (bass     - 0.5f) * 2.0f * p.bassRangeDb;
    const float trebleDb   = (treble   - 0.5f) * 2.0f * p.trebleRangeDb;
    const float presenceDb = (presence - 0.5f) * 2.0f * p.presenceRangeDb;

    float midDb = (mid - 0.5f) * 2.0f * p.midRangeDb;

    // Interacción de pila pasiva: los graves altos deshinchan los medios.
    midDb -= p.interaction * bassDb * 0.45f;

    // La presencia y los agudos comparten banda: se compensan ligeramente.
    const float trebleCompDb = -p.interaction * presenceDb * 0.15f;

    // Compensación de nivel: una pila pasiva pierde nivel al atenuar bandas.
    const float totalDb = bassDb + midDb + trebleDb + presenceDb;
    const float trimDb = p.outputTrimDb + juce::jlimit (0.0f, 9.0f, -0.2f * totalDb);

    outputTrimGain = juce::Decibels::decibelsToGain (trimDb);

    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    bassFilter.coefficients     = Coefficients::makeLowShelf  (sampleRate, p.bassFreq,     p.bassQ,
                                                              juce::Decibels::decibelsToGain (bassDb));

    midFilter.coefficients      = Coefficients::makePeakFilter (sampleRate, p.midFreq,      p.midQ,
                                                               juce::Decibels::decibelsToGain (midDb));

    trebleFilter.coefficients   = Coefficients::makeHighShelf (sampleRate, p.trebleFreq,   p.trebleQ,
                                                              juce::Decibels::decibelsToGain (trebleDb + trebleCompDb));

    presenceFilter.coefficients = Coefficients::makePeakFilter (sampleRate, p.presenceFreq, p.presenceQ,
                                                               juce::Decibels::decibelsToGain (presenceDb));
}

//==============================================================================
void ToneStack::process (juce::AudioBuffer<float>& buffer)
{
    if (! prepared)
        return;

    updateCoefficientsIfNeeded();

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) numChannels);
    const float trim = outputTrimGain;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i];

            x = bassFilter.processSample (ch, x);
            x = midFilter.processSample (ch, x);
            x = trebleFilter.processSample (ch, x);
            x = presenceFilter.processSample (ch, x);

            data[i] = x * trim;
        }
    }
}
