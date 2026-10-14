#pragma once

#include <JuceHeader.h>

/**
    Pila de tonos (tone stack) de amplificador de guitarra.

    Implementa el comportamiento de una pila de tonos pasiva clásica usando cuatro
    filtros biquad en cascada:

      - Bass     : filtro shelf de graves (frecuencia y rango según el estilo).
      - Mid      : filtro de pico en los medios.
      - Treble   : filtro shelf de agudos.
      - Presence : filtro de pico estrecho en agudos altos (etapa de potencia).

    Además modela la interacción típica de las pilas pasivas (subir graves deshincha
    los medios, y la presencia comparte banda con los agudos) y aplica una compensación
    de nivel para que mover los controles no cambie la sonoridad percibida.

    Todos los coeficientes se recalculan únicamente cuando cambia algún control,
    nunca por muestra, y no se reserva memoria en process().
*/
class ToneStack
{
public:
    /** Estilo de pila de tonos: cambia frecuencias, Q, rangos e interacción. */
    enum class Style
    {
        Fender,     ///< Pasiva tipo Fender: interactiva, graves cálidos, agudos brillantes.
        Marshall,   ///< Tipo Marshall: medios presentes y más rango en presencia.
        Modern      ///< Moderna: más rango total, respuesta más ancha y agresiva.
    };

    ToneStack() = default;

    //==============================================================================
    /** Prepara los filtros para un contexto de procesado. */
    void prepare (const juce::dsp::ProcessSpec& spec);

    /** Reinicia el estado de los filtros. */
    void reset();

    /** Aplica toda la cadena de tono sobre el buffer (in place). */
    void process (juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /** Estilo de pila de tonos. */
    void setStyle (Style newStyle);
    Style getStyle() const noexcept { return style; }

    /** Graves, 0 .. 1 (0.5 = plano). */
    void setBass (float value01);

    /** Medios, 0 .. 1 (0.5 = plano). */
    void setMid (float value01);

    /** Agudos, 0 .. 1 (0.5 = plano). */
    void setTreble (float value01);

    /** Presencia, 0 .. 1 (0.5 = plano). */
    void setPresence (float value01);

    /** Ajusta los cuatro controles de una vez (evita recalcular cuatro veces). */
    void setControls (float bass01, float mid01, float treble01, float presence01);

    //==============================================================================
    float getBass() const noexcept { return bass; }
    float getMid() const noexcept { return mid; }
    float getTreble() const noexcept { return treble; }
    float getPresence() const noexcept { return presence; }

    /** Ganancia de compensación aplicada a la salida de la pila (lineal). */
    float getOutputTrimGain() const noexcept { return outputTrimGain; }

private:
    //==============================================================================
    /** Parámetros de un estilo de pila de tonos. */
    struct StyleParameters
    {
        float bassFreq, bassQ, bassRangeDb;
        float midFreq, midQ, midRangeDb;
        float trebleFreq, trebleQ, trebleRangeDb;
        float presenceFreq, presenceQ, presenceRangeDb;
        float interaction;      ///< 0 = sin interacción, 1 = muy interactiva
        float outputTrimDb;     ///< compensación base del estilo
    };

    static StyleParameters getStyleParameters (Style styleToUse) noexcept;

    void updateCoefficientsIfNeeded();
    void markDirty() noexcept { dirty = true; }

    juce::dsp::IIR::Filter<float> bassFilter;
    juce::dsp::IIR::Filter<float> midFilter;
    juce::dsp::IIR::Filter<float> trebleFilter;
    juce::dsp::IIR::Filter<float> presenceFilter;

    double sampleRate = 44100.0;
    juce::uint32 numChannels = 2;
    bool prepared = false;
    bool dirty = true;

    Style style = Style::Fender;

    float bass = 0.5f;
    float mid = 0.5f;
    float treble = 0.5f;
    float presence = 0.5f;

    float outputTrimGain = 1.0f;

    JUCE_LEAK_DETECTOR (ToneStack)
};
