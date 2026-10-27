#pragma once

#include <JuceHeader.h>

/**
    Puerta de ruido / expansor descendente para guitarra.

    Funcionamiento:
      - Seguidor de envolvente (envelope follower) sobre el máximo de |x| de todos los
        canales, con un pasa-altos en la cadena lateral para que los graves no abran la
        puerta por sí solos.
      - Por encima del umbral (threshold) la puerta está abierta y no toca la señal.
      - Por debajo del umbral se aplica una reducción de ganancia proporcional a la
        distancia al umbral y al ratio (comportamiento de expansor, no de puerta dura):
        reduccion_dB = (env_dB - threshold_dB) * (ratio - 1)
      - Histéresis + tiempo de retención (hold) para evitar que la puerta "castañetee".
      - Ataque y liberación independientes para la apertura y el cierre.

    Los parámetros del plugin llegan en dB (-80 .. -20) y ratio 1 .. 20. Con ratio = 1
    la reducción es nula, es decir, la puerta queda desactivada de forma efectiva.
*/
class NoiseGate
{
public:
    NoiseGate() = default;

    //==============================================================================
    /** Prepara la puerta para un contexto de procesado. */
    void prepare (const juce::dsp::ProcessSpec& spec);

    /** Reinicia el estado interno (envolvente, ganancia, contadores). */
    void reset();

    /** Procesa el buffer completo (in place). */
    void process (juce::AudioBuffer<float>& buffer);

    //==============================================================================
    /** Umbral en dB (-80 .. -20). */
    void setThreshold (float thresholdDb);

    /** Ratio del expansor (1 .. 20). Con 1 no se aplica reducción. */
    void setRatio (float ratio);

    /** Tiempo de apertura en milisegundos. */
    void setAttackMs (float milliseconds);

    /** Tiempo de cierre en milisegundos. */
    void setReleaseMs (float milliseconds);

    /** Tiempo de retención antes de empezar a cerrar, en milisegundos. */
    void setHoldMs (float milliseconds);

    /** Atenuación máxima aplicada cuando la puerta está cerrada, en dB. */
    void setRangeDb (float decibels);

    /** Frecuencia del pasa-altos de la cadena lateral. */
    void setSidechainHighPassHz (float frequencyHz);

    /** Activa o desactiva la puerta. */
    void setEnabled (bool shouldBeEnabled);

    //==============================================================================
    float getThreshold() const noexcept { return thresholdDb; }
    float getRatio() const noexcept { return ratio; }
    float getAttackMs() const noexcept { return attackMs; }
    float getReleaseMs() const noexcept { return releaseMs; }
    float getHoldMs() const noexcept { return holdMs; }
    float getRangeDb() const noexcept { return rangeDb; }
    bool isEnabled() const noexcept { return enabled; }

    /** Reducción de ganancia actual en dB (0 = abierta). Útil para un medidor en la GUI. */
    float getGainReductionDb() const noexcept { return currentGainReductionDb; }

    /** Envolvente actual en dB (útil para un medidor en la GUI). */
    float getEnvelopeDb() const noexcept { return currentEnvelopeDb; }

    /** Indica si la puerta está abierta en este momento. */
    bool isOpen() const noexcept { return gateOpen; }

private:
    //==============================================================================
    void updateCoefficientsIfNeeded();

    juce::dsp::IIR::Filter<float> sidechainHighPass;

    double sampleRate = 44100.0;
    juce::uint32 numChannels = 2;
    bool prepared = false;
    bool coefficientsDirty = true;

    bool enabled = true;

    float thresholdDb = -60.0f;
    float ratio = 4.0f;
    float attackMs = 2.0f;
    float releaseMs = 120.0f;
    float holdMs = 40.0f;
    float rangeDb = 80.0f;
    float sidechainHz = 90.0f;

    // Coeficientes de los filtros de un polo (0 = instantáneo, 1 = congelado).
    float envelopeAttackCoeff = 0.0f;
    float envelopeReleaseCoeff = 0.0f;
    float openCoeff = 0.0f;
    float closeCoeff = 0.0f;
    int holdSamples = 0;

    // Estado
    float envelope = 0.0f;
    float gain = 1.0f;
    int holdCounter = 0;
    bool gateOpen = false;

    float currentGainReductionDb = 0.0f;
    float currentEnvelopeDb = -100.0f;

    JUCE_LEAK_DETECTOR (NoiseGate)
};
