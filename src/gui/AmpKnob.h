#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Paleta de colores de un amplificador. Cada "skin" describe como se pinta un
    knob: el metal de la tapa, el aro, la escala de puntos y el color del
    indicador / LED.
*/
struct AmpKnobColours
{
    juce::Colour faceTop;      // metal claro (luz)
    juce::Colour faceBottom;   // metal oscuro (sombra)
    juce::Colour rim;          // aro exterior
    juce::Colour tick;         // escala de puntos
    juce::Colour pointer;      // linea indicadora
    juce::Colour led;          // LED del indicador
    juce::Colour arc;          // arco de valor

    /** Estilo "ampli vintage": dorado envejecido sobre baquelita negra. */
    static AmpKnobColours vintageGold();

    /** Estilo "blackface": plateado sobre panel negro, con indicador dorado. */
    static AmpKnobColours blackfaceSilver();

    /** Estilo "hot rod": cromo oscuro con LED naranja. */
    static AmpKnobColours hotRodOrange();

    /** Estilo "tolex": baquelita crema con indicador negro (knobs de marfil). */
    static AmpKnobColours ivoryTolex();
};

//==============================================================================
/**
    LookAndFeel global del plugin. Pinta knobs metalicos estilo amplificador,
    switches tipo toggle con LED, etiquetas vintage y combo boxes oscuros.

    Todos los colores salen de los colores de la paleta JUCE, de modo que
    overrideColour() sigue funcionando para retoques puntuales.
*/
class AmpLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AmpLookAndFeel();

    /** Skin usada para los knobs rotativos. */
    void setKnobColours(const AmpKnobColours& newColours);
    const AmpKnobColours& getKnobColours() const noexcept { return knobColours; }

    /** Escala global (1.0 = tamano de diseno). Afecta fuentes y grosores. */
    void setUiScale(float newScale);
    float getUiScale() const noexcept { return uiScale; }

    //== Knobs =================================================================
    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;

    //== Botones / switches ====================================================
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

    //== Etiquetas =============================================================
    void drawLabel(juce::Graphics& g, juce::Label& label) override;
    juce::Font getLabelFont(juce::Label& label) override;

    //== ComboBox ==============================================================
    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;

    //== Tooltips ==============================================================
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                          juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;

    //== PopupMenu =============================================================
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

private:
    AmpKnobColours knobColours;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpLookAndFeel)
};

//==============================================================================
/** Fuentes del sistema con estetica vintage (con fallback seguro). */
namespace AmpFonts
{
    /** Fuente con serifas para titulos: Georgia / Bookman / Times. */
    juce::Font vintageSerif(float height, bool bold = true);

    /** Fuente sans condensada para etiquetas de panel. */
    juce::Font panelSans(float height, bool bold = true);

    /** Nombre legible de la fuente de titulos (para depuracion). */
    juce::String vintageSerifName();
}

//==============================================================================
/**
    Knob personalizado estilo amplificador de guitarra.

    - Hereda de juce::Slider (estilo rotativo, sin textbox, con popup de valor).
    - Pinta metal, aro, escala de puntos, arco de valor e indicador con LED.
    - El LED se ilumina al pasar el raton o al arrastrar.
    - Muestra un tooltip con el nombre del parametro y el valor actual.

    Uso tipico:
    @code
        AmpKnob gain;
        gain.setLabelText("GAIN");
        gain.setup(0.0, 1.0, 0.001, 0.5);
        addAndMakeVisible(gain);
    @endcode
*/
class AmpKnob : public juce::Slider
{
public:
    AmpKnob();
    explicit AmpKnob(const juce::String& componentName);
    ~AmpKnob() override = default;

    /** Nombre mostrado en el tooltip y en la etiqueta del panel. */
    void setLabelText(const juce::String& newLabel);
    const juce::String& getLabelText() const noexcept { return labelText; }

    /** Texto que se anade al valor en el tooltip / popup (ej. " %"). */
    void setValueSuffix(const juce::String& newSuffix);
    const juce::String& getValueSuffix() const noexcept { return valueSuffix; }

    /** Formato del valor mostrado en tooltip y popup. */
    enum class ValueFormat
    {
        percent,    ///< 0..1 se muestra como 0..100 %
        decibels,   ///< valor real con un decimal (ej. -60.0 dB)
        ratio,      ///< valor real con un decimal + sufijo (ej. 4.0:1)
        raw         ///< valor real sin decimales
    };

    void setValueFormat(ValueFormat newFormat);
    ValueFormat getValueFormat() const noexcept { return valueFormat; }

    /** Skin concreta para este knob (por defecto, la del LookAndFeel global). */
    void setKnobColours(const AmpKnobColours& newColours);
    void clearKnobColours();

    /** Configura rango y valor por defecto en una sola llamada. */
    void setup(double minimum, double maximum, double interval, double defaultValue,
               const juce::String& suffix = {});

    /** Ajusta la altura del indicador de valor y las fuentes. */
    void setUiScale(float newScale);
    float getUiScale() const noexcept { return uiScale; }

    /** Dibuja el nombre del parametro encima del knob. */
    void setShowNameLabel(bool shouldShow);
    bool isShowingNameLabel() const noexcept { return showNameLabel; }

    /** Altura reservada para el nombre (0 si esta desactivado). */
    int getNameLabelHeight() const;

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    /** Color del LED (por defecto el de la skin activa). */
    void setLedColour(juce::Colour newColour);

    /** Fuerza el repintado del LED (util si el valor cambia por automation). */
    void refreshLed();

    //== Callbacks opcionales para el host / panel =============================
    std::function<void()> onValueChanged;
    std::function<void(bool)> onActivityChanged;

private:
    void initialise();
    void updateTooltip();
    const AmpKnobColours& resolveColours() const;
    juce::String getTextFromValue(double value) override;

    juce::String labelText;
    juce::String valueSuffix = " %";
    ValueFormat valueFormat = ValueFormat::percent;
    std::unique_ptr<AmpKnobColours> customColours;
    bool showNameLabel = true;
    bool mouseOver = false;
    bool hasLedOverride = false;
    juce::Colour ledOverride;
    float uiScale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmpKnob)
};
