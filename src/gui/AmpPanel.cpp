#include "AmpPanel.h"

//==============================================================================
namespace
{
    /** Ruido pseudoaleatorio reproducible (sin depender de rand()). */
    inline float hashNoise(int x, int y, int seed)
    {
        juce::uint32 h = (juce::uint32) (x * 374761393) + (juce::uint32) (y * 668265263)
                       + (juce::uint32) (seed * 2246822519u);
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= (h >> 16);
        return (float) (h & 0xffffu) / 65535.0f;
    }

    /** Oscurece las esquinas para dar volumen al mueble. */
    void applyVignette(juce::Graphics& g, juce::Rectangle<float> area, float strength)
    {
        const auto centre = area.getCentre();
        const auto radius = area.getWidth() * 0.62f;

        juce::ColourGradient vignette(juce::Colours::transparentBlack, centre,
                                      juce::Colour(0xff000000).withAlpha(strength),
                                      centre.translated(radius, radius * 0.85f), true);
        g.setGradientFill(vignette);
        g.fillRect(area);
    }

    /** Tornillos del chasis. */
    void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffcbbf9d),
                                               centre.translated(-radius * 0.4f, -radius * 0.4f),
                                               juce::Colour(0xff3a3128),
                                               centre.translated(radius * 0.6f, radius * 0.6f),
                                               true));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

        g.setColour(juce::Colour(0x99000000));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 0.9f);

        g.setColour(juce::Colour(0xaa2a231b));
        g.drawLine(centre.x - radius * 0.6f, centre.y - radius * 0.6f,
                   centre.x + radius * 0.6f, centre.y + radius * 0.6f, juce::jmax(1.0f, radius * 0.32f));
    }
}

//==============================================================================
AmpPanel::AmpPanel()
{
    setOpaque(false);
    setInterceptsMouseClicks(false, true);
    startTimerHz(24);
}

AmpPanel::~AmpPanel()
{
    stopTimer();
}

//==============================================================================
void AmpPanel::setTextureStyle(TextureStyle newStyle)
{
    if (textureStyle == newStyle)
        return;

    textureStyle = newStyle;
    regenerateTexture();
    repaint();
}

void AmpPanel::setAmpName(const juce::String& newName)
{
    ampName = newName;
    repaint(getHeaderArea());
}

void AmpPanel::setAmpSubtitle(const juce::String& newSubtitle)
{
    ampSubtitle = newSubtitle;
    repaint(getHeaderArea());
}

void AmpPanel::setPowerOn(bool shouldBeOn)
{
    if (powerOn == shouldBeOn)
        return;

    powerOn = shouldBeOn;
    repaint(getHeaderArea());
}

void AmpPanel::setLedIntensity(float newIntensity)
{
    const auto clamped = juce::jlimit(0.0f, 1.0f, newIntensity);

    if (std::abs(clamped - ledIntensity) < 0.01f)
        return;

    ledIntensity = clamped;
    repaint(getPowerLedBounds().expanded(14));
}

void AmpPanel::setUiScale(float newScale)
{
    uiScale = juce::jlimit(0.6f, 2.0f, newScale);
    repaint();
}

//==============================================================================
juce::Rectangle<int> AmpPanel::getHeaderArea() const
{
    const auto headerHeight = juce::roundToInt(juce::jlimit(64.0f, 130.0f, 96.0f * uiScale));
    return getLocalBounds().removeFromTop(headerHeight);
}

juce::Rectangle<int> AmpPanel::getPowerLedBounds() const
{
    auto header = getHeaderArea();
    const auto diameter = juce::roundToInt(juce::jlimit(14.0f, 34.0f, 22.0f * uiScale));

    return juce::Rectangle<int>(diameter, diameter)
             .withCentre({ header.getRight() - juce::roundToInt(52.0f * uiScale),
                           header.getCentreY() - juce::roundToInt(6.0f * uiScale) });
}

juce::Rectangle<int> AmpPanel::getControlArea() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(getHeaderArea().getHeight());

    const auto margin = juce::roundToInt(juce::jlimit(6.0f, 26.0f, 14.0f * uiScale));
    const auto bottomMargin = juce::roundToInt(juce::jlimit(6.0f, 26.0f, 16.0f * uiScale));

    bounds.removeFromBottom(bottomMargin);
    return bounds.reduced(margin, 0);
}

void AmpPanel::resized()
{
    const auto controlArea = getControlArea();

    if (controlArea != lastControlArea)
    {
        lastControlArea = controlArea;
        regenerateTexture();
    }
}

void AmpPanel::timerCallback()
{
    if (! powerOn)
        return;

    pulsePhase += 0.075f;

    if (pulsePhase > juce::MathConstants<float>::twoPi)
        pulsePhase -= juce::MathConstants<float>::twoPi;

    repaint(getPowerLedBounds().expanded(14));
}

void AmpPanel::mouseDown(const juce::MouseEvent& e)
{
    if (getPowerLedBounds().expanded(8).contains(e.getPosition()) && onPowerLedClicked != nullptr)
        onPowerLedClicked();
}

//==============================================================================
void AmpPanel::regenerateTexture()
{
    const auto w = juce::jmax(1, getWidth());
    const auto h = juce::jmax(1, getHeight());

    texture = juce::Image(juce::Image::ARGB, w, h, true);

    {
        juce::Graphics g(texture);
        const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h);

        //--------------------------------------------------------------- base
        switch (textureStyle)
        {
            case TextureStyle::tweed:
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb08a4e), 0.0f, 0.0f,
                                                       juce::Colour(0xff6a4a22), 0.0f, (float) h,
                                                       false));
                g.fillRect(area);
                break;

            case TextureStyle::blackface:
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2a2c), 0.0f, 0.0f,
                                                       juce::Colour(0xff0d0d0e), 0.0f, (float) h,
                                                       false));
                g.fillRect(area);
                break;

            case TextureStyle::blackTolex:
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xff232120), 0.0f, 0.0f,
                                                       juce::Colour(0xff0a0908), 0.0f, (float) h,
                                                       false));
                g.fillRect(area);
                break;

            case TextureStyle::sunburstWood:
                g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd89b46), (float) w * 0.5f,
                                                       (float) h * 0.35f,
                                                       juce::Colour(0xff4a2a10), (float) w * 0.5f,
                                                       (float) h * 1.25f, true));
                g.fillRect(area);
                break;
        }

        //------------------------------------------------------------ patrones
        switch (textureStyle)
        {
            case TextureStyle::tweed:
            {
                // Trama de tejido: fibras horizontales y verticales alternas.
                const int step = 3;

                for (int y = 0; y < h; y += step)
                {
                    for (int x = 0; x < w; x += step)
                    {
                        const bool horizontal = (((x / step) + (y / step)) % 2) == 0;
                        const auto n = hashNoise(x, y, 7);
                        const auto alpha = 0.10f + n * 0.22f;

                        g.setColour(horizontal ? juce::Colour(0xffe8c877).withAlpha(alpha * 0.55f)
                                               : juce::Colour(0xff2a1c0c).withAlpha(alpha));
                        g.fillRect(x, y, step, step);
                    }
                }

                // Vetas diagonales largas del tweed.
                g.setColour(juce::Colour(0x18fff0c0));

                for (int i = -h; i < w + h; i += 11)
                    g.drawLine((float) i, 0.0f, (float) (i + h), (float) h, 1.0f);

                break;
            }

            case TextureStyle::blackface:
            {
                // Grano fino de tolex + linea plateada horizontal.
                for (int y = 0; y < h; ++y)
                {
                    for (int x = 0; x < w; x += 2)
                    {
                        const auto n = hashNoise(x, y, 3);

                        if (n > 0.72f)
                        {
                            g.setColour(juce::Colour(0xffbfc4c9).withAlpha((n - 0.72f) * 0.55f));
                            g.fillRect(x, y, 2, 1);
                        }
                    }
                }

                g.setGradientFill(juce::ColourGradient(juce::Colour(0x66dfe4e8), 0.0f,
                                                       (float) getHeaderArea().getHeight(),
                                                       juce::Colours::transparentBlack, 0.0f,
                                                       (float) getHeaderArea().getHeight() + 26.0f,
                                                       false));
                g.fillRect(0, getHeaderArea().getHeight() - 6, w, 32);
                break;
            }

            case TextureStyle::blackTolex:
            {
                // Tolex: cuadricula de puntos con brillo.
                for (int y = 0; y < h; y += 4)
                {
                    for (int x = 0; x < w; x += 4)
                    {
                        const auto n = hashNoise(x, y, 11);
                        const auto alpha = n * 0.13f;

                        g.setColour(juce::Colour(0xff9c8f78).withAlpha(alpha));
                        g.fillRect(x, y, 2, 2);
                    }
                }

                g.setColour(juce::Colour(0x14ffffff));

                for (int i = -h; i < w; i += 64)
                    g.drawLine((float) i, 0.0f, (float) (i + h), (float) h, 1.2f);

                break;
            }

            case TextureStyle::sunburstWood:
            {
                // Vetas de madera: curvas senoidales.
                for (int i = 0; i < 90; ++i)
                {
                    const auto n = hashNoise(i, 5, 21);
                    const auto baseY = (float) i * (float) h / 90.0f;
                    const auto alpha = 0.05f + n * 0.14f;

                    g.setColour(juce::Colour(0xff3a1f0a).withAlpha(alpha));

                    juce::Path grain;
                    grain.startNewSubPath(0.0f, baseY);

                    for (int x = 0; x <= w; x += 8)
                        grain.lineTo((float) x, baseY + std::sin((float) x * 0.02f + (float) i) * 2.4f);

                    g.strokePath(grain, juce::PathStrokeType(1.0f + n * 1.6f));
                }

                break;
            }
        }

        //----------------------------------------------------------- vignette
        applyVignette(g, area, textureStyle == TextureStyle::blackface ? 0.55f : 0.42f);

        //------------------------------------------ chasis metalico + rejilla
        const auto controlArea = getControlArea();
        drawChassis(g, controlArea);
    }

    //----------------------------------------------------------------- rejilla
    const auto controlArea = getControlArea();

    if (! controlArea.isEmpty())
    {
        grilleCloth = juce::Image(juce::Image::ARGB, controlArea.getWidth(),
                                  controlArea.getHeight(), true);

        juce::Graphics gg(grilleCloth);
        const auto clothArea = juce::Rectangle<float>(0.0f, 0.0f,
                                                      (float) controlArea.getWidth(),
                                                      (float) controlArea.getHeight());

        const bool dark = (textureStyle == TextureStyle::tweed
                            || textureStyle == TextureStyle::sunburstWood);

        gg.setGradientFill(juce::ColourGradient(dark ? juce::Colour(0xff2b2118) : juce::Colour(0xff1a1a1c),
                                                clothArea.getCentreX(), clothArea.getY(),
                                                dark ? juce::Colour(0xff100b07) : juce::Colour(0xff080809),
                                                clothArea.getCentreX(), clothArea.getBottom(), false));
        gg.fillRect(clothArea);

        const auto thread = dark ? juce::Colour(0xffd9b567) : juce::Colour(0xffb9bec4);

        gg.setColour(thread.withAlpha(0.17f));

        for (float d = -clothArea.getHeight(); d < clothArea.getWidth(); d += 4.0f)
        {
            gg.drawLine(d, 0.0f, d + clothArea.getHeight(), clothArea.getHeight(), 1.0f);
            gg.drawLine(d, clothArea.getHeight(), d + clothArea.getHeight(), 0.0f, 1.0f);
        }

        gg.setColour(thread.withAlpha(0.07f));

        for (float d = -clothArea.getHeight(); d < clothArea.getWidth(); d += 2.0f)
            gg.drawLine(d, 0.0f, d + clothArea.getHeight(), clothArea.getHeight(), 0.6f);

        // Sombra interior de la rejilla.
        gg.setGradientFill(juce::ColourGradient(juce::Colour(0x99000000), clothArea.getCentre(),
                                                juce::Colours::transparentBlack,
                                                clothArea.getCentre().translated(clothArea.getWidth() * 0.6f,
                                                                                 clothArea.getHeight() * 0.6f),
                                                true));
        gg.fillRect(clothArea);
    }
    else
    {
        grilleCloth = {};
    }
}

//==============================================================================
void AmpPanel::drawChassis(juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    const auto r = area.toFloat();
    const auto corner = juce::jlimit(6.0f, 18.0f, 10.0f * uiScale);

    // Marco metalico exterior del chasis.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff6b6154), r.getCentreX(), r.getY(),
                                           juce::Colour(0xff1d1913), r.getCentreX(), r.getBottom(),
                                           false));
    g.fillRoundedRectangle(r, corner);

    g.setColour(juce::Colour(0xffe8c877).withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(1.0f), corner, 1.6f);

    g.setColour(juce::Colour(0xaa000000));
    g.drawRoundedRectangle(r.reduced(2.6f), corner - 1.0f, 1.2f);

    // Tornillos en las esquinas del chasis.
    const auto screwRadius = juce::jlimit(2.4f, 5.5f, 3.4f * uiScale);
    const auto inset = screwRadius * 3.0f;

    drawScrew(g, { r.getX() + inset, r.getY() + inset }, screwRadius);
    drawScrew(g, { r.getRight() - inset, r.getY() + inset }, screwRadius);
    drawScrew(g, { r.getX() + inset, r.getBottom() - inset }, screwRadius);
    drawScrew(g, { r.getRight() - inset, r.getBottom() - inset }, screwRadius);
}

//==============================================================================
void AmpPanel::drawHeader(juce::Graphics& g, juce::Rectangle<int> header)
{
    const auto h = header.toFloat();

    // Franja de la chapa frontal.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x66120e0a), h.getCentreX(), h.getY(),
                                           juce::Colour(0xaa0a0705), h.getCentreX(), h.getBottom(),
                                           false));
    g.fillRect(h);

    g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, h.getCentreX(), h.getY(),
                                           juce::Colour(0xaa000000), h.getCentreX(), h.getBottom(),
                                           false));
    g.fillRect(h);

    // Linea dorada inferior.
    {
        juce::ColourGradient line(juce::Colour(0x00e8c877), h.getX(), h.getBottom() - 2.0f,
                                  juce::Colour(0x00e8c877), h.getRight(), h.getBottom() - 2.0f,
                                  false);
        line.addColour(0.5, juce::Colour(0xffe8c877));
        g.setGradientFill(line);
        g.fillRect(h.getX(), h.getBottom() - 2.0f, h.getWidth(), 2.0f);
    }

    //--------------------------------------------------------------- nameplate
    auto nameArea = header.reduced(juce::roundToInt(26.0f * uiScale), 0);
    nameArea.removeFromRight(juce::roundToInt(120.0f * uiScale));

    const auto titleHeight = juce::jlimit(22.0f, 54.0f, 34.0f * uiScale);
    auto titleArea = nameArea.withSizeKeepingCentre(nameArea.getWidth(),
                                                    juce::roundToInt(titleHeight * 1.05f));
    titleArea.translate(0, -juce::roundToInt(6.0f * uiScale));

    const auto titleFont = AmpFonts::vintageSerif(titleHeight, true);
    g.setFont(titleFont);

    // Sombra + relieve del texto.
    g.setColour(juce::Colour(0xcc000000));
    g.drawFittedText(ampName, titleArea.translated(2, 3), juce::Justification::centred(), 1, 0.85f);

    g.setColour(juce::Colour(0x66ffe9b0));
    g.drawFittedText(ampName, titleArea.translated(0, -1), juce::Justification::centred(), 1, 0.85f);

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfffff0c4),
                                           (float) titleArea.getCentreX(), (float) titleArea.getY(),
                                           juce::Colour(0xffb9832f),
                                           (float) titleArea.getCentreX(), (float) titleArea.getBottom(),
                                           false));
    g.drawFittedText(ampName, titleArea, juce::Justification::centred(), 1, 0.85f);

    // Subtitulo.
    if (ampSubtitle.isNotEmpty())
    {
        auto subArea = nameArea.withSizeKeepingCentre(nameArea.getWidth(),
                                                      juce::roundToInt(titleHeight * 0.55f));
        subArea.translate(0, juce::roundToInt(titleHeight * 0.62f));

        g.setColour(juce::Colour(0xffe8c877).withAlpha(0.70f));
        g.setFont(AmpFonts::panelSans(juce::jlimit(9.0f, 17.0f, 11.0f * uiScale), false));
        g.drawFittedText(ampSubtitle, subArea, juce::Justification::centred(), 1, 0.7f);
    }

    //----------------------------------------------------------------- POWER
    const auto ledBounds = getPowerLedBounds();

    g.setColour(juce::Colour(0xffe8c877).withAlpha(0.80f));
    g.setFont(AmpFonts::panelSans(juce::jlimit(8.0f, 14.0f, 10.0f * uiScale), true));
    g.drawFittedText("POWER",
                     juce::Rectangle<int>(ledBounds.getX() - 40, ledBounds.getBottom() + 4,
                                          ledBounds.getWidth() + 80,
                                          juce::roundToInt(16.0f * uiScale)),
                     juce::Justification::centred(), 1, 0.7f);
}

void AmpPanel::drawPowerLed(juce::Graphics& g)
{
    const auto bounds = getPowerLedBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto radius = bounds.getWidth() * 0.5f;

    // Rosca metalica del portaled.
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8a7f6a),
                                           centre.translated(-radius * 0.5f, -radius * 0.5f),
                                           juce::Colour(0xff211b14),
                                           centre.translated(radius * 0.7f, radius * 0.7f), true));
    g.fillEllipse(bounds.expanded(radius * 0.30f));

    const auto pulse = 0.78f + 0.22f * std::sin(pulsePhase);
    const auto intensity = powerOn ? juce::jlimit(0.0f, 1.0f, pulse * (0.45f + 0.55f * ledIntensity))
                                   : 0.0f;

    // Halo.
    if (intensity > 0.01f)
    {
        const auto glowRadius = radius * (2.6f + intensity * 1.6f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff5a2a).withAlpha(0.45f * intensity),
                                               centre,
                                               juce::Colours::transparentBlack,
                                               centre.translated(glowRadius, glowRadius), true));
        g.fillEllipse(juce::Rectangle<float>(glowRadius * 2.0f, glowRadius * 2.0f).withCentre(centre));
    }

    const auto onColour = juce::Colour(0xffffd9a0).interpolatedWith(juce::Colour(0xffff3b12),
                                                                   1.0f - intensity * 0.55f);

    g.setGradientFill(juce::ColourGradient(powerOn ? onColour.brighter(0.75f)
                                                   : juce::Colour(0xff4a2a22),
                                           centre.translated(-radius * 0.35f, -radius * 0.35f),
                                           powerOn ? onColour.darker(0.55f)
                                                   : juce::Colour(0xff2a1a16),
                                           centre.translated(radius * 0.75f, radius * 0.75f), true));
    g.fillEllipse(bounds);

    g.setColour(juce::Colour(0xcc000000));
    g.drawEllipse(bounds.reduced(0.4f), 1.0f);

    // Reflejo especular.
    g.setColour(juce::Colours::white.withAlpha(powerOn ? 0.45f : 0.12f));
    g.fillEllipse(juce::Rectangle<float>(radius * 0.75f, radius * 0.55f)
                    .withCentre(centre.translated(-radius * 0.28f, -radius * 0.34f)));
}

//==============================================================================
void AmpPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    if (! texture.isValid() || texture.getWidth() != bounds.getWidth()
        || texture.getHeight() != bounds.getHeight())
    {
        regenerateTexture();
    }

    //----------------------------------------------------------------- mueble
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds.toFloat(), 6.0f);
        g.reduceClipRegion(clip);
        g.drawImageAt(texture, 0, 0);
    }

    //----------------------------------------------------------------- chapa
    drawHeader(g, getHeaderArea());

    //--------------------------------------------------------------- rejilla
    const auto controlArea = getControlArea();

    if (grilleCloth.isValid() && ! controlArea.isEmpty())
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addRoundedRectangle(controlArea.toFloat().reduced(3.0f),
                                 juce::jlimit(4.0f, 16.0f, 8.0f * uiScale));
        g.reduceClipRegion(clip);
        g.drawImageAt(grilleCloth, controlArea.getX(), controlArea.getY());

        // Brillo superior de la rejilla.
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.05f),
                                               controlArea.toFloat().getCentreX(),
                                               (float) controlArea.getY(),
                                               juce::Colours::transparentWhite,
                                               controlArea.toFloat().getCentreX(),
                                               (float) controlArea.getCentreY(), false));
        g.fillRect(controlArea);
    }

    //------------------------------------------------------------------- LED
    drawPowerLed(g);

    // Borde exterior del mueble.
    g.setColour(juce::Colour(0xcc000000));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 6.0f, 1.6f);
    g.setColour(juce::Colour(0x33e8c877));
    g.drawRoundedRectangle(bounds.toFloat().reduced(2.0f), 5.0f, 1.0f);
}
