#include "PluginEditor.h"
#include "PluginProcessor.h"

//==============================================================================
namespace editorIds
{
    const juce::String ampModel    { "ampModel" };
    const juce::String gateEnabled { "gateEnabled" };
}

//==============================================================================
namespace
{
    /** Convierte la envolvente del gate (dB) en un nivel 0 .. 1 para los LEDs. */
    float envelopeDbToLevel(float envelopeDb) noexcept
    {
        constexpr float floorDb = -54.0f;
        return juce::jlimit(0.0f, 1.0f, (envelopeDb - floorDb) / -floorDb);
    }
}

//==============================================================================
GuitarAmpAudioProcessorEditor::GuitarAmpAudioProcessorEditor(GuitarAmpAudioProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef(p),
      controlPanel(p.getAPVTS())
{
    //--------------------------------------------------- LookAndFeel global
    lookAndFeel = std::make_unique<AmpLookAndFeel>();

    juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());

    setLookAndFeel(lookAndFeel.get());
    ampPanel.setLookAndFeel(lookAndFeel.get());
    controlPanel.setLookAndFeel(lookAndFeel.get());

    // El mueble usa la paleta "tweed"; los knobs, el dorado vintage.
    lookAndFeel->setKnobColours(AmpKnobColours::vintageGold());

    //------------------------------------------------------------ componentes
    ampPanel.setTextureStyle(AmpPanel::TextureStyle::tweed);
    ampPanel.setAmpName("GUITAR AMP");
    ampPanel.setAmpSubtitle("ALL-TUBE GUITAR AMPLIFIER  \xc2\xb7  VST3");
    ampPanel.setPowerOn(true);

    addAndMakeVisible(ampPanel);
    addAndMakeVisible(controlPanel);

    // Al cambiar de modelo se refresca el LED y el titulo de la chapa.
    controlPanel.onModelChanged = [this](int modelIndex)
    {
        static const char* const subtitles[] =
        {
            "CLEAN CHANNEL  \xc2\xb7  ALL-TUBE GUITAR AMPLIFIER",
            "CRUNCH CHANNEL  \xc2\xb7  ALL-TUBE GUITAR AMPLIFIER",
            "DISTORTION CHANNEL  \xc2\xb7  ALL-TUBE GUITAR AMPLIFIER"
        };

        ampPanel.setAmpSubtitle(subtitles[juce::jlimit(0, 2, modelIndex)]);
    };

    ampPanel.onPowerLedClicked = [this]
    {
        ampPanel.setPowerOn(! ampPanel.isPowerOn());
    };

    //-------------------------------------------------------------- APVTS
    setupModelButtonAttachments();
    setupGateAttachment();

    // Estado inicial coherente con los parametros guardados.
    controlPanel.refreshFromParameters();

    //--------------------------------------------------------------- ventana
    setResizable(true, true);
    setResizeLimits(720, 420, 1900, 1100);

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio((double) designWidth / (double) designHeight);

    setSize(designWidth, designHeight);
    updateUiScale();

    // Feedback visual del LED de encendido.
    startTimerHz(20);
}

GuitarAmpAudioProcessorEditor::~GuitarAmpAudioProcessorEditor()
{
    stopTimer();

    // Los attachments deben morir antes que los componentes y el APVTS.
    gateEnableAttachment.reset();

    for (auto& attachment : modelButtonAttachments)
        attachment.reset();

    // Se liberan los componentes del LookAndFeel antes de destruirlo.
    controlPanel.setLookAndFeel(nullptr);
    ampPanel.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);

    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    lookAndFeel.reset();
}

//==============================================================================
void GuitarAmpAudioProcessorEditor::setupModelButtonAttachments()
{
    auto& apvts = processorRef.getAPVTS();

    // Cada switch representa un unico valor del parametro de eleccion:
    // Clean -> 0, Crunch -> 1, Distortion -> 2.
    for (int i = 0; i < 3; ++i)
    {
        if (apvts.getParameter(editorIds::ampModel) == nullptr)
            break;

        modelButtonAttachments[(size_t) i] =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                apvts, editorIds::ampModel, controlPanel.getModelButton(i),
                juce::NormalisableRange<float>((float) i, (float) i, 1.0f));
    }
}

void GuitarAmpAudioProcessorEditor::setupGateAttachment()
{
    auto& apvts = processorRef.getAPVTS();

    // Parametro opcional: si el processor todavia no expone "gateEnabled",
    // el switch sigue funcionando como indicador visual sin binding.
    if (apvts.getParameter(editorIds::gateEnabled) != nullptr)
        gateEnableAttachment =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                apvts, editorIds::gateEnabled, controlPanel.getGateEnableButton());
}

//==============================================================================
void GuitarAmpAudioProcessorEditor::updateUiScale()
{
    const auto scale = juce::jlimit(0.6f, 2.2f,
                                    juce::jmin((float) getWidth() / (float) designWidth,
                                               (float) getHeight() / (float) designHeight));

    if (std::abs(scale - uiScale) < 0.005f)
        return;

    uiScale = scale;

    if (lookAndFeel != nullptr)
        lookAndFeel->setUiScale(uiScale);

    ampPanel.setUiScale(uiScale);
    controlPanel.setUiScale(uiScale);
}

void GuitarAmpAudioProcessorEditor::layoutComponents()
{
    ampPanel.setBounds(getLocalBounds());

    auto controlArea = ampPanel.getControlArea();
    const auto inset = juce::roundToInt(juce::jlimit(4.0f, 22.0f, 12.0f * uiScale));

    controlPanel.setBounds(controlArea.reduced(inset, inset));
}

void GuitarAmpAudioProcessorEditor::resized()
{
    updateUiScale();
    layoutComponents();
}

void GuitarAmpAudioProcessorEditor::timerCallback()
{
    auto* gate = processorRef.getNoiseGate();

    if (! ampPanel.isPowerOn())
    {
        ampPanel.setLedIntensity(0.0f);
        controlPanel.setGateMeter(false, 0.0f);
        return;
    }

    // La envolvente del noise gate alimenta el LED de encendido y el medidor
    // del panel de controles (feedback visual de la senal de entrada).
    const auto level = gate != nullptr ? envelopeDbToLevel(gate->getEnvelopeDb()) : 0.0f;
    const auto gateOpen = gate != nullptr ? gate->isOpen() : false;

    ledActivityTarget = level;
    ledActivity += (ledActivityTarget - ledActivity) * 0.30f;

    ampPanel.setLedIntensity(juce::jlimit(0.20f, 1.0f, 0.35f + ledActivity * 0.65f));
    controlPanel.setGateMeter(gateOpen, level);
}

//==============================================================================
void GuitarAmpAudioProcessorEditor::paint(juce::Graphics& g)
{
    // El mueble cubre toda la ventana; este relleno evita parpadeos al redimensionar.
    g.fillAll(juce::Colour(0xff0b0806));

    // Nombre y version en la esquina inferior derecha (estilo placa trasera).
    g.setColour(juce::Colour(0xffe8c877).withAlpha(0.35f));
    g.setFont(AmpFonts::panelSans(juce::jlimit(8.0f, 13.0f, 10.0f * uiScale), false));
    g.drawText("v1.0  \xc2\xb7  OpenAmp",
               getLocalBounds().reduced(juce::roundToInt(18.0f * uiScale)).removeFromBottom(14),
               juce::Justification::centredRight(), false);
}
