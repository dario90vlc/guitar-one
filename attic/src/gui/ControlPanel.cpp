#include "ControlPanel.h"

//==============================================================================
namespace cpIds
{
    // Parametros globales (definidos en GuitarAmpAudioProcessor).
    const juce::String ampModel      { "ampModel" };
    const juce::String gateThreshold { "gateThreshold" };
    const juce::String gateRatio     { "gateRatio" };
    const juce::String gateEnabled   { "gateEnabled" };
    const juce::String outputLevel   { "outputLevel" };

    // Sufijos de los parametros por modelo.
    const char* const toneSuffixes[] = { "Gain", "Bass", "Mid", "Treble", "Presence", "Volume" };

    const juce::Colour gold      { 0xffe8c877 };
    const juce::Colour goldDark  { 0xff8a6a34 };
    const juce::Colour panelDark { 0xff17120e };
    const juce::Colour textDim   { 0xffbfa87a };
}

//==============================================================================
juce::StringArray ControlPanel::getMainKnobNames()
{
    return { "GAIN", "BASS", "MID", "TREBLE", "PRESENCE", "VOLUME" };
}

juce::String ControlPanel::getModelPrefix(int modelIndex)
{
    switch (modelIndex)
    {
        case 1:  return "crunch";
        case 2:  return "dist";
        case 0:
        default: return "clean";
    }
}

//==============================================================================
ControlPanel::ControlPanel(juce::AudioProcessorValueTreeState& stateToControl)
    : apvts(stateToControl)
{
    setOpaque(false);

    //------------------------------------------------------- knobs principales
    const auto names = getMainKnobNames();

    const double defaults[]  = { 0.5, 0.5, 0.5, 0.5, 0.5, 0.7 };

    for (int i = 0; i < numMainKnobs; ++i)
    {
        createKnob(knobs[(size_t) i], names[i]);
        knobs[(size_t) i].setup(0.0, 1.0, 0.001, defaults[i], " %");
        knobs[(size_t) i].setTooltip(names[i] + " del canal - doble clic para volver al valor por defecto");
        addAndMakeVisible(knobs[(size_t) i]);

        createCaption(knobCaptions[(size_t) i], names[i]);
        addAndMakeVisible(knobCaptions[(size_t) i]);
    }

    //------------------------------------------------------ selector de modelo
    createCaption(modelCaption, "AMP MODEL");
    addAndMakeVisible(modelCaption);

    static const char* const modelNames[] = { "CLEAN", "CRUNCH", "DISTORTION" };

    for (int i = 0; i < 3; ++i)
    {
        auto& button = modelButtons[(size_t) i];
        button.setButtonText(modelNames[i]);
        button.setClickingTogglesState(false);
        button.setToggleable(true);
        button.addListener(this);
        button.setTooltip("Selecciona el modelo de amplificador: " + juce::String(modelNames[i]));
        addAndMakeVisible(button);
    }

    modelBox.addItemList({ "Clean", "Crunch", "Distortion" }, 1);
    modelBox.setTooltip("Modelo de amplificador");
    modelBox.setVisible(false);          // binding invisible; los switches son la UI
    modelBox.setWantsKeyboardFocus(false);
    addAndMakeVisible(modelBox);
    modelBox.addListener(this);

    //------------------------------------------------------------- noise gate
    createCaption(gateCaption, "NOISE GATE");
    addAndMakeVisible(gateCaption);

    gateEnableButton.setButtonText("GATE");
    gateEnableButton.setClickingTogglesState(true);
    gateEnableButton.setTooltip("Activa o desactiva el noise gate");
    gateEnableButton.addListener(this);
    addAndMakeVisible(gateEnableButton);

    createKnob(gateThreshold, "THRESHOLD");
    gateThreshold.setup(-80.0, -20.0, 0.1, -60.0, " dB");
    gateThreshold.setValueFormat(AmpKnob::ValueFormat::decibels);
    gateThreshold.setTooltip("Umbral del noise gate (dB) - por debajo de este nivel se silencia la entrada");
    addAndMakeVisible(gateThreshold);

    createKnob(gateRatio, "RATIO");
    gateRatio.setup(1.0, 20.0, 0.1, 4.0, ":1");
    gateRatio.setValueFormat(AmpKnob::ValueFormat::ratio);
    gateRatio.setTooltip("Relacion del noise gate - cuanto se atenua la senal por debajo del umbral");
    addAndMakeVisible(gateRatio);

    createCaption(gateThresholdCaption, "THRESHOLD");
    addAndMakeVisible(gateThresholdCaption);

    createCaption(gateRatioCaption, "RATIO");
    addAndMakeVisible(gateRatioCaption);

    //---------------------------------------------------------------- salida
    createCaption(outputCaption, "OUTPUT LEVEL");
    addAndMakeVisible(outputCaption);

    createKnob(outputLevel, "OUTPUT");
    outputLevel.setup(0.0, 1.0, 0.001, 0.8, " %");
    outputLevel.setTooltip("Nivel de salida general del plugin");
    addAndMakeVisible(outputLevel);

    createCaption(outputKnobCaption, "LEVEL");
    addAndMakeVisible(outputKnobCaption);

    //------------------------------------------------------------ attachments
    gateThresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, cpIds::gateThreshold, gateThreshold);
    gateRatioAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, cpIds::gateRatio, gateRatio);
    outputLevelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, cpIds::outputLevel, outputLevel);

    // Binding oficial del parametro de eleccion (requisito del proyecto).
    modelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, cpIds::ampModel, modelBox);

    // Estado inicial del modelo y de los knobs de tono.
    if (auto* raw = apvts.getRawParameterValue(cpIds::ampModel))
        currentModel = juce::jlimit(0, 2, (int) raw->load());

    if (auto* gateRaw = apvts.getRawParameterValue(cpIds::gateEnabled))
        gateSwitchState = gateRaw->load() > 0.5f;

    gateEnableButton.setToggleState(gateSwitchState, juce::dontSendNotification);
    gateActive = gateSwitchState;

    rebindKnobsToModel(currentModel);
    updateModelButtons();
}

ControlPanel::~ControlPanel()
{
    stopTimer();
    modelBox.removeListener(this);
    gateEnableButton.removeListener(this);

    for (auto& button : modelButtons)
        button.removeListener(this);

    // Los attachments deben destruirse antes que los componentes: se liberan aqui
    // de forma explicita para no depender del orden de destruccion de miembros.
    for (auto& attachment : knobAttachments)
        attachment.reset();

    gateThresholdAttachment.reset();
    gateRatioAttachment.reset();
    outputLevelAttachment.reset();
    modelAttachment.reset();
}

//==============================================================================
void ControlPanel::createKnob(AmpKnob& knob, const juce::String& name)
{
    knob.setName(name);
    knob.setLabelText(name);
    knob.setShowNameLabel(false);        // el nombre va en la etiqueta inferior
    knob.setUiScale(uiScale);
}

void ControlPanel::createCaption(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred());
    label.setInterceptsMouseClicks(false, false);
    label.setColour(juce::Label::textColourId, cpIds::gold);
}

void ControlPanel::setUiScale(float newScale)
{
    uiScale = juce::jlimit(0.6f, 2.0f, newScale);

    for (auto& knob : knobs)
        knob.setUiScale(uiScale);

    gateThreshold.setUiScale(uiScale);
    gateRatio.setUiScale(uiScale);
    outputLevel.setUiScale(uiScale);

    resized();
    repaint();
}

//==============================================================================
void ControlPanel::selectModel(int modelIndex, bool notify)
{
    const auto clamped = juce::jlimit(0, 2, modelIndex);
    const bool changed = (clamped != currentModel);

    if (changed)
    {
        currentModel = clamped;
        rebindKnobsToModel(currentModel);
        updateModelButtons();
        repaint();
    }

    // Solo se notifica cuando el modelo cambia de verdad: el ComboBox y los
    // switches escriben el mismo parametro, asi que un clic puede disparar dos
    // avisos y no queremos refrescar el editor dos veces.
    if (notify && changed && onModelChanged != nullptr)
        onModelChanged(currentModel);
}

void ControlPanel::rebindKnobsToModel(int modelIndex)
{
    const auto prefix = getModelPrefix(modelIndex);

    for (int i = 0; i < numMainKnobs; ++i)
    {
        knobAttachments[(size_t) i].reset();

        const auto parameterID = prefix + cpIds::toneSuffixes[i];

        if (apvts.getParameter(parameterID) != nullptr)
            knobAttachments[(size_t) i] =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    apvts, parameterID, knobs[(size_t) i]);
        else
            jassertfalse;   // parametro no encontrado: revisar el layout del processor
    }
}

void ControlPanel::updateModelButtons()
{
    for (int i = 0; i < 3; ++i)
        modelButtons[(size_t) i].setToggleState(i == currentModel, juce::dontSendNotification);
}

void ControlPanel::refreshFromParameters()
{
    if (auto* raw = apvts.getRawParameterValue(cpIds::ampModel))
        selectModel((int) raw->load(), false);

    if (auto* gateRaw = apvts.getRawParameterValue(cpIds::gateEnabled))
    {
        gateSwitchState = gateRaw->load() > 0.5f;
        gateEnableButton.setToggleState(gateSwitchState, juce::dontSendNotification);
        setGateActive(gateSwitchState);
    }

    if (auto* gateThresholdRaw = apvts.getRawParameterValue(cpIds::gateThreshold))
        gateThreshold.setValue((double) gateThresholdRaw->load(), juce::dontSendNotification);

    if (auto* gateRatioRaw = apvts.getRawParameterValue(cpIds::gateRatio))
        gateRatio.setValue((double) gateRatioRaw->load(), juce::dontSendNotification);

    if (auto* outputRaw = apvts.getRawParameterValue(cpIds::outputLevel))
        outputLevel.setValue((double) outputRaw->load(), juce::dontSendNotification);

    repaint();
}

void ControlPanel::setGateActive(bool isActive)
{
    if (gateActive == isActive)
        return;

    gateActive = isActive;
    repaint();
}

void ControlPanel::setGateMeter(bool gateIsOpen, float inputLevel)
{
    const auto level = juce::jlimit(0.0f, 1.0f, inputLevel);

    // El LED del gate solo se anima cuando hay cambios apreciables.
    if (gateIsOpen == gateOpen && std::abs(level - gateInputLevel) < 0.01f)
        return;

    const bool wasPulsing = (gateOpen != gateIsOpen) || gateIsOpen;

    gateOpen = gateIsOpen;
    gateInputLevel = level;

    if (wasPulsing)
    {
        gateLedPulse = 1.0f;
        startTimerHz(30);
    }

    repaint(getLocalBounds().removeFromBottom(getHeight() / 2));
}

void ControlPanel::timerCallback()
{
    gateLedPulse *= 0.82f;

    if (gateLedPulse < 0.02f)
    {
        gateLedPulse = 0.0f;
        stopTimer();
    }

    repaint(getLocalBounds().removeFromBottom(getHeight() / 2));
}

//==============================================================================
void ControlPanel::comboBoxChanged(juce::ComboBox* box)
{
    if (box == &modelBox)
        selectModel(modelBox.getSelectedItemIndex(), true);
}

void ControlPanel::buttonClicked(juce::Button* button)
{
    for (int i = 0; i < 3; ++i)
    {
        if (button == &modelButtons[(size_t) i])
        {
            modelBox.setSelectedItemIndex(i, juce::sendNotificationSync);
            selectModel(i, true);
            return;
        }
    }

    if (button == &gateEnableButton)
    {
        gateSwitchState = gateEnableButton.getToggleState();
        setGateActive(gateSwitchState);
    }
}

//==============================================================================
ControlPanel::Layout ControlPanel::computeLayout() const
{
    Layout l;

    l.separatorHeight = juce::roundToInt(juce::jlimit(6.0f, 22.0f, 12.0f * uiScale));
    l.knobRowHeight  = juce::roundToInt((float) getHeight() * 0.40f);
    l.modelRowY      = l.knobRowHeight + l.separatorHeight;
    l.modelRowHeight = juce::roundToInt(juce::jlimit(44.0f, 78.0f, 60.0f * uiScale));
    l.gateRowY       = l.modelRowY + l.modelRowHeight + l.separatorHeight;
    l.gateRowHeight  = juce::jmax(1, getHeight() - l.gateRowY - 6);
    l.captionHeight  = juce::roundToInt(juce::jlimit(11.0f, 24.0f, 15.0f * uiScale));

    return l;
}

//==============================================================================
void ControlPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto layout = computeLayout();

    //---------------------------------------------------------- placa frontal
    {
        juce::ColourGradient plate(juce::Colour(0x4d0d0a07), bounds.getCentreX(), bounds.getY(),
                                   juce::Colour(0x80050403), bounds.getCentreX(), bounds.getBottom(),
                                   false);
        g.setGradientFill(plate);
        g.fillRoundedRectangle(bounds, 8.0f);

        g.setColour(cpIds::gold.withAlpha(0.35f));
        g.drawRoundedRectangle(bounds.reduced(0.75f), 8.0f, 1.4f);

        g.setColour(juce::Colour(0x66000000));
        g.drawRoundedRectangle(bounds.reduced(3.0f), 6.0f, 1.0f);
    }

    const auto row = [this](int y, int h)
    {
        return juce::Rectangle<int>(10, y, juce::jmax(1, getWidth() - 20), h);
    };

    // Titulo de seccion con subrayado dorado degradado.
    const auto drawSectionTitle = [&g](juce::Rectangle<int> area, const juce::String& text)
    {
        if (text.isEmpty())
            return;

        const auto titleHeight = juce::jlimit(10, 18, area.getHeight() / 2);
        auto titleArea = area.removeFromTop(titleHeight);

        g.setFont(AmpFonts::panelSans(juce::jlimit(9.5f, 15.0f, (float) titleHeight * 0.85f), true));
        g.setColour(juce::Colour(0xcc000000));
        g.drawFittedText(text, titleArea.translated(1, 1), juce::Justification::centred(), 1, 0.7f);
        g.setColour(cpIds::gold.withAlpha(0.92f));
        g.drawFittedText(text, titleArea, juce::Justification::centred(), 1, 0.7f);

        const auto lineY = (float) titleArea.getBottom() + 1.0f;
        const auto cx = (float) titleArea.getCentreX();
        const auto half = (float) titleArea.getWidth() * 0.5f;

        juce::ColourGradient line(juce::Colour(0x00e8c877), cx - half, lineY,
                                  juce::Colour(0x00e8c877), cx + half, lineY, false);
        line.addColour(0.5, cpIds::goldDark.withAlpha(0.85f));
        g.setGradientFill(line);
        g.fillRect(cx - half, lineY, half * 2.0f, 1.0f);
    };

    // LED redondo con halo (reutilizado por el modelo y el gate).
    const auto drawLed = [&g](juce::Point<float> centre, float radius, juce::Colour colour)
    {
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.55f), centre,
                                               juce::Colours::transparentBlack,
                                               centre.translated(radius * 4.5f, radius * 4.5f), true));
        g.fillEllipse(juce::Rectangle<float>(radius * 9.0f, radius * 9.0f).withCentre(centre));

        g.setGradientFill(juce::ColourGradient(colour.brighter(0.75f),
                                               centre.translated(-radius * 0.3f, -radius * 0.3f),
                                               colour.darker(0.45f),
                                               centre.translated(radius, radius), true));
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

        g.setColour(juce::Colour(0x88000000));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 0.8f);
    };

    //------------------------------------------- sombras bajo los knobs (base)
    {
        auto area = row(0, layout.knobRowHeight);
        const auto cellWidth = juce::jmax(1, area.getWidth() / numMainKnobs);

        for (int i = 0; i < numMainKnobs; ++i)
        {
            auto cell = area.removeFromLeft(cellWidth);
            const auto cx = (float) cell.getCentreX();
            const auto cy = (float) cell.getBottom() - (float) layout.captionHeight - 4.0f;
            const auto d = juce::jmin((float) cell.getWidth(), (float) cell.getHeight()) * 0.86f;

            g.setGradientFill(juce::ColourGradient(juce::Colour(0x55000000), { cx, cy },
                                                   juce::Colours::transparentBlack,
                                                   { cx + d * 0.5f, cy + d * 0.5f }, true));
            g.fillEllipse(juce::Rectangle<float>(d, d).withCentre({ cx, cy }));
        }
    }

    //------------------------------------------------------- selector de modelo
    {
        auto area = row(layout.modelRowY, layout.modelRowHeight);
        drawSectionTitle(area, "AMP MODEL");

        const auto ledRadius = juce::jlimit(3.0f, 6.0f, 4.0f * uiScale);
        const auto ledCentre = juce::Point<float>((float) area.getX() + ledRadius * 2.6f,
                                                  (float) area.getY() + ledRadius * 2.2f);

        const juce::Colour modelColours[3] = { juce::Colour(0xff58c9ff),
                                               juce::Colour(0xffffc14d),
                                               juce::Colour(0xffff4a2a) };

        drawLed(ledCentre, ledRadius, modelColours[juce::jlimit(0, 2, currentModel)]);
    }

    //------------------------------------------------------------- noise gate
    {
        auto area = row(layout.gateRowY, layout.gateRowHeight);
        auto gateArea = area.removeFromLeft(juce::roundToInt((float) area.getWidth() * 0.66f));

        drawSectionTitle(gateArea, "NOISE GATE");

        // LED de estado del gate: se ilumina cuando la puerta esta abierta
        // (deja pasar senal) y se atenua cuando esta cerrada.
        {
            const auto ledRadius = juce::jlimit(3.0f, 6.5f, 4.2f * uiScale);
            const auto ledCentre = juce::Point<float>((float) gateArea.getRight() - ledRadius * 3.4f,
                                                      (float) gateArea.getY() + ledRadius * 2.2f);

            const auto glow = gateActive ? juce::jlimit(0.15f, 1.0f,
                                                        0.30f + 0.45f * gateInputLevel
                                                              + 0.35f * gateLedPulse)
                                         : 0.0f;

            const auto colour = (gateActive && gateOpen) ? juce::Colour(0xffffb347)
                                                         : juce::Colour(0xff8a2f14);

            if (glow > 0.01f)
            {
                g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.55f * glow), ledCentre,
                                                       juce::Colours::transparentBlack,
                                                       ledCentre.translated(ledRadius * 4.5f,
                                                                            ledRadius * 4.5f), true));
                g.fillEllipse(juce::Rectangle<float>(ledRadius * 9.0f, ledRadius * 9.0f)
                                .withCentre(ledCentre));
            }

            const auto lit = colour.withMultipliedBrightness(0.55f + glow * 0.9f);
            g.setGradientFill(juce::ColourGradient(lit.brighter(0.7f),
                                                   ledCentre.translated(-ledRadius * 0.3f,
                                                                        -ledRadius * 0.3f),
                                                   lit.darker(0.5f),
                                                   ledCentre.translated(ledRadius, ledRadius), true));
            g.fillEllipse(juce::Rectangle<float>(ledRadius * 2.0f, ledRadius * 2.0f).withCentre(ledCentre));

            g.setColour(juce::Colour(0x88000000));
            g.drawEllipse(juce::Rectangle<float>(ledRadius * 2.0f, ledRadius * 2.0f).withCentre(ledCentre),
                          0.8f);
        }

        // Separador vertical con el bloque de salida.
        const float separatorX = (float) gateArea.getRight() + 6.0f;
        const float lineTop = (float) gateArea.getY();
        const float lineBottom = (float) gateArea.getBottom();

        const juce::Colour lineColour (0x00e8c877);
        juce::ColourGradient vline (lineColour, separatorX, lineTop,
                                    lineColour, separatorX, lineBottom, false);
        vline.addColour(0.5, cpIds::goldDark.withAlpha(0.7f));
        g.setGradientFill(vline);
        g.fillRect(separatorX, lineTop + 6.0f, 1.0f,
                   (float) juce::jmax(1, gateArea.getHeight() - 12));
    }

    //----------------------------------------------------------------- salida
    {
        auto area = row(layout.gateRowY, layout.gateRowHeight);
        auto outputArea = area.removeFromRight(juce::roundToInt((float) area.getWidth() * 0.34f));
        drawSectionTitle(outputArea, "OUTPUT");
    }

    // Etiqueta de estado del switch de gate (ON / OFF) bajo el switch.
    {
        auto area = row(layout.gateRowY, layout.gateRowHeight);
        auto gateArea = area.removeFromLeft(juce::roundToInt((float) area.getWidth() * 0.66f));
        const auto titleHeight = juce::jlimit(10, 18, gateArea.getHeight() / 2);
        auto content = gateArea.withTrimmedTop(titleHeight).reduced(4, 0);

        auto switchArea = content.removeFromLeft(juce::jmax(1, content.getWidth() / 4));
        auto stateArea = switchArea.removeFromBottom(layout.captionHeight);

        g.setFont(AmpFonts::panelSans(juce::jlimit(9.0f, 14.0f, (float) stateArea.getHeight() * 0.72f), true));
        g.setColour(gateSwitchState ? cpIds::gold : cpIds::textDim.withAlpha(0.6f));
        g.drawFittedText(gateSwitchState ? "ON" : "OFF", stateArea,
                         juce::Justification::centred(), 1, 0.8f);
    }
}

//==============================================================================
void ControlPanel::resized()
{
    const auto layout = computeLayout();

    //------------------------------------------------------- knobs principales
    {
        auto area = juce::Rectangle<int>(10, 0, juce::jmax(1, getWidth() - 20), layout.knobRowHeight);
        const auto cellWidth = juce::jmax(1, area.getWidth() / numMainKnobs);

        for (int i = 0; i < numMainKnobs; ++i)
        {
            auto cell = area.removeFromLeft(cellWidth).reduced(2, 0);
            auto caption = cell.removeFromBottom(layout.captionHeight);

            knobCaptions[(size_t) i].setBounds(caption);
            knobs[(size_t) i].setBounds(cell.reduced(1));
        }
    }

    //------------------------------------------------------ selector de modelo
    {
        auto area = juce::Rectangle<int>(10, layout.modelRowY, juce::jmax(1, getWidth() - 20),
                                         layout.modelRowHeight);
        const auto titleHeight = juce::jlimit(10, 18, area.getHeight() / 2);

        modelCaption.setBounds(area.getX(), layout.modelRowY, area.getWidth(), titleHeight);
        area.removeFromTop(titleHeight);

        auto buttons = area.reduced(6, 0);
        const auto buttonWidth = juce::jmax(1, buttons.getWidth() / 3);

        for (auto& button : modelButtons)
            button.setBounds(buttons.removeFromLeft(buttonWidth).reduced(4, 2));

        // El ComboBox solo existe para el binding: se mantiene oculto pero con
        // limites validos para que los menus emergentes funcionen si se muestra.
        modelBox.setBounds(juce::Rectangle<int>(10, layout.modelRowY, 160, titleHeight));
    }

    //---------------------------------------------- noise gate + output level
    {
        auto area = juce::Rectangle<int>(10, layout.gateRowY, juce::jmax(1, getWidth() - 20),
                                         layout.gateRowHeight);
        auto gateArea = area.removeFromLeft(juce::roundToInt((float) area.getWidth() * 0.66f));
        auto outputArea = area;

        const auto titleHeight = juce::jlimit(10, 18, gateArea.getHeight() / 2);

        gateCaption.setBounds(gateArea.getX(), layout.gateRowY, gateArea.getWidth(), titleHeight);
        outputCaption.setBounds(outputArea.getX(), layout.gateRowY, outputArea.getWidth(), titleHeight);

        //--- switch de activacion + knobs del gate
        auto gateContent = gateArea.withTrimmedTop(titleHeight).reduced(4, 0);
        auto switchArea = gateContent.removeFromLeft(juce::jmax(1, gateContent.getWidth() / 4));

        {
            auto stateArea = switchArea.removeFromBottom(layout.captionHeight);
            const auto switchHeight = juce::jlimit(24, 46, switchArea.getHeight() - 6);
            const auto switchWidth = juce::jmin(switchArea.getWidth() - 8,
                                                juce::roundToInt(78.0f * uiScale));

            gateEnableButton.setBounds(switchArea.withSizeKeepingCentre(switchWidth, switchHeight));

            juce::ignoreUnused(stateArea);   // "ON / OFF" se dibuja en paint()
        }

        {
            const auto cellWidth = juce::jmax(1, gateContent.getWidth() / 2);

            auto thresholdCell = gateContent.removeFromLeft(cellWidth).reduced(2, 0);
            auto ratioCell = gateContent.reduced(2, 0);

            gateThresholdCaption.setBounds(thresholdCell.removeFromBottom(layout.captionHeight));
            gateRatioCaption.setBounds(ratioCell.removeFromBottom(layout.captionHeight));

            gateThreshold.setBounds(thresholdCell);
            gateRatio.setBounds(ratioCell);
        }

        //--- nivel de salida
        {
            auto outputContent = outputArea.withTrimmedTop(titleHeight).reduced(8, 0);
            outputKnobCaption.setBounds(outputContent.removeFromBottom(layout.captionHeight));
            outputLevel.setBounds(outputContent);
        }
    }
}

