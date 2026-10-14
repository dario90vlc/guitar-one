#pragma once

#include <JuceHeader.h>
#include "gui/AmpKnob.h"
#include "gui/AmpPanel.h"
#include "gui/ControlPanel.h"

class GuitarAmpAudioProcessor;

//==============================================================================
/**
    Editor principal del plugin.

    Monta el mueble del amplificador (AmpPanel) y el panel de controles
    (ControlPanel), aplica el LookAndFeel global (AmpLookAndFeel) y conecta
    todos los controles con el AudioProcessorValueTreeState:

    - SliderAttachment  : Gain, Bass, Mid, Treble, Presence, Volume (segun el
                          modelo activo), Threshold, Ratio y Output Level.
    - ButtonAttachment  : los tres switches de modelo (Clean / Crunch /
                          Distortion) y el switch del noise gate.
    - ComboBoxAttachment: el parametro de eleccion del modelo de amplificador.
*/
class GuitarAmpAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit GuitarAmpAudioProcessorEditor(GuitarAmpAudioProcessor&);
    ~GuitarAmpAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutComponents();
    void updateUiScale();
    void setupModelButtonAttachments();
    void setupGateAttachment();

    GuitarAmpAudioProcessor& processorRef;

    //== LookAndFeel global ===================================================
    std::unique_ptr<AmpLookAndFeel> lookAndFeel;

    //== Componentes ==========================================================
    AmpPanel ampPanel;
    ControlPanel controlPanel;
    juce::TooltipWindow tooltipWindow { this, 600 };

    //== Attachments ==========================================================
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>, 3>
        modelButtonAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> gateEnableAttachment;

    //== Estado de la vista ===================================================
    float uiScale = 1.0f;
    float ledActivity = 1.0f;
    float ledActivityTarget = 1.0f;

    static constexpr int designWidth = 900;
    static constexpr int designHeight = 500;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarAmpAudioProcessorEditor)
};
