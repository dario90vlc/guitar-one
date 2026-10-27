#pragma once

#include "AmpModel.h"
#include "TubeStage.h"

/**
    Amplificador de distorsión de alta ganancia (estilo moderno / high gain).

    Cadena de señal:
        entrada -> pasa-altos "tight" -> pasa-bajos previo
                -> etapa de triodo 1 (saturación fuerte) -> acoplo 1
                -> etapa de triodo 2 (saturación fuerte) -> acoplo 2
                -> recorte cúbico agresivo (diodos)
                -> etapa de triodo 3 (clipping duro) -> eliminador de continua
                -> pila de tonos moderna (más rango) -> pantalla 4x12 moderna -> volumen

    Es el modelo con más ganancia del plugin: las tres etapas no lineales en cascada
    producen compresión y armónicos muy marcados, con recortes de banda estrechos para
    evitar el "fizz" y mantener definición en acordes.
*/
class DistortionAmp : public AmpModel
{
public:
    DistortionAmp() = default;
    ~DistortionAmp() override = default;

    //==============================================================================
    void prepare (const juce::dsp::ProcessSpec& spec) override;
    void reset() override;

    juce::String getName() const override { return "Distortion"; }

    //==============================================================================
    /** Ajuste fino del drive en dB (por defecto 0). */
    void setDriveTrimDb (float db);

    /** Ajuste del filtro de entrada (cuánto se recortan los subgraves). */
    void setTightHz (float frequencyHz);

    /** Cantidad de recorte cúbico entre la segunda y la tercera etapa (0 .. 1). */
    void setClipAmount (float amount01);

    /** Permite cambiar la pantalla usada por este amplificador. */
    void setCabinetType (CabinetSimulator::CabinetType type);

    /** Acceso a las etapas de válvula para ajustes avanzados. */
    TubeStage& getStage1() noexcept { return stage1; }
    TubeStage& getStage2() noexcept { return stage2; }
    TubeStage& getStage3() noexcept { return stage3; }

protected:
    void processSaturation (juce::AudioBuffer<float>& buffer) override;

private:
    void updateFilters();

    TubeStage stage1;   // primera válvula: saturación fuerte
    TubeStage stage2;   // segunda válvula: saturación fuerte
    TubeStage stage3;   // tercera válvula: clipping duro

    juce::dsp::IIR::Filter<float> inputHighPass;       // recorte "tight" de subgraves
    juce::dsp::IIR::Filter<float> inputLowPass;        // quita agudos antes de saturar
    juce::dsp::IIR::Filter<float> interstage1HighPass;
    juce::dsp::IIR::Filter<float> interstage1LowPass;
    juce::dsp::IIR::Filter<float> interstage2HighPass;
    juce::dsp::IIR::Filter<float> interstage2LowPass;
    juce::dsp::IIR::Filter<float> dcBlocker;
    juce::dsp::IIR::Filter<float> outputLowPass;

    bool filtersDirty = true;
    float driveTrimDb = 0.0f;
    float tightHz = 100.0f;
    float clipAmount = 1.0f;

    JUCE_LEAK_DETECTOR (DistortionAmp)
};
