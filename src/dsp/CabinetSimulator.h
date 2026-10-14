#pragma once

#include <JuceHeader.h>

/**
    Simulador de pantalla de altavoz (cabinet) para amplificador de guitarra.

    Modela la respuesta en frecuencia característica de un cono de guitarra mediante
    una cadena de filtros biquad:

      - Paso-altos doble: simula la caída de graves del cono en caja cerrada/abierta.
      - Paso-bajos doble: simula la caída de agudos propia del altavoz (y del micrófono).
      - Resonancia de altavoz: pico de graves-medios (impedancia/Thiele-Small).
      - Pico de medios-altos: el "honk" característico del cono.
      - Muesca de medios: el hueco que deja el altavoz entre ambas zonas.

    El carácter (0 = oscuro/retro, 1 = brillante/moderno) desplaza el corte de agudos
    y la intensidad del pico de medios-altos.
*/
class CabinetSimulator
{
public:
    /** Tipo de pantalla: cambia la respuesta base del cono. */
    enum class CabinetType
    {
        Direct,         ///< Sin pantalla (respuesta plana, sólo nivel).
        Open1x12,       ///< 1x12 abierto: típico combo limpio (Fender).
        Closed2x12,     ///< 2x12 cerrado: medios más controlados.
        Closed4x12,     ///< 4x12 cerrado: pantalla tipo Marshall.
        Modern4x12      ///< 4x12 moderno: más graves y más agudos, medios recortados.
    };

    CabinetSimulator() = default;

    //==============================================================================
    /** Prepara los filtros para un contexto de procesado. */
    void prepare (const juce::dsp::ProcessSpec& spec);

    /** Reinicia el estado de los filtros. */
    void reset();

    /** Aplica la simulación de pantalla sobre el buffer (in place). */
    void process (juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /** Selecciona el tipo de pantalla. */
    void setCabinetType (CabinetType newType);

    CabinetType getCabinetType() const noexcept { return cabinetType; }

    /** Activa o desactiva la simulación (al desactivar sólo se aplica el nivel). */
    void setEnabled (bool shouldBeEnabled);

    bool isEnabled() const noexcept { return enabled; }

    /** Carácter tonal: 0 = oscuro/retro, 0.5 = neutro, 1 = brillante/moderno. */
    void setCharacter (float character01);

    float getCharacter() const noexcept { return character; }

    //==============================================================================
    /** Sobrescribe manualmente el corte de graves (Hz). */
    void setLowCutHz (float frequencyHz);

    /** Sobrescribe manualmente el corte de agudos (Hz). */
    void setHighCutHz (float frequencyHz);

    /** Sobrescribe manualmente la ganancia de la resonancia de graves (dB). */
    void setResonanceGainDb (float gainDb);

    /** Devuelve el ajuste manual a los valores del tipo de pantalla. */
    void clearOverrides();

    //==============================================================================
    float getLowCutHz() const noexcept { return lowCutHz; }
    float getHighCutHz() const noexcept { return highCutHz; }

private:
    //==============================================================================
    struct CabinetParameters
    {
        float lowCutHz;         ///< corte de graves principal
        float highCutHz;        ///< corte de agudos principal
        float resonanceHz;      ///< frecuencia de la resonancia del cono
        float resonanceGainDb;  ///< ganancia de la resonancia
        float presenceHz;       ///< pico de medios-altos ("honk")
        float presenceGainDb;
        float scoopHz;          ///< muesca de medios
        float scoopGainDb;
        float outputTrimDb;     ///< compensación de nivel de la pantalla
    };

    static CabinetParameters getCabinetParameters (CabinetType type) noexcept;

    void updateCoefficientsIfNeeded();
    void markDirty() noexcept { dirty = true; }

    juce::dsp::IIR::Filter<float> lowCutFilter1, lowCutFilter2;
    juce::dsp::IIR::Filter<float> highCutFilter1, highCutFilter2;
    juce::dsp::IIR::Filter<float> resonanceFilter;
    juce::dsp::IIR::Filter<float> presenceFilter;
    juce::dsp::IIR::Filter<float> scoopFilter;

    double sampleRate = 44100.0;
    juce::uint32 numChannels = 2;
    bool prepared = false;
    bool dirty = true;

    CabinetType cabinetType = CabinetType::Closed4x12;
    bool enabled = true;
    float character = 0.5f;

    bool lowCutOverridden = false;
    bool highCutOverridden = false;
    bool resonanceOverridden = false;

    float lowCutHz = 85.0f;
    float highCutHz = 4800.0f;
    float resonanceGainDb = 5.5f;

    float outputTrimGain = 1.0f;

    JUCE_LEAK_DETECTOR (CabinetSimulator)
};
