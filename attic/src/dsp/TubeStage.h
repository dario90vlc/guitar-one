#pragma once

#include <JuceHeader.h>

/**
    Etapa de válvula (triodo) con saturación no lineal.

    Modelo simplificado de una etapa de triodo de guitarra:
      - Función de transferencia no lineal y asimétrica (genera armónicos pares e impares).
      - Control de bias (punto de polarización de la reja) que controla la asimetría.
      - Región de corte (grid cutoff) con transición suave, como una válvula real.
      - Filtro de acoplo (condensador de acoplo) que elimina la componente continua
        introducida por el bias.
      - Filtro de reja (capacidad Miller) que redondea los agudos antes de saturar.
      - Realce de cátodo (bypass de cátodo) con un pico en los medios-bajos.

    Es un procesador totalmente "in place" y sin asignaciones de memoria en process(),
    por lo que es seguro para tiempo real.
*/
class TubeStage
{
public:
    /** Carácter de la válvula: ajusta dureza de saturación y bias por defecto. */
    enum class Character
    {
        Triode12AX7,   ///< Mucha ganancia, saturación media (válvula de previo clásica).
        Triode12AT7,   ///< Menos ganancia, saturación más suave y limpia.
        TriodeEL34,    ///< Saturación más dura y comprimida (etapa de potencia).
        Pentode6L6     ///< Intermedia, con corte más marcado.
    };

    TubeStage() = default;

    //==============================================================================
    /** Prepara la etapa para un contexto de procesado concreto. */
    void prepare (const juce::dsp::ProcessSpec& spec);

    /** Reinicia el estado interno de los filtros. */
    void reset();

    /** Procesa un buffer completo (in place). */
    void process (juce::AudioBuffer<float>& buffer);

    /** Procesa una única muestra de un canal concreto. */
    float processSample (int channel, float input) noexcept;

    //==============================================================================
    /** Ganancia de entrada de la etapa (lineal). */
    void setDrive (float driveLinear) noexcept;

    /** Ganancia de entrada de la etapa en decibelios. */
    void setDriveDb (float driveDb) noexcept;

    /** Punto de polarización (-1 .. 1). Valores positivos => saturación asimétrica. */
    void setBias (float newBias) noexcept;

    /** Dureza de la saturación (0.05 .. 6). A mayor valor, más compresión y armónicos. */
    void setHardness (float newHardness) noexcept;

    /** Nivel de salida de la etapa (lineal). */
    void setOutputLevel (float levelLinear) noexcept;

    /** Frecuencia del filtro de acoplo (elimina continua). Típicamente 15 - 40 Hz. */
    void setCouplingHighPassHz (float frequencyHz);

    /** Frecuencia del filtro de reja / Miller (redondea los agudos antes de saturar). */
    void setGridLowPassHz (float frequencyHz);

    /** Cantidad de realce de cátodo (0 .. 1) => 0 .. 4 dB de pico en medios-bajos. */
    void setCathodeBypass (float amount01);

    /** Selecciona el carácter de válvula (dureza y bias por defecto). */
    void setCharacter (Character newCharacter) noexcept;

    //==============================================================================
    float getDrive() const noexcept { return drive; }
    float getBias() const noexcept { return bias; }
    float getHardness() const noexcept { return hardness; }
    Character getCharacter() const noexcept { return character; }

    //==============================================================================
    /** Función de transferencia del triodo: no lineal, asimétrica y monótona en la zona útil. */
    static float triodeTransfer (float x, float bias, float hardness) noexcept;

    /** Saturación racional suave: y = x / (1 + k * |x|). Asíntota en +/- 1/k. */
    static float softSaturate (float x, float k) noexcept;

    /** Recorte cúbico suave (útil para etapas de clipping agresivo). */
    static float softClipCubic (float x) noexcept;

private:
    //==============================================================================
    void updateFilterCoefficientsIfNeeded();

    juce::dsp::IIR::Filter<float> couplingFilter;   // pasa-altos: condensador de acoplo
    juce::dsp::IIR::Filter<float> gridFilter;       // pasa-bajos: capacidad de reja (Miller)
    juce::dsp::IIR::Filter<float> cathodeFilter;    // pico: bypass de cátodo

    double sampleRate = 44100.0;
    juce::uint32 numChannels = 2;
    bool prepared = false;
    bool filtersDirty = true;

    float drive = 1.0f;
    float bias = 0.12f;
    float hardness = 1.2f;
    float outputLevel = 1.0f;

    float couplingHz = 25.0f;
    float gridLowPassHz = 14000.0f;
    float cathodeAmount = 0.0f;
    float cathodeFreqHz = 750.0f;

    Character character = Character::Triode12AX7;

    JUCE_LEAK_DETECTOR (TubeStage)
};
