#include "CabinetSimulator.h"

//==============================================================================
CabinetSimulator::CabinetParameters CabinetSimulator::getCabinetParameters (CabinetType type) noexcept
{
    switch (type)
    {
        case CabinetType::Open1x12:
            // Combo abierto: menos graves, agudos más presentes, resonancia suave.
            return { 90.0f, 5200.0f, 120.0f, 4.0f, 2500.0f, 2.0f, 800.0f, -1.5f, 1.0f };

        case CabinetType::Closed2x12:
            return { 80.0f, 5000.0f, 105.0f, 5.0f, 2200.0f, 2.5f, 750.0f, -2.0f, 0.5f };

        case CabinetType::Modern4x12:
            // Más graves y más agudos, medios más recortados.
            return { 95.0f, 6000.0f, 100.0f, 4.0f, 3000.0f, 3.5f, 900.0f, -3.5f, 0.0f };

        case CabinetType::Closed4x12:
            return { 85.0f, 4800.0f, 110.0f, 5.5f, 2400.0f, 3.0f, 800.0f, -2.5f, 0.5f };

        case CabinetType::Direct:
        default:
            return { 20.0f, 20000.0f, 100.0f, 0.0f, 2000.0f, 0.0f, 800.0f, 0.0f, 0.0f };
    }
}

//==============================================================================
void CabinetSimulator::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    numChannels = juce::jmax ((juce::uint32) 1, spec.numChannels);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = sampleRate;
    filterSpec.numChannels = numChannels;

    lowCutFilter1.prepare (filterSpec);
    lowCutFilter2.prepare (filterSpec);
    highCutFilter1.prepare (filterSpec);
    highCutFilter2.prepare (filterSpec);
    resonanceFilter.prepare (filterSpec);
    presenceFilter.prepare (filterSpec);
    scoopFilter.prepare (filterSpec);

    dirty = true;
    updateCoefficientsIfNeeded();
    reset();

    prepared = true;
}

void CabinetSimulator::reset()
{
    lowCutFilter1.reset();
    lowCutFilter2.reset();
    highCutFilter1.reset();
    highCutFilter2.reset();
    resonanceFilter.reset();
    presenceFilter.reset();
    scoopFilter.reset();
}

//==============================================================================
void CabinetSimulator::setCabinetType (CabinetType newType)
{
    if (newType != cabinetType)
    {
        cabinetType = newType;
        markDirty();
    }
}

void CabinetSimulator::setEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled != enabled)
    {
        enabled = shouldBeEnabled;
        markDirty();
    }
}

void CabinetSimulator::setCharacter (float character01)
{
    const float newCharacter = juce::jlimit (0.0f, 1.0f, character01);

    if (newCharacter != character)
    {
        character = newCharacter;
        markDirty();
    }
}

void CabinetSimulator::setLowCutHz (float frequencyHz)
{
    lowCutOverridden = true;
    lowCutHz = juce::jlimit (20.0f, 1000.0f, frequencyHz);
    markDirty();
}

void CabinetSimulator::setHighCutHz (float frequencyHz)
{
    highCutOverridden = true;
    highCutHz = juce::jlimit (1000.0f, 18000.0f, frequencyHz);
    markDirty();
}

void CabinetSimulator::setResonanceGainDb (float gainDb)
{
    resonanceOverridden = true;
    resonanceGainDb = juce::jlimit (0.0f, 12.0f, gainDb);
    markDirty();
}

void CabinetSimulator::clearOverrides()
{
    lowCutOverridden = false;
    highCutOverridden = false;
    resonanceOverridden = false;
    markDirty();
}

//==============================================================================
void CabinetSimulator::updateCoefficientsIfNeeded()
{
    if (! dirty)
        return;

    dirty = false;

    const auto p = getCabinetParameters (cabinetType);

    // El carácter desplaza el corte de agudos (0.75x .. 1.35x) y refuerza el pico de medios.
    const float characterHighCut = juce::jmap (character, 0.0f, 1.0f, 0.75f, 1.35f);
    const float characterPresence = juce::jmap (character, 0.0f, 1.0f, 0.6f, 1.4f);

    const float effectiveLowCut = lowCutOverridden ? lowCutHz : p.lowCutHz;
    const float effectiveHighCut = highCutOverridden ? highCutHz : p.highCutHz * characterHighCut;
    const float effectiveResonanceGainDb = resonanceOverridden ? resonanceGainDb : p.resonanceGainDb;
    const float effectivePresenceGainDb = p.presenceGainDb * characterPresence;

    outputTrimGain = juce::Decibels::decibelsToGain (p.outputTrimDb);

    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    // Paso-altos en dos etapas: caída de graves más parecida a un cono real.
    lowCutFilter1.coefficients = Coefficients::makeHighPass (sampleRate, effectiveLowCut, 0.60f);
    lowCutFilter2.coefficients = Coefficients::makeHighPass (sampleRate, effectiveLowCut * 0.70f, 0.50f);

    // Paso-bajos en dos etapas: caída progresiva de agudos (cono + micrófono).
    highCutFilter1.coefficients = Coefficients::makeLowPass (sampleRate, effectiveHighCut, 0.70f);
    highCutFilter2.coefficients = Coefficients::makeLowPass (sampleRate, effectiveHighCut * 1.35f, 0.60f);

    // Resonancia del altavoz (Thiele-Small) y pico de medios-altos ("honk").
    resonanceFilter.coefficients = Coefficients::makePeakFilter (sampleRate,
                                                                p.resonanceHz,
                                                                1.30f,
                                                                juce::Decibels::decibelsToGain (effectiveResonanceGainDb));

    presenceFilter.coefficients = Coefficients::makePeakFilter (sampleRate,
                                                               p.presenceHz,
                                                               1.00f,
                                                               juce::Decibels::decibelsToGain (effectivePresenceGainDb));

    // Muesca de medios: el hueco característico del cono.
    scoopFilter.coefficients = Coefficients::makePeakFilter (sampleRate,
                                                            p.scoopHz,
                                                            0.80f,
                                                            juce::Decibels::decibelsToGain (p.scoopGainDb));
}

//==============================================================================
void CabinetSimulator::process (juce::AudioBuffer<float>& buffer)
{
    if (! prepared)
        return;

    updateCoefficientsIfNeeded();

    // Sin pantalla: no se aplica nada (respuesta plana).
    if (! enabled || cabinetType == CabinetType::Direct)
        return;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), (int) numChannels);
    const float trim = outputTrimGain;

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = data[i];

            x = lowCutFilter1.processSample (ch, x);
            x = lowCutFilter2.processSample (ch, x);
            x = resonanceFilter.processSample (ch, x);
            x = scoopFilter.processSample (ch, x);
            x = presenceFilter.processSample (ch, x);
            x = highCutFilter1.processSample (ch, x);
            x = highCutFilter2.processSample (ch, x);

            data[i] = x * trim;
        }
    }
}
