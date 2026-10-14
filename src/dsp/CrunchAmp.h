#pragma once

#include "AmpModel.h"
#include "TubeStage.h"

/**
    Amplificador crunch (estilo británico tipo Marshall "plexi" saturado).

    Cadena de señal:
        entrada -> pasa-altos de entrada -> condensador "bright"
                -> etapa de triodo 1 (más saturada) -> filtro de acoplo entre etapas
                -> etapa de triodo 2 -> eliminador de continua -> suavizado de agudos
                -> pila de tonos Marshall -> pantalla 4x12 cerrada -> volumen

    Trabaja con bastante más ganancia que el canal limpio: la primera válvula entra
    claramente en saturación y la segunda redondea y comprime el resultado.
*/
class CrunchAmp : public AmpModel
{
public:
    CrunchAmp() = default;
    ~CrunchAmp() override = default;

    //==============================================================================
    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;

    juce::String getName() const override { return "Crunch"; }

    //==============================================================================
    /** Condensador de agudos ("bright cap") del canal. */
    void setBright (bool shouldBeBright);
    bool isBright() const noexcept { return bright; }

    /** Ajuste fino del drive en dB (por defecto 0). */
    void setDriveTrimDb (float db);

    /** Ajuste del filtro de entrada (cuánto se recortan los subgraves). */
    void setTightHz (float frequencyHz);

    /** Permite cambiar la pantalla usada por este amplificador. */
    void setCabinetType (CabinetSimulator::CabinetType type);

    /** Acceso a las etapas de válvula para ajustes avanzados. */
    TubeStage& getStage1() noexcept { return stage1; }
    TubeStage& getStage2() noexcept { return stage2; }

protected:
    void processSaturation (juce::AudioBuffer<float>& buffer) override;

private:
    void updateFilters();

    TubeStage stage1;   // primera válvula: genera la mayor parte de la saturación
    TubeStage stage2;   // segunda válvula: redondea y comprime

    juce::dsp::IIR::Filter<float> inputHighPass;      // recorta subgraves ("tight")
    juce::dsp::IIR::Filter<float> brightShelf;        // condensador de agudos
    juce::dsp::IIR::Filter<float> interstageHighPass; // acoplo entre etapas
    juce::dsp::IIR::Filter<float> interstageLowPass;  // quita "fizz" entre etapas
    juce::dsp::IIR::Filter<float> dcBlocker;          // elimina continua
    juce::dsp::IIR::Filter<float> outputLowPass;      // suavizado final de agudos

    bool bright = true;
    bool filtersDirty = true;
    float driveTrimDb = 0.0f;
    float tightHz = 70.0f;

    JUCE_LEAK_DETECTOR (CrunchAmp)
};
