#include "CrunchAmp.h"

//==============================================================================
void CrunchAmp::prepare (const juce::dsp::ProcessSpec& spec)
{
    AmpModel::prepare (spec);

    // Pila de tonos británica y pantalla 4x12 cerrada.
    toneStack.setStyle (ToneStack::Style::Marshall);
    cabinet.setCabinetType (CabinetSimulator::CabinetType::Closed4x12);
    cabinet.setCharacter (0.55f);

    stage1.prepare (spec);
    stage1.setCharacter (TubeStage::Character::Triode12AX7);
    stage1.setHardness (1.35f);
    stage1.setBias (0.14f);
    stage1.setCathodeBypass (0.35f);        // realce de medios-bajos británico
    stage1.setOutputLevel (1.0f);

    stage2.prepare (spec);
    stage2.setCharacter (TubeStage::Character::Triode12AX7);
    stage2.setHardness (1.70f);
    stage2.setBias (0.20f);
    stage2.setCathodeBypass (0.15f);
    stage2.setOutputLevel (1.0f);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = getSampleRate();
    filterSpec.numChannels = (juce::uint32) juce::jmax (1, getNumChannels());

    inputHighPass.prepare (filterSpec);
    brightShelf.prepare (filterSpec);
    interstageHighPass.prepare (filterSpec);
    interstageLowPass.prepare (filterSpec);
    dcBlocker.prepare (filterSpec);
    outputLowPass.prepare (filterSpec);

    filtersDirty = true;
    updateFilters();

    reset();
}

void CrunchAmp::reset()
{
    AmpModel::reset();

    stage1.reset();
    stage2.reset();
    inputHighPass.reset();
    brightShelf.reset();
    interstageHighPass.reset();
    interstageLowPass.reset();
    dcBlocker.reset();
    outputLowPass.reset();
}

//==============================================================================
void CrunchAmp::setBright (bool shouldBeBright)
{
    if (shouldBeBright != bright)
    {
        bright = shouldBeBright;
        filtersDirty = true;
    }
}

void CrunchAmp::setDriveTrimDb (float db)
{
    const float newDb = juce::jlimit (-12.0f, 12.0f, db);

    if (newDb != driveTrimDb)
        driveTrimDb = newDb;
}

void CrunchAmp::setTightHz (float frequencyHz)
{
    const float newFreq = juce::jlimit (30.0f, 300.0f, frequencyHz);

    if (newFreq != tightHz)
    {
        tightHz = newFreq;
        filtersDirty = true;
    }
}

void CrunchAmp::setCabinetType (CabinetSimulator::CabinetType type)
{
    cabinet.setCabinetType (type);
}

//==============================================================================
void CrunchAmp::updateFilters()
{
    if (! filtersDirty || ! isPrepared())
        return;

    filtersDirty = false;

    const double sr = getSampleRate();
    const float maxFreq = (float) (sr * 0.45);

    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    // Recorte de subgraves: evita que la distorsión se vuelva "embarrada".
    inputHighPass.coefficients = Coefficients::makeHighPass (sr, tightHz, 0.70f);

    // Condensador de agudos: +2.5 dB por encima de 3 kHz.
    brightShelf.coefficients = Coefficients::makeHighShelf (sr, 3000.0f, 0.70f,
                                                           juce::Decibels::decibelsToGain (bright ? 2.5f : 0.0f));

    // Acoplo entre etapas: 90 Hz + 7.5 kHz define la banda útil del previo.
    interstageHighPass.coefficients = Coefficients::makeHighPass (sr, 90.0f, 0.60f);
    interstageLowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 7500.0f), 0.70f);

    // Eliminador de continua tras la saturación asimétrica.
    dcBlocker.coefficients = Coefficients::makeHighPass (sr, 18.0f, 0.50f);

    // Suavizado final: la distorsión genera mucho contenido por encima de 10 kHz.
    outputLowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 10000.0f), 0.60f);
}

//==============================================================================
void CrunchAmp::processSaturation (juce::AudioBuffer<float>& buffer)
{
    updateFilters();

    // Primera válvula: de +6 dB a +36 dB (entra en saturación pronto).
    const float drive1Db = gainToDriveDb (6.0f, 36.0f) + driveTrimDb;

    // Segunda válvula: de 0 dB a +12 dB (redondea y comprime).
    const float drive2Db = gainToDriveDb (0.0f, 12.0f);

    stage1.setDriveDb (drive1Db);
    stage2.setDriveDb (drive2Db);

    // Compensación de nivel: +5 dB fijos y -0.03 dB por dB de drive de la primera etapa.
    // Así el canal crunch mantiene un nivel parecido al limpio cuando se sube la ganancia
    // (la saturación ya comprime por sí sola).
    const float makeup = juce::Decibels::decibelsToGain (5.0f - 0.03f * juce::jmax (0.0f, drive1Db));

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getNumChannels());

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = inputHighPass.processSample (ch, data[i]);
            x = brightShelf.processSample (ch, x);

            x = stage1.processSample (ch, x);

            x = interstageHighPass.processSample (ch, x);
            x = interstageLowPass.processSample (ch, x);

            x = stage2.processSample (ch, x);

            x = dcBlocker.processSample (ch, x);
            x = outputLowPass.processSample (ch, x);

            data[i] = x * makeup;
        }
    }
}
