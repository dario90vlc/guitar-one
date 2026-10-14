#pragma once

#include <JuceHeader.h>

#include "ToneStack.h"
#include "CabinetSimulator.h"

/**
    Clase base abstracta para los modelos de amplificador de guitarra.

    Define la interfaz común (prepare / process / reset) y la cadena de procesado
    compartida por todos los amplificadores:

        entrada -> [ saturación propia del amplificador ] -> pila de tonos
                -> simulador de pantalla -> control de volumen

    Las clases derivadas sólo deben implementar processSaturation(), que es donde
    vive el carácter no lineal de cada amplificador (etapas de válvula, clipping, etc.).

    Los parámetros se expresan siempre en el rango normalizado 0 .. 1, que es lo que
    entrega el AudioProcessorValueTreeState del plugin.

    Todo el procesado es "in place", sin asignaciones de memoria, y los coeficientes
    de los filtros sólo se recalculan cuando algún control cambia.
*/
class AmpModel
{
public:
    AmpModel() = default;
    virtual ~AmpModel() = default;

    //==============================================================================
    /** Prepara el modelo para un contexto de procesado (frecuencia de muestreo, canales). */
    virtual void prepare (const juce::dsp::ProcessSpec& spec);

    /** Procesa el buffer completo: saturación + pila de tonos + pantalla + volumen. */
    virtual void process (juce::AudioBuffer<float>& buffer);

    /** Reinicia el estado interno (filtros, osciladores, envolventes). */
    virtual void reset();

    //==============================================================================
    /** Ganancia de entrada del amplificador (0 .. 1). */
    void setGain (float value01);

    /** Graves (0 .. 1, 0.5 = plano). */
    void setBass (float value01);

    /** Medios (0 .. 1, 0.5 = plano). */
    void setMid (float value01);

    /** Agudos (0 .. 1, 0.5 = plano). */
    void setTreble (float value01);

    /** Presencia (0 .. 1, 0.5 = plano). */
    void setPresence (float value01);

    /** Volumen de salida (0 .. 1). */
    void setVolume (float value01);

    //==============================================================================
    float getGain() const noexcept { return gain; }
    float getBass() const noexcept { return bass; }
    float getMid() const noexcept { return mid; }
    float getTreble() const noexcept { return treble; }
    float getPresence() const noexcept { return presence; }
    float getVolume() const noexcept { return volume; }

    //==============================================================================
    /** Nombre legible del modelo de amplificador. */
    virtual juce::String getName() const = 0;

    /** Acceso a la pila de tonos (útil para la GUI o para ajustes finos). */
    ToneStack& getToneStack() noexcept { return toneStack; }

    /** Acceso al simulador de pantalla (útil para la GUI o para ajustes finos). */
    CabinetSimulator& getCabinet() noexcept { return cabinet; }

    bool isPrepared() const noexcept { return prepared; }
    double getSampleRate() const noexcept { return sampleRate; }
    int getNumChannels() const noexcept { return (int) numChannels; }

protected:
    //==============================================================================
    /** Etapa no lineal propia de cada amplificador. Se ejecuta antes de la pila de tonos. */
    virtual void processSaturation (juce::AudioBuffer<float>& buffer) = 0;

    /** Se llama cuando alguno de los controles comunes ha cambiado. */
    virtual void ampParametersChanged() {}

    /** Aplica a la pila de tonos y al volumen los valores actuales si han cambiado. */
    void updateParametersIfNeeded();

    /** Convierte el control de ganancia (0 .. 1) a decibelios de drive dentro de un rango. */
    float gainToDriveDb (float minDb, float maxDb) const noexcept;

    /** Aplica el volumen de salida con suavizado por muestra. */
    void applyOutputVolume (juce::AudioBuffer<float>& buffer);

    /** Convierte el control de volumen (0 .. 1) a ganancia lineal con taper de audio. */
    static float volumeToLinearGain (float volume01) noexcept;

    /** Aplica una ganancia constante a todos los canales del buffer. */
    static void applyGain (juce::AudioBuffer<float>& buffer, float gainLinear) noexcept;

    //==============================================================================
    ToneStack toneStack;
    CabinetSimulator cabinet;

    juce::dsp::ProcessSpec currentSpec {};
    double sampleRate = 44100.0;
    int maximumBlockSize = 512;
    juce::uint32 numChannels = 2;

    bool prepared = false;
    bool parametersDirty = true;

    float gain = 0.5f;
    float bass = 0.5f;
    float mid = 0.5f;
    float treble = 0.5f;
    float presence = 0.5f;
    float volume = 0.7f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> volumeSmoothed;

    JUCE_LEAK_DETECTOR (AmpModel)
};
