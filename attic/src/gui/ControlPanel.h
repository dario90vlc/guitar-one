#pragma once

#include <JuceHeader.h>
#include "AmpKnob.h"

//==============================================================================
/**
    Panel de controles del amplificador.

    Contiene:
    - Los seis knobs del canal: Gain, Bass, Mid, Treble, Presence, Volume.
    - El selector de modelo (Clean / Crunch / Distortion) con switches tipo
      toggle (y un ComboBox oculto que mantiene el binding con el APVTS).
    - Los controles de Noise Gate (umbral, ratio y switch de activacion).
    - El control de Output Level.

    El plugin tiene un juego de parametros por modelo (cleanGain, crunchGain,
    distGain...). Al cambiar de modelo, este panel reengancha los seis knobs
    principales al juego de parametros correspondiente, de modo que el usuario
    siempre ve "Gain / Bass / Mid / Treble / Presence / Volume".
*/
class ControlPanel : public juce::Component,
                     private juce::ComboBox::Listener,
                     private juce::Button::Listener,
                     private juce::Timer
{
public:
    explicit ControlPanel(juce::AudioProcessorValueTreeState& stateToControl);
    ~ControlPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Escala de la interfaz (1.0 = diseno de 900x500). */
    void setUiScale(float newScale);
    float getUiScale() const noexcept { return uiScale; }

    /** Modelo activo (0 = Clean, 1 = Crunch, 2 = Distortion). */
    int getCurrentModel() const noexcept { return currentModel; }

    /** Fuerza la lectura del estado actual del APVTS (tras cargar un preset). */
    void refreshFromParameters();

    /** Actualiza el LED del noise gate (estado logico, sin senal). */
    void setGateActive(bool isActive);

    /** Medidor en vivo de la puerta: si esta abierta y el nivel de entrada. */
    void setGateMeter(bool gateIsOpen, float inputLevel);

    //== Acceso a los controles (para attachments externos) ===================
    juce::ComboBox& getModelComboBox() noexcept { return modelBox; }
    juce::TextButton& getModelButton(int index) { return modelButtons[(size_t) juce::jlimit(0, 2, index)]; }
    juce::TextButton& getGateEnableButton() noexcept { return gateEnableButton; }

    AmpKnob& getKnob(int index)
    {
        return knobs[(size_t) juce::jlimit(0, numMainKnobs - 1, index)];
    }

    static constexpr int numMainKnobs = 6;

    /** Nombres visibles de los knobs principales (Gain, Bass, Mid, ...). */
    static juce::StringArray getMainKnobNames();

    /** Prefijo de parametros de cada modelo. */
    static juce::String getModelPrefix(int modelIndex);

    //== Callbacks ============================================================
    std::function<void(int)> onModelChanged;

private:
    /** Filas calculadas del layout, compartidas entre paint() y resized(). */
    struct Layout
    {
        int knobRowHeight = 0;
        int separatorHeight = 0;
        int modelRowY = 0;
        int modelRowHeight = 0;
        int gateRowY = 0;
        int gateRowHeight = 0;
        int captionHeight = 0;
    };

    Layout computeLayout() const;

    void comboBoxChanged(juce::ComboBox* box) override;
    void buttonClicked(juce::Button* button) override;
    void timerCallback() override;

    void selectModel(int modelIndex, bool notify);
    void rebindKnobsToModel(int modelIndex);
    void updateModelButtons();
    void createKnob(AmpKnob& knob, const juce::String& name);
    void createCaption(juce::Label& label, const juce::String& text);

    juce::AudioProcessorValueTreeState& apvts;

    //== Knobs principales ====================================================
    std::array<AmpKnob, numMainKnobs> knobs;
    std::array<juce::Label, numMainKnobs> knobCaptions;

    //== Selector de modelo ===================================================
    std::array<juce::TextButton, 3> modelButtons;
    juce::ComboBox modelBox;              // binding con el parametro ampModel
    juce::Label modelCaption;
    int currentModel = 0;
    float gateLedPulse = 0.0f;
    float gateInputLevel = 0.0f;
    bool gateOpen = false;

    //== Noise gate ===========================================================
    juce::TextButton gateEnableButton;
    AmpKnob gateThreshold;
    AmpKnob gateRatio;
    juce::Label gateCaption;
    juce::Label gateThresholdCaption;
    juce::Label gateRatioCaption;
    bool gateActive = true;
    bool gateSwitchState = true;

    //== Salida ===============================================================
    AmpKnob outputLevel;
    juce::Label outputCaption;
    juce::Label outputKnobCaption;

    //== Attachments ==========================================================
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               numMainKnobs> knobAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateThresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gateRatioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputLevelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modelAttachment;

    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanel)
};
