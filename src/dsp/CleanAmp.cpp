#include "CleanAmp.h"

//==============================================================================
void CleanAmp::prepare (const juce::dsp::ProcessSpec& spec)
{
    AmpModel::prepare (spec);

    // Pila de tonos pasiva estilo Fender y pantalla 1x12 abierta de combo.
    toneStack.setStyle (ToneStack::Style::Fender);
    cabinet.setCabinetType (CabinetSimulator::CabinetType::Open1x12);
    cabinet.setCharacter (0.60f);

    tubeStage.prepare (spec);
    tubeStage.setCharacter (TubeStage::Character::Triode12AT7);
    tubeStage.setHardness (0.65f);          // saturación muy suave
    tubeStage.setBias (0.05f);
    tubeStage.setCathodeBypass (0.15f);
    tubeStage.setOutputLevel (1.0f);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = getSampleRate();
    filterSpec.numChannels = (juce::uint32) juce::jmax (1, getNumChannels());

    inputHighPass.prepare (filterSpec);
    brightShelf.prepare (filterSpec);
    dcBlocker.prepare (filterSpec);
    outputLowPass.prepare (filterSpec);

    filtersDirty = true;
    updateFilters();

    reset();
}

void CleanAmp::reset()
{
    AmpModel::reset();

    tubeStage.reset();
    inputHighPass.reset();
    brightShelf.reset();
    dcBlocker.reset();
    outputLowPass.reset();
}

//==============================================================================
void CleanAmp::setBright (bool shouldBeBright)
{
    if (shouldBeBright != bright)
    {
        bright = shouldBeBright;
        filtersDirty = true;
    }
}

void CleanAmp::setDriveTrimDb (float db)
{
    const float newDb = juce::jlimit (-12.0f, 12.0f, db);

    if (newDb != driveTrimDb)
        driveTrimDb = newDb;
}

void CleanAmp::setCabinetType (CabinetSimulator::CabinetType type)
{
    cabinet.setCabinetType (type);
}

//==============================================================================
void CleanAmp::updateFilters()
{
    if (! filtersDirty || ! isPrepared())
        return;

    filtersDirty = false;

    const double sr = getSampleRate();
    const float maxFreq = (float) (sr * 0.45);

    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    // 35 Hz: los subgraves no llegan al previo de guitarra.
    inputHighPass.coefficients = Coefficients::makeHighPass (sr, 35.0f, 0.60f);

    // Condensador de agudos del volumen: +3.5 dB por encima de 3 kHz.
    brightShelf.coefficients = Coefficients::makeHighShelf (sr, 3000.0f, 0.70f,
                                                           juce::Decibels::decibelsToGain (bright ? 3.5f : 0.0f));

    // Eliminador de continua (el bias del tubo la introduce).
    dcBlocker.coefficients = Coefficients::makeHighPass (sr, 18.0f, 0.50f);

    // Suavizado de agudos: un amplificador limpio no llega a 20 kHz.
    outputLowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 13000.0f), 0.60f);
}

//==============================================================================
void CleanAmp::processSaturation (juce::AudioBuffer<float>& buffer)
{
    updateFilters();

    // Rango de ganancia del canal limpio: -6 dB (muy limpio) a +21 dB (breakup suave).
    const float driveDb = gainToDriveDb (-6.0f, 21.0f) + driveTrimDb;
    tubeStage.setDriveDb (driveDb);

    // Compensación de nivel: +2 dB fijos y -0.3 dB por cada dB de drive, de modo que
    // subir la ganancia aporte saturación sin disparar el nivel de salida.
    const float makeup = juce::Decibels::decibelsToGain (2.0f - 0.30f * juce::jmax (0.0f, driveDb));

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getNumChannels());

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = inputHighPass.processSample (ch, data[i]);
            x = brightShelf.processSample (ch, x);
            x = tubeStage.processSample (ch, x);
            x = dcBlocker.processSample (ch, x);
            x = outputLowPass.processSample (ch, x);

            data[i] = x * makeup;
        }
    }
}
