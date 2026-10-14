#include "DistortionAmp.h"

//==============================================================================
void DistortionAmp::prepare (const juce::dsp::ProcessSpec& spec)
{
    AmpModel::prepare (spec);

    // Pila de tonos moderna (más rango) y pantalla 4x12 moderna.
    toneStack.setStyle (ToneStack::Style::Modern);
    cabinet.setCabinetType (CabinetSimulator::CabinetType::Modern4x12);
    cabinet.setCharacter (0.65f);

    stage1.prepare (spec);
    stage1.setCharacter (TubeStage::Character::Triode12AX7);
    stage1.setHardness (1.85f);
    stage1.setBias (0.18f);
    stage1.setCathodeBypass (0.45f);
    stage1.setOutputLevel (1.0f);

    stage2.prepare (spec);
    stage2.setCharacter (TubeStage::Character::TriodeEL34);
    stage2.setHardness (2.10f);
    stage2.setBias (0.22f);
    stage2.setCathodeBypass (0.25f);
    stage2.setOutputLevel (1.0f);

    stage3.prepare (spec);
    stage3.setCharacter (TubeStage::Character::Pentode6L6);
    stage3.setHardness (2.45f);
    stage3.setBias (0.26f);
    stage3.setCathodeBypass (0.10f);
    stage3.setOutputLevel (1.0f);

    juce::dsp::ProcessSpec filterSpec = spec;
    filterSpec.sampleRate = getSampleRate();
    filterSpec.numChannels = (juce::uint32) juce::jmax (1, getNumChannels());

    inputHighPass.prepare (filterSpec);
    inputLowPass.prepare (filterSpec);
    interstage1HighPass.prepare (filterSpec);
    interstage1LowPass.prepare (filterSpec);
    interstage2HighPass.prepare (filterSpec);
    interstage2LowPass.prepare (filterSpec);
    dcBlocker.prepare (filterSpec);
    outputLowPass.prepare (filterSpec);

    filtersDirty = true;
    updateFilters();

    reset();
}

void DistortionAmp::reset()
{
    AmpModel::reset();

    stage1.reset();
    stage2.reset();
    stage3.reset();
    inputHighPass.reset();
    inputLowPass.reset();
    interstage1HighPass.reset();
    interstage1LowPass.reset();
    interstage2HighPass.reset();
    interstage2LowPass.reset();
    dcBlocker.reset();
    outputLowPass.reset();
}

//==============================================================================
void DistortionAmp::setDriveTrimDb (float db)
{
    const float newDb = juce::jlimit (-12.0f, 12.0f, db);

    if (newDb != driveTrimDb)
        driveTrimDb = newDb;
}

void DistortionAmp::setTightHz (float frequencyHz)
{
    const float newFreq = juce::jlimit (40.0f, 400.0f, frequencyHz);

    if (newFreq != tightHz)
    {
        tightHz = newFreq;
        filtersDirty = true;
    }
}

void DistortionAmp::setClipAmount (float amount01)
{
    clipAmount = juce::jlimit (0.0f, 1.0f, amount01);
}

void DistortionAmp::setCabinetType (CabinetSimulator::CabinetType type)
{
    cabinet.setCabinetType (type);
}

//==============================================================================
void DistortionAmp::updateFilters()
{
    if (! filtersDirty || ! isPrepared())
        return;

    filtersDirty = false;

    const double sr = getSampleRate();
    const float maxFreq = (float) (sr * 0.45);

    using Coefficients = juce::dsp::IIR::Coefficients<float>;

    // Filtro "tight": con tanta ganancia, los subgraves emborronan la distorsión.
    inputHighPass.coefficients = Coefficients::makeHighPass (sr, tightHz, 0.75f);
    inputLowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 9000.0f), 0.70f);

    // Acoplo entre la primera y la segunda etapa.
    interstage1HighPass.coefficients = Coefficients::makeHighPass (sr, 120.0f, 0.60f);
    interstage1LowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 6500.0f), 0.70f);

    // Acoplo entre la segunda etapa y la etapa de clipping.
    interstage2HighPass.coefficients = Coefficients::makeHighPass (sr, 150.0f, 0.60f);
    interstage2LowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 5500.0f), 0.70f);

    // Eliminador de continua (las tres etapas son asimétricas).
    dcBlocker.coefficients = Coefficients::makeHighPass (sr, 20.0f, 0.50f);

    // Suavizado final: la distorsión alta genera mucho "fizz" por encima de 8.5 kHz.
    outputLowPass.coefficients = Coefficients::makeLowPass (sr, juce::jmin (maxFreq, 8500.0f), 0.60f);
}

//==============================================================================
void DistortionAmp::processSaturation (juce::AudioBuffer<float>& buffer)
{
    updateFilters();

    // Máxima ganancia del plugin: tres etapas de válvula en cascada.
    const float drive1Db = gainToDriveDb (12.0f, 42.0f) + driveTrimDb;
    const float drive2Db = gainToDriveDb (6.0f, 30.0f);
    const float drive3Db = gainToDriveDb (3.0f, 18.0f);

    stage1.setDriveDb (drive1Db);
    stage2.setDriveDb (drive2Db);
    stage3.setDriveDb (drive3Db);

    // El recorte cúbico se alimenta cada vez más fuerte según el control de ganancia.
    const float clipDrive = juce::Decibels::decibelsToGain (gainToDriveDb (0.0f, 12.0f) * clipAmount);

    // Compensación: +4 dB fijos y +0.06 dB por dB de drive. Con tanta saturación el
    // recorte cúbico y la tercera válvula limitan la señal, así que se recupera nivel a
    // medida que sube la ganancia para que el canal no se vuelva más flojo al saturar.
    const float makeup = juce::Decibels::decibelsToGain (4.0f + 0.06f * juce::jmax (0.0f, drive1Db));

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (buffer.getNumChannels(), getNumChannels());

    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* data = buffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            float x = inputHighPass.processSample (ch, data[i]);
            x = inputLowPass.processSample (ch, x);

            x = stage1.processSample (ch, x);

            x = interstage1HighPass.processSample (ch, x);
            x = interstage1LowPass.processSample (ch, x);

            x = stage2.processSample (ch, x);

            x = interstage2HighPass.processSample (ch, x);
            x = interstage2LowPass.processSample (ch, x);

            // Recorte cúbico agresivo (etapa de diodos): aporta compresión adicional.
            x = TubeStage::softClipCubic (x * clipDrive);

            x = stage3.processSample (ch, x);

            x = dcBlocker.processSample (ch, x);
            x = outputLowPass.processSample (ch, x);

            data[i] = x * makeup;
        }
    }
}
