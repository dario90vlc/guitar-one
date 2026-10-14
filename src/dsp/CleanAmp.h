#pragma once

#include "AmpModel.h"
#include "TubeStage.h"

/**
    Amplificador limpio (estilo combo americano tipo Fender).

    Cadena de señal:
        entrada -> pasa-altos de entrada -> condensador "bright" -> etapa de triodo suave
                -> eliminador de continua -> suavizado de agudos -> pila de tonos Fender
                -> pantalla 1x12 abierta -> volumen

    El tubo trabaja casi siempre en su zona lineal: sólo satura suavemente cuando el
    control de ganancia se sube, aportando compresión y armónicos de forma progresiva.
*/
class CleanAmp : public AmpModel
{
public:
    CleanAmp() = default;
    ~CleanAmp() override = default;

    //==============================================================================
    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;

    juce::String getName() const override { return "Clean"; }

    //==============================================================================
    /** Condensador de agudos ("bright cap") del control de volumen. */
    void setBright (bool shouldBeBright);
    bool isBright() const noexcept { return bright; }

    /** Ajuste fino del drive en dB (por defecto 0). */
    void setDriveTrimDb (float db);

    /** Permite cambiar la pantalla usada por este amplificador. */
    void setCabinetType (CabinetSimulator::CabinetType type);

    /** Acceso a la etapa de válvula para ajustes avanzados. */
    TubeStage& getTubeStage() noexcept { return tubeStage; }

protected:
    void processSaturation (juce::AudioBuffer<float>& buffer) override;

private:
    void updateFilters();

    TubeStage tubeStage;

    juce::dsp::IIR::Filter<float> inputHighPass;   // quita subgraves antes del tubo
    juce::dsp::IIR::Filter<float> brightShelf;     // condensador de agudos
    juce::dsp::IIR::Filter<float> dcBlocker;       // elimina continua del bias
    juce::dsp::IIR::Filter<float> outputLowPass;   // suaviza el extremo agudo

    bool bright = true;
    bool filtersDirty = true;
    float driveTrimDb = 0.0f;

    JUCE_LEAK_DETECTOR (CleanAmp)
};
