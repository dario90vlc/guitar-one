#pragma once

#include <JuceHeader.h>
#include "AmpKnob.h"

//==============================================================================
/**
    Panel principal del amplificador.

    Se encarga del "mueble" del ampli: textura de tweed / blackface / tolex,
    chapa con el nombre, LED de encendido palpitante, chasis metalico y la
    rejilla (grille cloth) sobre la que se monta el panel de controles.

    El panel de controles se coloca como hijo (o como componente superpuesto)
    dentro de getControlArea().
*/
class AmpPanel : public juce::Component,
                 private juce::Timer
{
public:
    /** Textura del mueble. */
    enum class TextureStyle
    {
        tweed,        ///< tweed marron/dorado estilo anos 50
        blackface,    ///< panel negro con rejilla plateada
        blackTolex,   ///< tolex negro con rejilla dorada
        sunburstWood  ///< madera ambar envejecida
    };

    AmpPanel();
    ~AmpPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Cambia la textura del mueble (regenera la imagen de fondo). */
    void setTextureStyle(TextureStyle newStyle);
    TextureStyle getTextureStyle() const noexcept { return textureStyle; }

    /** Nombre grande de la chapa (ej. "GUITAR AMP"). */
    void setAmpName(const juce::String& newName);
    const juce::String& getAmpName() const noexcept { return ampName; }

    /** Subtitulo pequeno bajo el nombre (ej. "TUBE AMPLIFIER - VST3"). */
    void setAmpSubtitle(const juce::String& newSubtitle);
    const juce::String& getAmpSubtitle() const noexcept { return ampSubtitle; }

    /** LED de encendido (tambien usado para indicar actividad de senal). */
    void setPowerOn(bool shouldBeOn);
    bool isPowerOn() const noexcept { return powerOn; }

    /** Brillo instantaneo del LED (0..1) para feedback de senal. */
    void setLedIntensity(float newIntensity);

    /** Zona interior donde va el panel de controles. */
    juce::Rectangle<int> getControlArea() const;
    juce::Rectangle<int> getHeaderArea() const;
    juce::Rectangle<int> getPowerLedBounds() const;

    /** Escala de la interfaz (1.0 = 900x500). */
    void setUiScale(float newScale);
    float getUiScale() const noexcept { return uiScale; }

    //== Callbacks para el editor =============================================
    std::function<void()> onPowerLedClicked;

    void mouseDown(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;
    void regenerateTexture();
    void drawHeader(juce::Graphics& g, juce::Rectangle<int> header);
    void drawChassis(juce::Graphics& g, juce::Rectangle<int> area);
    void drawPowerLed(juce::Graphics& g);

    TextureStyle textureStyle = TextureStyle::tweed;
    juce::String ampName { "GUITAR AMP" };
    juce::String ampSubtitle { "ALL-TUBE GUITAR AMPLIFIER" };

    juce::Image texture;          // textura cacheada del mueble
    juce::Image grilleCloth;      // rejilla cacheada
    juce::Rectangle<int> lastControlArea;

    bool powerOn = true;
    float ledIntensity = 1.0f;
    float pulsePhase = 0.0f;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpPanel)
};
