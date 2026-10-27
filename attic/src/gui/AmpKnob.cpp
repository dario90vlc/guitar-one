#include "AmpKnob.h"

//==============================================================================
// Fuentes del sistema (compatibles con JUCE 6.1, 7 y 8)
//==============================================================================
namespace
{
    const juce::StringArray& serifCandidates()
    {
        static const juce::StringArray names { "Georgia", "Bookman Old Style",
                                               "Palatino Linotype", "Times New Roman",
                                               "Cambria", "Constantia", "Serif" };
        return names;
    }

    const juce::StringArray& sansCandidates()
    {
        static const juce::StringArray names { "Segoe UI Semibold", "Segoe UI",
                                               "Tahoma", "Verdana", "Arial",
                                               "Helvetica Neue", "Sans-Serif" };
        return names;
    }

    const juce::String& findFirstAvailable(const juce::StringArray& candidates, bool serif)
    {
        static juce::String cachedSerif;
        static juce::String cachedSans;

        auto& cache = serif ? cachedSerif : cachedSans;

        if (cache.isEmpty())
        {
            const auto installed = juce::Font::findAllTypefaceNames();

            for (const auto& name : candidates)
                if (installed.contains(name))
                {
                    cache = name;
                    break;
                }

            if (cache.isEmpty())
                cache = serif ? juce::Font::getDefaultSerifFontName()
                              : juce::Font::getDefaultSansSerifFontName();
        }

        return cache;
    }

    juce::Font makeFont(const juce::String& typefaceName, float height, bool bold)
    {
        const auto h = juce::jmax(6.0f, height);

#if JUCE_MAJOR_VERSION >= 8
        juce::FontOptions options { typefaceName, h,
                                    bold ? juce::Font::bold : juce::Font::plain };
        return juce::Font { options };
#else
        juce::Font font { typefaceName, h, bold ? juce::Font::bold : juce::Font::plain };
        return font;
#endif
    }

    /** Gradiente radial con parada clara desplazada: simula metal pulido. */
    juce::ColourGradient metalGradient(juce::Point<float> centre, float radius,
                                       juce::Colour light, juce::Colour dark,
                                       juce::Point<float> highlightOffset)
    {
        return juce::ColourGradient(light, centre + highlightOffset,
                                    dark, centre.translated(radius * 1.25f, radius * 1.25f),
                                    true);
    }
}

//==============================================================================
juce::Font AmpFonts::vintageSerif(float height, bool bold)
{
    return makeFont(findFirstAvailable(serifCandidates(), true), height, bold);
}

juce::Font AmpFonts::panelSans(float height, bool bold)
{
    return makeFont(findFirstAvailable(sansCandidates(), false), height, bold);
}

juce::String AmpFonts::vintageSerifName()
{
    return findFirstAvailable(serifCandidates(), true);
}

//==============================================================================
AmpKnobColours AmpKnobColours::vintageGold()
{
    return { juce::Colour(0xff6d6357),   // faceTop
             juce::Colour(0xff1c1712),   // faceBottom
             juce::Colour(0xffb99245),   // rim
             juce::Colour(0xffe8c877),   // tick
             juce::Colour(0xffffe6a3),   // pointer
             juce::Colour(0xffffb347),   // led
             juce::Colour(0xffe0aa4a) }; // arc
}

AmpKnobColours AmpKnobColours::blackfaceSilver()
{
    return { juce::Colour(0xffd8d8d8),
             juce::Colour(0xff2b2b2d),
             juce::Colour(0xff8d8d90),
             juce::Colour(0xffdcdcdc),
             juce::Colour(0xff111111),
             juce::Colour(0xffffc14d),
             juce::Colour(0xffc9c9cc) };
}

AmpKnobColours AmpKnobColours::hotRodOrange()
{
    return { juce::Colour(0xff8a8079),
             juce::Colour(0xff141210),
             juce::Colour(0xffd97a24),
             juce::Colour(0xffffcf8a),
             juce::Colour(0xfffff0d2),
             juce::Colour(0xffff5a1f),
             juce::Colour(0xffff8a2b) };
}

AmpKnobColours AmpKnobColours::ivoryTolex()
{
    return { juce::Colour(0xfff3ead6),
             juce::Colour(0xffb9ac91),
             juce::Colour(0xff6b5a3e),
             juce::Colour(0xff4a3d29),
             juce::Colour(0xff2b2318),
             juce::Colour(0xffd8b25a),
             juce::Colour(0xff9c7c3c) };
}

//==============================================================================
AmpLookAndFeel::AmpLookAndFeel()
    : knobColours(AmpKnobColours::vintageGold())
{
    setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff151110));
    setColour(juce::DocumentWindow::textColourId,        juce::Colour(0xffe8c877));

    setColour(juce::Label::textColourId,                 juce::Colour(0xffe8c877));
    setColour(juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId,              juce::Colours::transparentBlack);

    setColour(juce::Slider::textBoxTextColourId,         juce::Colour(0xffe8c877));
    setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId,    juce::Colour(0x55e0aa4a));
    setColour(juce::Slider::thumbColourId,               juce::Colour(0xffe0aa4a));

    setColour(juce::TextButton::buttonColourId,          juce::Colour(0xff2a231c));
    setColour(juce::TextButton::buttonOnColourId,        juce::Colour(0xff7a4a12));
    setColour(juce::TextButton::textColourOffId,         juce::Colour(0xffcdb894));
    setColour(juce::TextButton::textColourOnId,          juce::Colour(0xfffff0d2));

    setColour(juce::ComboBox::backgroundColourId,        juce::Colour(0xff211b15));
    setColour(juce::ComboBox::textColourId,              juce::Colour(0xffe8c877));
    setColour(juce::ComboBox::outlineColourId,           juce::Colour(0xffb99245));
    setColour(juce::ComboBox::arrowColourId,             juce::Colour(0xffe0aa4a));
    setColour(juce::ComboBox::buttonColourId,            juce::Colour(0xff2a231c));

    setColour(juce::PopupMenu::backgroundColourId,       juce::Colour(0xff211b15));
    setColour(juce::PopupMenu::textColourId,             juce::Colour(0xffe8c877));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff7a4a12));
    setColour(juce::PopupMenu::highlightedTextColourId,  juce::Colour(0xfffff0d2));

    setColour(juce::TooltipWindow::backgroundColourId,   juce::Colour(0xf01b1512));
    setColour(juce::TooltipWindow::textColourId,         juce::Colour(0xffe8c877));
    setColour(juce::TooltipWindow::outlineColourId,      juce::Colour(0xffb99245));

    setColour(juce::ToggleButton::textColourId,          juce::Colour(0xffe8c877));
    setColour(juce::ToggleButton::tickColourId,          juce::Colour(0xffe0aa4a));
    setColour(juce::ToggleButton::tickDisabledColourId,  juce::Colour(0xff5a4c38));
}

void AmpLookAndFeel::setKnobColours(const AmpKnobColours& newColours)
{
    knobColours = newColours;
}

void AmpLookAndFeel::setUiScale(float newScale)
{
    uiScale = juce::jlimit(0.6f, 2.0f, newScale);
}

//==============================================================================
void AmpLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                      int x, int y, int width, int height,
                                      float sliderPos,
                                      float rotaryStartAngle,
                                      float rotaryEndAngle,
                                      juce::Slider& slider)
{
    const auto& colours = knobColours;

    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();

    // Espacio para la escala de puntos alrededor del knob.
    const auto tickOuter = radius;
    const auto capRadius  = radius * 0.80f;
    const auto rimRadius  = radius * 0.885f;
    const auto tickInner  = radius * 0.90f;

    const auto angle = rotaryStartAngle + juce::jlimit(0.0f, 1.0f, sliderPos)
                                          * (rotaryEndAngle - rotaryStartAngle);

    //--------------------------------------------------------------------------
    // 1. Escala de puntos (se ilumina por debajo del valor actual)
    {
        constexpr int numTicks = 11;
        const auto clampedForTicks = juce::jlimit(0.0f, 1.0f, sliderPos);

        for (int i = 0; i < numTicks; ++i)
        {
            const auto t = (float) i / (float) (numTicks - 1);
            const auto tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const auto unit = juce::Point<float>(std::sin(tickAngle), -std::cos(tickAngle));

            const bool passed = t <= clampedForTicks + 0.001f;

            g.setColour(passed ? colours.tick.withAlpha(0.95f)
                               : colours.tick.withAlpha(0.30f));

            const auto p1 = centre + unit * tickInner;
            const auto p2 = centre + unit * tickOuter;
            const auto thickness = juce::jmax(1.0f, radius * (i % 5 == 0 ? 0.085f : 0.05f));

            g.drawLine({ p1, p2 }, thickness);
        }
    }

    //--------------------------------------------------------------------------
    // 2. Arco de valor (pista + progreso con brillo)
    const auto arcThickness = juce::jmax(2.0f, radius * 0.10f);
    const auto arcRadius    = radius * 0.955f;

    {
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(juce::Colour(0x99000000));
        g.strokePath(track, juce::PathStrokeType(arcThickness + 1.5f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(colours.arc.withAlpha(0.22f));
        g.strokePath(track, juce::PathStrokeType(arcThickness,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const auto clampedPos = juce::jlimit(0.0f, 1.0f, sliderPos);

    if (clampedPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle,
                            rotaryStartAngle + clampedPos * (rotaryEndAngle - rotaryStartAngle),
                            true);

        const bool lit = slider.isMouseOverOrDragging() || slider.isMouseButtonDown();

        if (lit)
        {
            g.setColour(colours.arc.withAlpha(0.28f));
            g.strokePath(value, juce::PathStrokeType(arcThickness * 2.4f,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        g.setColour(colours.arc.withAlpha(lit ? 1.0f : 0.80f));
        g.strokePath(value, juce::PathStrokeType(arcThickness,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    //--------------------------------------------------------------------------
    // 3. Sombra proyectada del knob
    {
        juce::ColourGradient shadow(juce::Colour(0x88000000),
                                    centre.translated(radius * 0.10f, radius * 0.16f),
                                    juce::Colours::transparentBlack,
                                    centre.translated(radius * 1.15f, radius * 1.30f),
                                    true);
        g.setGradientFill(shadow);
        g.fillEllipse(juce::Rectangle<float>(rimRadius * 2.15f, rimRadius * 2.15f).withCentre(centre));
    }

    //--------------------------------------------------------------------------
    // 4. Aro exterior metalico
    {
        const auto rimBounds = juce::Rectangle<float>(rimRadius * 2.0f, rimRadius * 2.0f)
                                   .withCentre(centre);

        juce::ColourGradient rimGrad(colours.rim.brighter(0.55f),
                                     centre.translated(-rimRadius * 0.6f, -rimRadius * 0.7f),
                                     colours.rim.darker(0.85f),
                                     centre.translated(rimRadius * 0.7f, rimRadius * 0.8f),
                                     false);
        g.setGradientFill(rimGrad);
        g.fillEllipse(rimBounds);

        g.setColour(juce::Colour(0x66000000));
        g.drawEllipse(rimBounds.reduced(0.5f), 1.2f);
    }

    //--------------------------------------------------------------------------
    // 5. Tapa del knob (metal con reflejo segun la posicion del raton)
    {
        const auto capBounds = juce::Rectangle<float>(capRadius * 2.0f, capRadius * 2.0f)
                                   .withCentre(centre);

        auto highlight = juce::Point<float>(-capRadius * 0.35f, -capRadius * 0.45f);

        // Reflejo que sigue al raton (se calcula en coordenadas de pantalla).
        if (slider.getPeer() != nullptr)
        {
            const auto local = slider.getLocalPoint(nullptr,
                                                    juce::Desktop::getMousePosition()).toFloat();
            const auto delta = (local - centre) / juce::jmax(1.0f, capRadius);
            highlight = { juce::jlimit(-1.0f, 1.0f, delta.x) * capRadius * 0.45f,
                          juce::jlimit(-1.0f, 1.0f, delta.y) * capRadius * 0.45f };
        }

        g.setGradientFill(metalGradient(centre, capRadius,
                                        colours.faceTop, colours.faceBottom, highlight));
        g.fillEllipse(capBounds);

        // Anillos concentricos: mecanizado del metal.
        g.setColour(colours.faceTop.withAlpha(0.10f));

        for (int i = 1; i <= 3; ++i)
            g.drawEllipse(capBounds.reduced(capRadius * 0.16f * (float) i), 1.0f);

        // Brillo superior tipo "cromo".
        {
            juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.20f),
                                       centre.translated(0.0f, -capRadius * 0.55f),
                                       juce::Colours::transparentWhite,
                                       centre.translated(0.0f, capRadius * 0.25f),
                                       false);
            g.setGradientFill(sheen);
            g.fillEllipse(capBounds.reduced(capRadius * 0.08f));
        }

        // Borde interior.
        g.setColour(juce::Colour(0x99000000));
        g.drawEllipse(capBounds.reduced(0.5f), 1.4f);
    }

    //--------------------------------------------------------------------------
    // 6. Indicador (aguja) + LED
    {
        const auto unit = juce::Point<float>(std::sin(angle), -std::cos(angle));
        const auto inner = centre + unit * (capRadius * 0.22f);
        const auto outer = centre + unit * (capRadius * 0.84f);

        const bool lit = slider.isMouseOverOrDragging() || slider.isMouseButtonDown();

        if (lit)
        {
            g.setColour(colours.led.withAlpha(0.35f));
            g.drawLine({ inner, outer }, juce::jmax(3.5f, radius * 0.17f));
        }

        g.setColour(colours.pointer);
        g.drawLine({ inner, outer }, juce::jmax(1.8f, radius * 0.085f));

        // LED en la punta del indicador: intensidad proporcional al valor.
        const auto ledRadius = juce::jmax(1.6f, radius * 0.075f);
        const auto ledCentre = centre + unit * (capRadius * 0.62f);
        const auto ledColour = colours.led.withMultipliedBrightness(0.75f + 0.5f * clampedPos);

        g.setGradientFill(juce::ColourGradient(ledColour.brighter(0.8f), ledCentre,
                                               ledColour.darker(0.6f),
                                               ledCentre.translated(ledRadius, ledRadius), true));
        g.fillEllipse(juce::Rectangle<float>(ledRadius * 2.0f, ledRadius * 2.0f).withCentre(ledCentre));
    }

    juce::ignoreUnused(uiScale);
}

//==============================================================================
void AmpLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                          juce::Button& button,
                                          const juce::Colour&,
                                          bool shouldDrawButtonAsHighlighted,
                                          bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto corner = juce::jmin(bounds.getHeight() * 0.35f, 8.0f);
    const bool on = button.getToggleState();

    // Cuerpo del switch.
    {
        juce::ColourGradient body(on ? juce::Colour(0xff4a3418) : juce::Colour(0xff2b231b),
                                  bounds.getCentreX(), bounds.getY(),
                                  on ? juce::Colour(0xff1d1408) : juce::Colour(0xff120e0a),
                                  bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(body);
        g.fillRoundedRectangle(bounds, corner);
    }

    g.setColour(on ? juce::Colour(0xffb99245) : juce::Colour(0xff5a4c38));
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, on ? 1.6f : 1.0f);

    if (shouldDrawButtonAsDown)
    {
        g.setColour(juce::Colour(0x33000000));
        g.fillRoundedRectangle(bounds, corner);
    }
    else if (shouldDrawButtonAsHighlighted)
    {
        g.setColour(juce::Colour(0x18ffffff));
        g.fillRoundedRectangle(bounds, corner);
    }

    // LED superior: rojo cuando esta apagado, ambar brillante cuando esta activo.
    const auto ledRadius = juce::jmin(bounds.getHeight() * 0.22f, 7.0f);
    const auto ledCentre = juce::Point<float>(bounds.getCentreX(), bounds.getY() + ledRadius * 1.7f);

    if (on)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0x88ffb347), ledCentre,
                                               juce::Colours::transparentBlack,
                                               ledCentre.translated(0.0f, ledRadius * 4.0f), true));
        g.fillEllipse(juce::Rectangle<float>(ledRadius * 8.0f, ledRadius * 8.0f).withCentre(ledCentre));
    }

    const auto ledColour = on ? juce::Colour(0xffffd27a) : juce::Colour(0xff3a2a22);
    g.setGradientFill(juce::ColourGradient(ledColour.brighter(on ? 0.9f : 0.1f),
                                           ledCentre.translated(-ledRadius * 0.3f, -ledRadius * 0.3f),
                                           ledColour.darker(on ? 0.35f : 0.2f),
                                           ledCentre.translated(ledRadius, ledRadius), true));
    g.fillEllipse(juce::Rectangle<float>(ledRadius * 2.0f, ledRadius * 2.0f).withCentre(ledCentre));
    g.setColour(juce::Colour(0x66000000));
    g.drawEllipse(juce::Rectangle<float>(ledRadius * 2.0f, ledRadius * 2.0f).withCentre(ledCentre), 0.8f);
}

void AmpLookAndFeel::drawButtonText(juce::Graphics& g,
                                    juce::TextButton& button,
                                    bool,
                                    bool shouldDrawButtonAsDown)
{
    const auto font = AmpFonts::panelSans(juce::jlimit(10.0f, 16.0f,
                                                       (float) button.getHeight() * 0.34f), true);
    g.setFont(font);
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                          : juce::TextButton::textColourOffId)
                      .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));

    // El texto se coloca bajo el LED.
    auto textArea = button.getLocalBounds();
    textArea.removeFromTop(juce::roundToInt((float) button.getHeight() * 0.42f));
    textArea.translate(0, shouldDrawButtonAsDown ? 1 : 0);

    g.drawFittedText(button.getButtonText().toUpperCase(), textArea,
                     juce::Justification::centred(), 1, 0.85f);
}

//==============================================================================
void AmpLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.fillAll(label.findColour(juce::Label::backgroundColourId));

    if (! label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        g.setColour(label.findColour(juce::Label::textColourId).withMultipliedAlpha(alpha));
        g.setFont(getLabelFont(label));

        auto area = label.getBorderSize().subtractedFrom(label.getLocalBounds());
        g.drawFittedText(label.getText(), area, label.getJustificationType(),
                         juce::jmax(1, (int) ((float) area.getHeight() / 12.0f)), 0.9f);
    }

    g.setColour(label.findColour(juce::Label::outlineColourId));
    g.drawRect(label.getLocalBounds());
}

juce::Font AmpLookAndFeel::getLabelFont(juce::Label& label)
{
    const auto height = label.getHeight() > 0 ? (float) label.getHeight() * 0.72f : 14.0f;
    return AmpFonts::vintageSerif(juce::jlimit(9.0f, 20.0f, height), true);
}

//==============================================================================
void AmpLookAndFeel::drawComboBox(juce::Graphics& g,
                                  int width, int height,
                                  bool isButtonDown,
                                  int, int, int, int,
                                  juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    const auto corner = juce::jmin(bounds.getHeight() * 0.25f, 6.0f);

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c241c), bounds.getCentreX(), bounds.getY(),
                                           juce::Colour(0xff14100c), bounds.getCentreX(), bounds.getBottom(),
                                           false));
    g.fillRoundedRectangle(bounds, corner);

    g.setColour(box.hasKeyboardFocus(false) || isButtonDown ? juce::Colour(0xffe0aa4a)
                                                            : juce::Colour(0xff8a6a34));
    g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.2f);

    // Flecha estilo "pointer" de ampli.
    const auto arrowArea = juce::Rectangle<float>((float) width - (float) height, 0.0f,
                                                  (float) height, (float) height);
    const auto c = arrowArea.getCentre();
    const auto s = (float) height * 0.16f;

    juce::Path arrow;
    arrow.startNewSubPath(c.x - s, c.y - s * 0.55f);
    arrow.lineTo(c.x, c.y + s * 0.65f);
    arrow.lineTo(c.x + s, c.y - s * 0.55f);

    g.setColour(juce::Colour(0xffe0aa4a));
    g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

juce::Font AmpLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return AmpFonts::panelSans(juce::jlimit(10.0f, 18.0f, (float) box.getHeight() * 0.48f), true);
}

juce::Font AmpLookAndFeel::getPopupMenuFont()
{
    return AmpFonts::panelSans(15.0f, false);
}

//==============================================================================
void AmpLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xf5221a14), bounds.getCentreX(), bounds.getY(),
                                           juce::Colour(0xf50d0a07), bounds.getCentreX(), bounds.getBottom(),
                                           false));
    g.fillRoundedRectangle(bounds, 5.0f);

    g.setColour(juce::Colour(0xffb99245));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, 1.2f);

    g.setColour(juce::Colour(0xffe8c877));
    g.setFont(AmpFonts::panelSans(13.5f, false));
    g.drawFittedText(text, bounds.toNearestInt().reduced(7, 4), juce::Justification::centredLeft(), 2);
}

juce::Rectangle<int> AmpLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                      juce::Point<int> screenPos,
                                                      juce::Rectangle<int> parentArea)
{
    const auto font = AmpFonts::panelSans(13.5f, false);
    const auto textWidth = (int) std::ceil(font.getStringWidthFloat(tipText));

    const auto w = juce::jlimit(90, 360, textWidth + 26);
    const auto h = 30;

    auto x = screenPos.x > parentArea.getCentreX() ? screenPos.x - w - 14 : screenPos.x + 18;
    auto y = screenPos.y > parentArea.getCentreY() ? screenPos.y - h - 12 : screenPos.y + 18;

    return juce::Rectangle<int>(x, y, w, h)
             .constrainedWithin(parentArea)
             .withY(juce::jlimit(parentArea.getY(), juce::jmax(parentArea.getY(),
                                                               parentArea.getBottom() - h), y));
}

void AmpLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff241c15), bounds.getCentreX(), bounds.getY(),
                                           juce::Colour(0xff0f0b08), bounds.getCentreX(), bounds.getBottom(),
                                           false));
    g.fillRect(bounds);

    g.setColour(juce::Colour(0xffb99245));
    g.drawRect(bounds, 1.4f);
}

//==============================================================================
AmpKnob::AmpKnob()
{
    initialise();
}

AmpKnob::AmpKnob(const juce::String& componentName)
    : juce::Slider(componentName)
{
    initialise();
}

void AmpKnob::initialise()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,      // -135 grados
                        juce::MathConstants<float>::pi * 2.75f,      // +135 grados
                        true);
    setVelocityBasedMode(false);
    setMouseDragSensitivity(180);
    setDoubleClickReturnValue(true, 0.0);
    setPopupDisplayEnabled(true, true, nullptr);
    setWantsKeyboardFocus(true);
    setScrollWheelEnabled(true);
    setRepaintsOnMouseActivity(true);
    setValue(0.0, juce::dontSendNotification);
    updateTooltip();
}

void AmpKnob::setup(double minimum, double maximum, double interval, double defaultValue,
                    const juce::String& suffix)
{
    setValueSuffix(suffix);
    setRange(minimum, maximum, interval);
    setDoubleClickReturnValue(true, defaultValue);
    setValue(defaultValue, juce::dontSendNotification);
    updateTooltip();
}

void AmpKnob::setLabelText(const juce::String& newLabel)
{
    labelText = newLabel;
    updateTooltip();
    repaint();
}

void AmpKnob::setValueSuffix(const juce::String& newSuffix)
{
    valueSuffix = newSuffix;
    updateTooltip();
    repaint();
}

void AmpKnob::setKnobColours(const AmpKnobColours& newColours)
{
    customColours = std::make_unique<AmpKnobColours>(newColours);
    repaint();
}

void AmpKnob::clearKnobColours()
{
    customColours.reset();
    repaint();
}

const AmpKnobColours& AmpKnob::resolveColours() const
{
    if (customColours != nullptr)
        return *customColours;

    if (auto* ampLnf = dynamic_cast<AmpLookAndFeel*>(&getLookAndFeel()))
        return ampLnf->getKnobColours();

    static const AmpKnobColours fallback = AmpKnobColours::vintageGold();
    return fallback;
}

void AmpKnob::setUiScale(float newScale)
{
    uiScale = juce::jlimit(0.6f, 2.0f, newScale);
    repaint();
}

void AmpKnob::setShowNameLabel(bool shouldShow)
{
    showNameLabel = shouldShow;
    repaint();
}

int AmpKnob::getNameLabelHeight() const
{
    if (! showNameLabel)
        return 0;

    return juce::roundToInt(juce::jlimit(11.0f, 22.0f, 13.0f * uiScale));
}

void AmpKnob::setLedColour(juce::Colour newColour)
{
    hasLedOverride = true;
    ledOverride = newColour;
    repaint();
}

void AmpKnob::refreshLed()
{
    repaint();
}

juce::String AmpKnob::getTextFromValue(double value)
{
    switch (valueFormat)
    {
        case ValueFormat::decibels:
        case ValueFormat::ratio:
            return juce::String(value, 1) + valueSuffix;

        case ValueFormat::raw:
            return juce::String(juce::roundToInt(value)) + valueSuffix;

        case ValueFormat::percent:
        default:
            return juce::String(juce::roundToInt(value * 100.0)) + valueSuffix;
    }
}

void AmpKnob::setValueFormat(ValueFormat newFormat)
{
    valueFormat = newFormat;
    updateTooltip();
    repaint();
}

void AmpKnob::updateTooltip()
{
    const auto name = labelText.isNotEmpty() ? labelText : getName();

    if (name.isEmpty())
    {
        setTooltip({});
        return;
    }

    setTooltip(name + " : " + getTextFromValue(getValue()));
}

void AmpKnob::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();

    if (showNameLabel)
    {
        auto nameArea = area.removeFromTop(getNameLabelHeight());

        const auto font = AmpFonts::panelSans(juce::jlimit(10.0f, 18.0f, 12.5f * uiScale), true);
        g.setFont(font);

        const auto label = labelText.isNotEmpty() ? labelText : getName();
        const auto textArea = nameArea.toFloat();

        g.setColour(juce::Colour(0xaa000000));
        g.drawFittedText(label.toUpperCase(), textArea.translated(0.0f, 1.0f).toNearestInt(),
                         juce::Justification::centred(), 1, 0.8f);

        g.setColour(juce::Colour(0xffe8c877));
        g.drawFittedText(label.toUpperCase(), textArea.toNearestInt(),
                         juce::Justification::centred(), 1, 0.8f);
    }

    // El dibujo real del knob lo hace el LookAndFeel (AmpLookAndFeel resuelve la
    // skin activa; un LookAndFeel generico usara su propio estilo rotativo).
    auto& lnf = getLookAndFeel();
    const auto pos = (float) valueToProportionOfLength(getValue());

    lnf.drawRotarySlider(g, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                         pos, getRotaryStartAngle(), getRotaryEndAngle(), *this);

    if (hasLedOverride)
    {
        // LED de estado opcional en la esquina superior derecha.
        const auto ledRadius = juce::jmax(2.0f, (float) area.getWidth() * 0.045f);
        const auto centre = juce::Point<float>((float) area.getRight() - ledRadius * 2.2f,
                                               (float) area.getY() + ledRadius * 2.2f);

        g.setGradientFill(juce::ColourGradient(ledOverride.brighter(0.7f), centre,
                                               ledOverride.darker(0.5f),
                                               centre.translated(ledRadius, ledRadius), true));
        g.fillEllipse(juce::Rectangle<float>(ledRadius * 2.0f, ledRadius * 2.0f).withCentre(centre));
    }

    updateTooltip();
}

void AmpKnob::mouseEnter(const juce::MouseEvent& e)
{
    juce::Slider::mouseEnter(e);
    mouseOver = true;

    if (onActivityChanged != nullptr)
        onActivityChanged(true);

    repaint();
}

void AmpKnob::mouseExit(const juce::MouseEvent& e)
{
    juce::Slider::mouseExit(e);
    mouseOver = false;

    if (onActivityChanged != nullptr)
        onActivityChanged(false);

    repaint();
}
